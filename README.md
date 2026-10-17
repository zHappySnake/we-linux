# wallpaperengine-linux

Run the Steam copy of [Wallpaper Engine](https://store.steampowered.com/app/431960/) on Linux under
Proton, with its original UI, and show its wallpapers as your desktop background.

You need to own and install Wallpaper Engine through Steam. This project ships no Wallpaper Engine
files and changes none on disk; it only adjusts how the program behaves while it runs.

Currently supported: Arch Linux, KDE Plasma 6 (Wayland or X11).

## How this differs from other Linux options

Projects such as [linux-wallpaperengine](https://github.com/Almamu/linux-wallpaperengine) and the
[Wallpaper Engine KDE plugin](https://github.com/catsout/wallpaper-engine-kde-plugin) reimplement
Wallpaper Engine's renderer natively and use their own interface (a command line or Plasma's
wallpaper settings). How well a wallpaper works with them depends on how much of Wallpaper Engine
they have reproduced.

This project takes the opposite approach: you launch the real Wallpaper Engine from Steam, and
everything you see and use is the original program.

- **The original UI.** Browsing, the Workshop, playlists, wallpaper settings, per-monitor setup and
  the editor are Wallpaper Engine's own app, exactly as on Windows.
- **The original renderer.** Wallpapers are drawn by Wallpaper Engine itself (Direct3D 11 through
  Proton's DXVK), so scene, video, web and application wallpapers behave as they do on Windows.
- **Launched through Steam.** Ownership, updates and the Workshop all go through Steam as usual.

The only piece that is replaced is the Windows desktop integration: the Explorer windows that
Wallpaper Engine attaches its wallpaper to, which Wine doesn't provide, and the job of keeping that
wallpaper behind all your other windows, which KWin now does.

## The trade-off: no desktop icons or widgets

On Windows, Wallpaper Engine draws behind Explorer's desktop icons. Linux desktops have no such
layer to slot into, so here the wallpaper is a borderless window covering all monitors and kept below
every other window. It also sits on top of Plasma's own desktop, so:

- desktop icons and desktop widgets are hidden behind the wallpaper;
- clicking or right-clicking the empty desktop reaches the wallpaper, not Plasma's desktop menu.

Panels, docks and all application windows are unaffected and stay on top.

## How it works

Under Proton, Wallpaper Engine runs fine but has no Windows desktop to attach its wallpaper to: it
looks for Explorer's `Progman` / `WorkerW` windows, which Wine doesn't have.

- `welaunch.exe` starts `wallpaper64.exe` suspended and loads `weshim.dll` into it.
- `weshim.dll` creates the window layout Windows 10 has behind the desktop icons. Wallpaper Engine
  finds it and parents its wallpaper into the host `WorkerW`, exactly as on Windows. That host is a
  borderless window spanning every monitor.
- `we-linux` installs a forced KWin window rule (`we-linux-desktop`) that keeps the host below all
  other windows, plus a KWin script that puts it on every virtual desktop.
- Wallpaper Engine pauses or stops wallpapers when another application is focused, maximized or
  fullscreen, but under Wine it can only see Wine's own windows. The KWin script reports the native
  windows on screen to `we-linux-agent`, and `weshim.dll` presents them to Wallpaper Engine as
  ordinary windows, so its own playback settings apply to your Linux apps too.

## Build

```sh
sudo pacman -S --needed base-devel mingw-w64-gcc xorg-xwininfo xorg-xprop
make
```

## Use

In Steam, open Wallpaper Engine → Properties:

- Compatibility: force Proton (Proton Experimental is tested).
- Launch options: `/path/to/wallpaperengine-linux/bin/we-linux %command%`

Or run `bin/we-linux` directly while Steam is running.

Set `WE_LINUX_DEBUG=1` to write Proton and shim logs to `logs/`.

## Uninstall

Remove the launch option, then delete the "Wallpaper Engine desktop (we-linux)" rule in
System Settings → Window Management → Window Rules.
