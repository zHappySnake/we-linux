/*
 * welaunch.exe <shim.dll> <target.exe> [args...]
 *
 * Starts the target suspended, loads the shim DLL into it, then lets it run.
 * Waits for the target to exit so Steam keeps tracking the game as running.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

/* Skip one command-line token, honouring the quoting rules CommandLineToArgvW uses. */
static const wchar_t *skip_arg(const wchar_t *p)
{
    BOOL quoted = FALSE;
    while (*p == L' ' || *p == L'\t') p++;
    for (; *p; p++) {
        if (*p == L'"') quoted = !quoted;
        else if (!quoted && (*p == L' ' || *p == L'\t')) break;
    }
    while (*p == L' ' || *p == L'\t') p++;
    return p;
}

static int fail(const char *what)
{
    fprintf(stderr, "welaunch: %s failed (error %lu)\n", what, GetLastError());
    return 1;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: welaunch.exe <shim.dll> <target.exe> [args...]\n");
        return 2;
    }

    wchar_t dll[MAX_PATH];
    if (!GetFullPathNameW(argv[1], MAX_PATH, dll, NULL)) return fail("GetFullPathNameW");

    /* Everything after the DLL argument is the target command line, passed through verbatim. */
    const wchar_t *target = skip_arg(skip_arg(GetCommandLineW()));
    wchar_t *cmdline = _wcsdup(target);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
        return fail("CreateProcessW");

    SIZE_T size = (wcslen(dll) + 1) * sizeof(wchar_t);
    void *remote = VirtualAllocEx(pi.hProcess, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(pi.hProcess, remote, dll, size, NULL)) {
        TerminateProcess(pi.hProcess, 1);
        return fail("writing DLL path into target");
    }

    LPTHREAD_START_ROUTINE load = (LPTHREAD_START_ROUTINE)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE thread = CreateRemoteThread(pi.hProcess, NULL, 0, load, remote, 0, NULL);
    if (!thread) {
        TerminateProcess(pi.hProcess, 1);
        return fail("CreateRemoteThread");
    }
    WaitForSingleObject(thread, INFINITE);
    DWORD loaded = 0;
    GetExitCodeThread(thread, &loaded);
    CloseHandle(thread);
    VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);

    if (!loaded) {
        fwprintf(stderr, L"welaunch: %ls did not load into the target\n", dll);
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}
