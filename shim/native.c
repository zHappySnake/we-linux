/*
 * Native Linux windows as Wallpaper Engine sees them.
 *
 * Wallpaper Engine pauses or stops wallpapers when another application is focused, maximized
 * or fullscreen (per the user's settings). It finds those applications by enumerating windows,
 * which under Wine only covers Wine's own. The KWin script reports the native windows on screen
 * through we-linux-agent into a state file (path in WE_LINUX_STATE); we turn each one into a
 * virtual window handle, append it to EnumWindows and answer the questions Wallpaper Engine asks
 * about it: style, minimized, cloaked, rectangle, monitor, owner process, class, and whether it is
 * the foreground window.
 *
 * Virtual handles never reach Wine: every hooked call checks for them first and passes anything
 * else through untouched.
 */
#include "shim.h"
#include <dwmapi.h>
#include <stdio.h>
#include <string.h>

#define MAX_NATIVE 128
#define NATIVE_CLASS L"WeLinuxNativeWindow"
/* Handles in a range Wine never hands out (generation 0xfffe), and a process id that doesn't exist. */
#define NATIVE_HANDLE_BASE 0xfffe0000u
#define NATIVE_PID 0xfffffff0u
#define NATIVE_TID 0xfffffff0u

struct native_window {
    HWND hwnd;
    RECT rect;  /* Windows virtual-screen coordinates */
    BOOL maximized, fullscreen;
};

static CRITICAL_SECTION lock;
static struct native_window windows[MAX_NATIVE];
static int window_count;
static HWND active;
static wchar_t state_path[MAX_PATH];

static HWND handle_for(unsigned id)
{
    return (HWND)(ULONG_PTR)(NATIVE_HANDLE_BASE | (0x20 + ((id % 0x7fe0) << 1)));
}

static BOOL is_native(HWND hwnd)
{
    return ((ULONG_PTR)hwnd & 0xffff0000u) == NATIVE_HANDLE_BASE && ((ULONG_PTR)hwnd >> 32) == 0;
}

static BOOL lookup(HWND hwnd, struct native_window *out)
{
    if (!is_native(hwnd)) return FALSE;
    BOOL found = FALSE;
    EnterCriticalSection(&lock);
    for (int i = 0; i < window_count; i++) {
        if (windows[i].hwnd != hwnd) continue;
        *out = windows[i];
        found = TRUE;
        break;
    }
    LeaveCriticalSection(&lock);
    return found;
}

/* ---- state file --------------------------------------------------------- */

/* KWin reports compositor coordinates; scale them onto Windows' virtual screen, using the
 * desktop window (which spans all monitors in both) as the reference. */
struct mapping { long kx, ky, kw, kh; long wx, wy, ww, wh; };

static RECT map_rect(const struct mapping *m, long x, long y, long w, long h)
{
    RECT r;
    r.left = m->wx + (x - m->kx) * m->ww / m->kw;
    r.top = m->wy + (y - m->ky) * m->wh / m->kh;
    r.right = m->wx + (x + w - m->kx) * m->ww / m->kw;
    r.bottom = m->wy + (y + h - m->ky) * m->wh / m->kh;
    return r;
}

static void load_state(void)
{
    HANDLE f = CreateFileW(state_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    static char buf[32768];
    DWORD len = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &len, NULL);
    CloseHandle(f);
    buf[len] = 0;

    struct native_window parsed[MAX_NATIVE];
    int count = 0;
    unsigned active_id = 0;
    struct mapping m = { 0 };
    m.wx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    m.wy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    m.ww = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m.wh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
        long x, y, w, h;
        unsigned id;
        char flags[8];
        if (sscanf(line, "screen %ld %ld %ld %ld", &x, &y, &w, &h) == 4) {
            m.kx = x; m.ky = y; m.kw = w; m.kh = h;
        } else if (sscanf(line, "win %u %ld %ld %ld %ld %7s", &id, &x, &y, &w, &h, flags) == 6 &&
                   count < MAX_NATIVE && m.kw > 0 && m.kh > 0) {
            struct native_window *nw = &parsed[count++];
            nw->hwnd = handle_for(id);
            nw->rect = map_rect(&m, x, y, w, h);
            nw->maximized = strchr(flags, 'M') != NULL;
            nw->fullscreen = strchr(flags, 'F') != NULL;
        } else {
            sscanf(line, "active %u", &active_id);
        }
    }

    EnterCriticalSection(&lock);
    memcpy(windows, parsed, count * sizeof(parsed[0]));
    window_count = count;
    active = active_id ? handle_for(active_id) : NULL;
    LeaveCriticalSection(&lock);
    shim_log("native windows: %d, active %u", count, active_id);
}

static DWORD WINAPI watch_thread(void *arg)
{
    FILETIME last = { 0 };
    for (;;) {
        WIN32_FILE_ATTRIBUTE_DATA attr;
        if (GetFileAttributesExW(state_path, GetFileExInfoStandard, &attr) &&
            CompareFileTime(&attr.ftLastWriteTime, &last) != 0) {
            last = attr.ftLastWriteTime;
            load_state();
        }
        Sleep(250);
    }
    return 0;
}

void native_init(void)
{
    InitializeCriticalSection(&lock);
    if (!GetEnvironmentVariableW(L"WE_LINUX_STATE", state_path, MAX_PATH)) {
        shim_log("WE_LINUX_STATE not set; native windows are invisible to Wallpaper Engine");
        return;
    }
    CloseHandle(CreateThread(NULL, 0, watch_thread, NULL, 0, NULL));
}

