# we-linux

Run the Steam copy of [Wallpaper Engine](https://store.steampowered.com/app/431960/) on Linux under
Proton, with its original UI, and show its wallpapers as your desktop background.

You need to own and install Wallpaper Engine through Steam. This project ships no Wallpaper Engine
files and changes none on disk; it only adjusts how the program behaves while it runs.

Currently supported: Arch Linux with KDE Plasma 6 on Wayland, using Proton Experimental. (Plasma on
X11 may work but hasn't been tested.)

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

`weshim.dll` also covers a few places where Windows and Proton behave differently:

- **Mouse input.** Wallpaper Engine makes the desktop click-through so clicks reach the icons in
  front of it. There are no icons here, so the shim keeps the wallpaper receiving the mouse, and
  keeps the cursor visible (Wallpaper Engine hides it, since on Windows the icons provide one).
- **Video colors.** Wallpaper Engine asks Media Foundation for BGRA frames but draws them into RGBA
  textures; Windows converts between the two, Proton doesn't, which swaps red and blue. The shim
  requests the format Wallpaper Engine actually draws into.

## Requirements

- Arch Linux with KDE Plasma 6 (Wayland session).
- Steam, with Wallpaper Engine installed.

## Install

1. Install [`we-linux-git`](https://aur.archlinux.org/packages/we-linux-git) from the AUR, for
   example with an AUR helper:

   ```sh
   yay -S we-linux-git
   ```

   Or build it yourself (see [Building from source](#building-from-source) below).

2. In Steam, right-click Wallpaper Engine → **Properties**:
   - **Compatibility:** tick "Force the use of a specific Steam Play compatibility tool" and choose
     **Proton Experimental**.
   - **General → Launch options:**

     ```
     /usr/bin/we-linux %command%
     ```

3. Press **Play**. If Steam asks which version to launch, either choice is fine: `we-linux` always
   starts the 64-bit build.

On the first launch Wallpaper Engine shows its welcome dialog; go through it, then pick a wallpaper
as you would on Windows. After that, Wallpaper Engine starts with your last wallpapers. Quit it
from its tray icon.

### Building from source

```sh
sudo pacman -S --needed base-devel git mingw-w64-gcc xorg-xwininfo xorg-xprop
git clone https://github.com/zHappySnake/we-linux.git
cd we-linux
make
sudo make install
```

This installs to `/usr/local`, so the launch option becomes `/usr/local/bin/we-linux %command%`.
Pass `PREFIX=/usr` to `make install` to install to `/usr` instead.

To work on we-linux itself, skip `make install` and point the launch option at `bin/we-linux` in
your checkout; it uses the files in `build/` directly.

### What `we-linux` changes on your system

- Adds a window rule, "Wallpaper Engine desktop (we-linux)", to `~/.config/kwinrulesrc`.
- Loads a KWin script (`we-desktop.js`) for the current session.
- Keeps a copy of its Windows-side files and the list of on-screen windows for Wallpaper Engine in
  `~/.cache/we-linux/`. (Proton runs in Steam's runtime container, which can't see `/usr` but can
  see your home directory.)

It never modifies Wallpaper Engine's installation.

## Troubleshooting

- **Logs.** Put `WE_LINUX_DEBUG=1` in front of the launch option
  (`WE_LINUX_DEBUG=1 /usr/bin/we-linux %command%`) to write Proton's log and the shim's log
  to `~/.local/state/we-linux/logs/`. Remove it again afterwards; it slows Wine down.
- **Wallpaper Engine stopped right after starting.** As a safety net, `we-linux` stops Wallpaper
  Engine if its wallpaper window ever ends up in front of other windows instead of behind them, and
  the KWin script minimizes it. The reason is in the log and in `journalctl --user -t kwin_wayland`.
- **"Safe Start" dialog.** Wallpaper Engine shows this after it was stopped without quitting (for
  example with Steam's Stop button). Press OK to restore your wallpapers.
- **Running outside Steam.** For development, `we-linux` with no arguments starts Wallpaper Engine
  through Proton Experimental itself. Steam must be running.

## Known limitations

- **Clone mode** (one wallpaper mirrored to several monitors) shows nothing on the extra monitors:
  it relies on DirectComposition, which Proton doesn't implement. Choose the wallpaper on each
  monitor individually, or stretch it across them.
- **Playback speed** has no effect on video wallpapers: Proton's media engine accepts the setting
  but doesn't apply it.

## Uninstall

1. Remove the launch option in Steam (and the forced Proton version, if you like).
2. Delete the "Wallpaper Engine desktop (we-linux)" rule in System Settings → Window Management →
   Window Rules.
3. Remove the program (`sudo pacman -R we-linux-git`, or `sudo make uninstall` in the source
   directory if you built it yourself) and its cache and logs:

   ```sh
   rm -rf ~/.cache/we-linux ~/.local/state/we-linux
   ```

The KWin script goes away when you log out.

## License

we-linux is free software, licensed under the GNU General Public License, version 3 or (at your
option) any later version. See [LICENSE](LICENSE).

Wallpaper Engine is proprietary software by Wallpaper Engine Team and is not covered by this
license; you need your own copy from Steam.
