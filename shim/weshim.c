/*
 * weshim.dll: loaded into wallpaper64.exe by welaunch.exe.
 *
 * Patches the main executable's import table so the calls Wallpaper Engine uses to
 * attach itself to the Windows desktop (Progman/WorkerW) go through us first.
 * This stage only logs them and passes them through.
 *
 * Set WE_LINUX_SHIM_LOG to a directory to get weshim-<pid>.log there.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- logging ------------------------------------------------------------ */

static HANDLE log_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION log_lock;

static void log_open(void)
{
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    InitializeCriticalSection(&log_lock);
    if (!GetEnvironmentVariableW(L"WE_LINUX_SHIM_LOG", dir, MAX_PATH)) return;
    _snwprintf(path, MAX_PATH, L"%ls\\weshim-%lu.log", dir, GetCurrentProcessId());
    log_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void shim_log(const char *fmt, ...)
{
    if (log_file == INVALID_HANDLE_VALUE) return;
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "%8lu [%04lx] ", GetTickCount(), GetCurrentThreadId());
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
    buf[n++] = '\n';
    DWORD written;
    EnterCriticalSection(&log_lock);
    WriteFile(log_file, buf, n, &written, NULL);
    LeaveCriticalSection(&log_lock);
}

/* "hwnd{class "title" pid}" for log lines; uses a small ring of buffers so several fit in one logf. */
static const char *describe(HWND hwnd)
{
    static char ring[8][192];
    static LONG next;
    char *out = ring[InterlockedIncrement(&next) & 7];
    if (!hwnd) return "NULL";
    char cls[64] = "", title[64] = "";
    DWORD pid = 0;
    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowTextA(hwnd, title, sizeof(title));
    GetWindowThreadProcessId(hwnd, &pid);
    snprintf(out, 192, "%p{%s \"%s\" pid=%lu}", (void *)hwnd, cls, title, pid);
    return out;
}

/* Class/window names may be atoms rather than strings. */
static const char *wstr(LPCWSTR s)
{
    static char ring[8][160];
    static LONG next;
    char *out = ring[InterlockedIncrement(&next) & 7];
    if (!s) return "NULL";
    if (IS_INTRESOURCE(s)) snprintf(out, 160, "#%u", (unsigned)(ULONG_PTR)s);
    else WideCharToMultiByte(CP_UTF8, 0, s, -1, out, 160, NULL, NULL);
    return out;
}

/* ---- stand-in Windows desktop --------------------------------------------- */

/*
 * Wine has no Explorer desktop, so we build the Windows 10 layout Wallpaper Engine searches for:
 *
 *   Progman                      hidden
 *   WorkerW  (icon layer)        hidden, holds SHELLDLL_DefView
 *   WorkerW  (wallpaper host)    next WorkerW after the icon layer; WE parents its wallpaper into it
 *
 * The wallpaper host is the one visible window: borderless, spanning every monitor, titled
 * DESKTOP_TITLE so the KWin script can find it and keep it below all other windows.
 */
#define DESKTOP_TITLE L"Wallpaper Engine Desktop"

static HWND shell_progman, shell_iconlayer, shell_defview, shell_host;
static HANDLE shell_ready;

static RECT virtual_screen(void)
{
    RECT r;
    r.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.right = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.bottom = r.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r;
}

static void shell_fit_to_screen(void)
{
    RECT r = virtual_screen();
    int w = r.right - r.left, h = r.bottom - r.top;
    HWND windows[] = { shell_progman, shell_iconlayer, shell_host };
    for (int i = 0; i < 3; i++)
        SetWindowPos(windows[i], NULL, r.left, r.top, w, h, SWP_NOACTIVATE | SWP_NOZORDER);
    shim_log("desktop sized to %ld,%ld %dx%d", r.left, r.top, w, h);
}

