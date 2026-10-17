#ifndef WESHIM_H
#define WESHIM_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* An import-table hook: calls from the main executable to dll!name go to fn. */
struct hook { const char *dll; const char *name; void *fn; };

/* weshim.c */
void shim_log(const char *fmt, ...);
const char *describe(HWND hwnd);

/* native.c: native Linux windows, reported by KWin, presented to Wallpaper Engine as virtual windows. */
extern const struct hook native_hooks[];
extern const size_t native_hook_count;
void native_init(void);
BOOL native_enum(WNDENUMPROC proc, LPARAM lp);

/* media.c: Wallpaper Engine's video playback through Media Foundation. */
extern const struct hook media_hooks[];
extern const size_t media_hook_count;

#endif