BOOL native_enum(WNDENUMPROC proc, LPARAM lp)
{
    HWND list[MAX_NATIVE];
    EnterCriticalSection(&lock);
    int count = window_count;
    for (int i = 0; i < count; i++) list[i] = windows[i].hwnd;
    LeaveCriticalSection(&lock);

    for (int i = 0; i < count; i++)
        if (!proc(list[i], lp)) return FALSE;
    return TRUE;
}

/* ---- hooks -------------------------------------------------------------- */

static LONG WINAPI hook_GetWindowLongW(HWND hwnd, int idx)
{
    struct native_window nw;
    if (!lookup(hwnd, &nw)) return GetWindowLongW(hwnd, idx);
    switch (idx) {
    case GWL_STYLE:
        return WS_VISIBLE | WS_CLIPSIBLINGS | (nw.fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW) |
               (nw.maximized ? WS_MAXIMIZE : 0);
    case GWL_EXSTYLE:
        return WS_EX_APPWINDOW;
    default:
        return 0;
    }
}

static BOOL WINAPI hook_IsIconic(HWND hwnd)
{
    struct native_window nw;
    return lookup(hwnd, &nw) ? FALSE : IsIconic(hwnd);  /* minimized windows aren't reported */
}

static BOOL WINAPI hook_IsWindowVisible(HWND hwnd)
{
    struct native_window nw;
    return lookup(hwnd, &nw) ? TRUE : IsWindowVisible(hwnd);
}

static BOOL WINAPI hook_IsWindow(HWND hwnd)
{
    struct native_window nw;
    return lookup(hwnd, &nw) ? TRUE : IsWindow(hwnd);
}

static BOOL WINAPI hook_GetWindowRect(HWND hwnd, RECT *rect)
{
    struct native_window nw;
    if (!lookup(hwnd, &nw)) return GetWindowRect(hwnd, rect);
    *rect = nw.rect;
    return TRUE;
}

static HMONITOR WINAPI hook_MonitorFromWindow(HWND hwnd, DWORD flags)
{
    struct native_window nw;
    if (!lookup(hwnd, &nw)) return MonitorFromWindow(hwnd, flags);
    return MonitorFromRect(&nw.rect, flags);
}

static BOOL WINAPI hook_GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT *wp)
{
    struct native_window nw;
    if (!lookup(hwnd, &nw)) return GetWindowPlacement(hwnd, wp);
    wp->flags = 0;
    wp->showCmd = nw.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    wp->ptMinPosition.x = wp->ptMinPosition.y = -1;
    wp->ptMaxPosition.x = wp->ptMaxPosition.y = -1;
    wp->rcNormalPosition = nw.rect;
    return TRUE;
}

static DWORD WINAPI hook_GetWindowThreadProcessId(HWND hwnd, DWORD *pid)
{
    struct native_window nw;
    if (!lookup(hwnd, &nw)) return GetWindowThreadProcessId(hwnd, pid);
    if (pid) *pid = NATIVE_PID;
    return NATIVE_TID;
}

static int WINAPI hook_GetClassNameW(HWND hwnd, LPWSTR buf, int size)
{
    struct native_window nw;
    if (!lookup(hwnd, &nw)) return GetClassNameW(hwnd, buf, size);
    if (size <= 0) return 0;
    lstrcpynW(buf, NATIVE_CLASS, size);
    return lstrlenW(buf);
}

static HWND WINAPI hook_GetAncestor(HWND hwnd, UINT flags)
{
    struct native_window nw;
    return lookup(hwnd, &nw) ? hwnd : GetAncestor(hwnd, flags);  /* virtual windows are top-level */
}

/* Wine only knows about focus among its own windows; a focused native window wins. */
static HWND WINAPI hook_GetForegroundWindow(void)
{
    EnterCriticalSection(&lock);
    HWND r = active;
    LeaveCriticalSection(&lock);
    return r ? r : GetForegroundWindow();
}

static HRESULT WINAPI hook_DwmGetWindowAttribute(HWND hwnd, DWORD attr, void *value, DWORD size)
{
    struct native_window nw;
    if (!lookup(hwnd, &nw)) return DwmGetWindowAttribute(hwnd, attr, value, size);
    if (attr == DWMWA_CLOAKED && size >= sizeof(DWORD)) {
        *(DWORD *)value = 0;
        return S_OK;
    }
    if (attr == DWMWA_EXTENDED_FRAME_BOUNDS && size >= sizeof(RECT)) {
        *(RECT *)value = nw.rect;
        return S_OK;
    }
    return E_INVALIDARG;
}

const struct hook native_hooks[] = {
    { "user32.dll", "GetWindowLongW",           hook_GetWindowLongW },
    { "user32.dll", "IsIconic",                 hook_IsIconic },
    { "user32.dll", "IsWindowVisible",          hook_IsWindowVisible },
    { "user32.dll", "IsWindow",                 hook_IsWindow },
    { "user32.dll", "GetWindowRect",            hook_GetWindowRect },
    { "user32.dll", "MonitorFromWindow",        hook_MonitorFromWindow },
    { "user32.dll", "GetWindowPlacement",       hook_GetWindowPlacement },
    { "user32.dll", "GetWindowThreadProcessId", hook_GetWindowThreadProcessId },
    { "user32.dll", "GetClassNameW",            hook_GetClassNameW },
    { "user32.dll", "GetAncestor",              hook_GetAncestor },
    { "user32.dll", "GetForegroundWindow",      hook_GetForegroundWindow },
    { "dwmapi.dll", "DwmGetWindowAttribute",    hook_DwmGetWindowAttribute },
};
const size_t native_hook_count = sizeof(native_hooks) / sizeof(native_hooks[0]);