static LRESULT CALLBACK shell_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case 0x052C:  /* "spawn WorkerW": ours already exists */
        return 0;
    case WM_DISPLAYCHANGE:
        if (hwnd == shell_host) shell_fit_to_screen();
        break;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_CLOSE:
        return 0;  /* the desktop is not closable */
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD WINAPI shell_thread(void *arg)
{
    HINSTANCE inst = (HINSTANCE)arg;
    static const wchar_t *classes[] = { L"Progman", L"SHELLDLL_DefView", L"WorkerW" };
    for (size_t i = 0; i < 3; i++) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = shell_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = classes[i];
        RegisterClassW(&wc);
    }

    RECT r = virtual_screen();
    int w = r.right - r.left, h = r.bottom - r.top;
    DWORD ex = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    /* New windows go on top of the Z-order, so create the host first to have it follow the icon layer. */
    shell_host = CreateWindowExW(ex, L"WorkerW", DESKTOP_TITLE, WS_POPUP | WS_CLIPCHILDREN,
                                 r.left, r.top, w, h, NULL, NULL, inst, NULL);
    shell_iconlayer = CreateWindowExW(ex, L"WorkerW", L"", WS_POPUP, r.left, r.top, w, h, NULL, NULL, inst, NULL);
    shell_defview = CreateWindowExW(0, L"SHELLDLL_DefView", L"", WS_CHILD, 0, 0, w, h, shell_iconlayer, NULL, inst, NULL);
    shell_progman = CreateWindowExW(ex, L"Progman", L"Program Manager", WS_POPUP, r.left, r.top, w, h, NULL, NULL, inst, NULL);
    ShowWindow(shell_host, SW_SHOWNOACTIVATE);
    shim_log("desktop created at %ld,%ld %dx%d: progman=%s iconlayer=%s host=%s", r.left, r.top, w, h,
             describe(shell_progman), describe(shell_iconlayer), describe(shell_host));
    SetEvent(shell_ready);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}

/* ---- hooks -------------------------------------------------------------- */

static HWND WINAPI hook_FindWindowW(LPCWSTR cls, LPCWSTR name)
{
    /* Don't let the first lookup race the desktop thread. */
    if (cls && !IS_INTRESOURCE(cls) && !_wcsicmp(cls, L"Progman")) WaitForSingleObject(shell_ready, 5000);
    HWND r = FindWindowW(cls, name);
    shim_log("FindWindowW(%s, %s) -> %s", wstr(cls), wstr(name), describe(r));
    return r;
}

static HWND WINAPI hook_FindWindowExW(HWND parent, HWND after, LPCWSTR cls, LPCWSTR name)
{
    HWND r;
    /* "The WorkerW after the icon layer" depends on Z-order; always answer with the host. */
    if (!parent && after && after == shell_iconlayer && cls && !IS_INTRESOURCE(cls) && !_wcsicmp(cls, L"WorkerW"))
        r = shell_host;
    else
        r = FindWindowExW(parent, after, cls, name);
    shim_log("FindWindowExW(%s, %s, %s, %s) -> %s", describe(parent), describe(after), wstr(cls), wstr(name), describe(r));
    return r;
}

static LRESULT WINAPI hook_SendMessageTimeoutW(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT flags, UINT timeout, PDWORD_PTR result)
{
    LRESULT r = SendMessageTimeoutW(hwnd, msg, wp, lp, flags, timeout, result);
    shim_log("SendMessageTimeoutW(%s, 0x%x, 0x%llx, 0x%llx, flags=0x%x, %u) -> %lld",
         describe(hwnd), msg, (unsigned long long)wp, (unsigned long long)lp, flags, timeout, (long long)r);
    return r;
}

struct enum_ctx { WNDENUMPROC proc; LPARAM lp; int visited; };

static BOOL CALLBACK enum_thunk(HWND hwnd, LPARAM lp)
{
    struct enum_ctx *ctx = (struct enum_ctx *)lp;
    ctx->visited++;
    BOOL r = ctx->proc(hwnd, ctx->lp);
    if (!r) shim_log("  EnumWindows callback stopped at %s", describe(hwnd));
    return r;
}

static BOOL WINAPI hook_EnumWindows(WNDENUMPROC proc, LPARAM lp)
{
    struct enum_ctx ctx = { proc, lp, 0 };
    BOOL r = EnumWindows(enum_thunk, (LPARAM)&ctx);
    shim_log("EnumWindows(%p) visited %d -> %d", (void *)proc, ctx.visited, r);
    return r;
}

static BOOL WINAPI hook_EnumChildWindows(HWND parent, WNDENUMPROC proc, LPARAM lp)
{
    struct enum_ctx ctx = { proc, lp, 0 };
    BOOL r = EnumChildWindows(parent, enum_thunk, (LPARAM)&ctx);
    shim_log("EnumChildWindows(%s, %p) visited %d -> %d", describe(parent), (void *)proc, ctx.visited, r);
    return r;
}

static HWND WINAPI hook_SetParent(HWND child, HWND parent)
{
    HWND r = SetParent(child, parent);
    shim_log("SetParent(%s, %s) -> %s", describe(child), describe(parent), describe(r));
    return r;
}

static HWND WINAPI hook_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int h,
                                        HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    HWND r = CreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    shim_log("CreateWindowExW(ex=0x%lx, %s, %s, style=0x%lx, %d,%d %dx%d, parent=%s) -> %s",
         ex, wstr(cls), wstr(name), style, x, y, w, h, describe(parent), describe(r));
    return r;
}

