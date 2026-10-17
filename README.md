# wallpaperengine-linux

Run the Steam copy of [Wallpaper Engine](https://store.steampowered.com/app/431960/) on Linux under
Proton, with its original UI, and show its wallpapers as your desktop background.

You need to own and install Wallpaper Engine through Steam. This project ships no Wallpaper Engine
files and changes none on disk; it only adjusts how the program behaves while it runs.

Currently supported: Arch Linux, KDE Plasma 6 (Wayland or X11).

## How it works

Under Proton, Wallpaper Engine runs fine but has no Windows desktop to attach its wallpaper to: it
looks for Explorer's `Progman` / `WorkerW` windows, which Wine doesn't have.

- `welaunch.exe` starts `wallpaper64.exe` suspended and loads `weshim.dll` into it.
- `weshim.dll` creates the window layout Windows 10 has behind the desktop icons. Wallpaper Engine
  finds it and parents its wallpaper into the host `WorkerW`, exactly as on Windows. That host is a
  borderless window spanning every monitor.
- `we-linux` installs a forced KWin window rule (`we-linux-desktop`) that keeps the host below all
  other windows, plus a small KWin script that puts it on every virtual desktop.

Desktop icons and widgets end up hidden behind the wallpaper.

## Build

```sh
sudo pacman -S --needed mingw-w64-gcc xorg-xwininfo xorg-xprop xorg-xrandr
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