static LONG setpos_count;

static BOOL WINAPI hook_SetWindowPos(HWND hwnd, HWND after, int x, int y, int w, int h, UINT flags)
{
    BOOL r = SetWindowPos(hwnd, after, x, y, w, h, flags);
    if (InterlockedIncrement(&setpos_count) <= 500)
        shim_log("SetWindowPos(%s, after=%p, %d,%d %dx%d, flags=0x%x) -> %d", describe(hwnd), (void *)after, x, y, w, h, flags, r);
    return r;
}

static LONG WINAPI hook_SetWindowLongW(HWND hwnd, int idx, LONG val)
{
    LONG r = SetWindowLongW(hwnd, idx, val);
    shim_log("SetWindowLongW(%s, %d, 0x%lx) -> 0x%lx", describe(hwnd), idx, val, r);
    return r;
}

static LONG_PTR WINAPI hook_SetWindowLongPtrW(HWND hwnd, int idx, LONG_PTR val)
{
    LONG_PTR r = SetWindowLongPtrW(hwnd, idx, val);
    if (idx == GWL_STYLE || idx == GWL_EXSTYLE || idx == GWLP_HWNDPARENT)
        shim_log("SetWindowLongPtrW(%s, %d, 0x%llx) -> 0x%llx", describe(hwnd), idx, (unsigned long long)val, (unsigned long long)r);
    return r;
}

static BOOL WINAPI hook_SystemParametersInfoW(UINT action, UINT uparam, PVOID pv, UINT winini)
{
    BOOL r = SystemParametersInfoW(action, uparam, pv, winini);
    shim_log("SystemParametersInfoW(0x%x, %u, %p, 0x%x) -> %d", action, uparam, pv, winini, r);
    return r;
}

static BOOL WINAPI hook_CreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta,
                                       BOOL inherit, DWORD flags, LPVOID env, LPCWSTR cwd, LPSTARTUPINFOW si,
                                       LPPROCESS_INFORMATION pi)
{
    BOOL r = CreateProcessW(app, cmd, pa, ta, inherit, flags, env, cwd, si, pi);
    shim_log("CreateProcessW(%s, %s) -> %d pid=%lu", wstr(app), wstr(cmd), r, r ? pi->dwProcessId : 0);
    return r;
}

/* ---- import table patching ---------------------------------------------- */

struct hook { const char *dll; const char *name; void *fn; };

static const struct hook hooks[] = {
    { "user32.dll",   "FindWindowW",           hook_FindWindowW },
    { "user32.dll",   "FindWindowExW",         hook_FindWindowExW },
    { "user32.dll",   "SendMessageTimeoutW",   hook_SendMessageTimeoutW },
    { "user32.dll",   "EnumWindows",           hook_EnumWindows },
    { "user32.dll",   "EnumChildWindows",      hook_EnumChildWindows },
    { "user32.dll",   "SetParent",             hook_SetParent },
    { "user32.dll",   "CreateWindowExW",       hook_CreateWindowExW },
    { "user32.dll",   "SetWindowPos",          hook_SetWindowPos },
    { "user32.dll",   "SetWindowLongW",        hook_SetWindowLongW },
#ifdef _WIN64
    { "user32.dll",   "SetWindowLongPtrW",     hook_SetWindowLongPtrW },
#endif
    { "user32.dll",   "SystemParametersInfoW", hook_SystemParametersInfoW },
    { "kernel32.dll", "CreateProcessW",        hook_CreateProcessW },
};

static int patch_imports(HMODULE module)
{
    BYTE *base = (BYTE *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;

    int patched = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); desc->Name; desc++) {
        const char *dll = (const char *)(base + desc->Name);
        if (!desc->OriginalFirstThunk) continue;  /* no name table to match against */
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA *slots = (IMAGE_THUNK_DATA *)(base + desc->FirstThunk);

        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char *fn = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++) {
                if (_stricmp(dll, hooks[i].dll) || strcmp(fn, hooks[i].name)) continue;
                DWORD old;
                VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &old);
                slots->u1.Function = (ULONG_PTR)hooks[i].fn;
                VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), old, &old);
                shim_log("hooked %s!%s", dll, fn);
                patched++;
            }
        }
    }
    return patched;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    log_open();

    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    shim_log("weshim loaded into %s (pid %lu)", exe, GetCurrentProcessId());
    shim_log("patched %d imports", patch_imports(GetModuleHandleW(NULL)));

    /* The thread starts once the loader lock is released. */
    shell_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    CloseHandle(CreateThread(NULL, 0, shell_thread, inst, 0, NULL));
    return TRUE;
}
