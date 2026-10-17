// Companion to the forced window rule that we-linux installs for Wallpaper Engine's desktop
// window (created by weshim.dll). The rule handles stacking, borders and geometry; this script
// puts the window on every virtual desktop and, as a safety net, minimizes it if it ever ends
// up fullscreen or not kept below, since it would then cover every other window.

function isWeDesktop(w) {
    return w.resourceClass == "steam_app_431960" && w.caption == "Wallpaper Engine Desktop";
}

function check(w) {
    const ok = !w.fullScreen && w.keepBelow;
    print("we-desktop: fullScreen=" + w.fullScreen + " keepBelow=" + w.keepBelow + " minimized=" + w.minimized +
          " geometry=" + w.frameGeometry.x + "," + w.frameGeometry.y + " " + w.frameGeometry.width + "x" + w.frameGeometry.height);
    if (!ok && !w.minimized) {
        print("we-desktop: window would cover other windows; minimizing it");
        w.minimized = true;
    }
}

function track(w) {
    if (!isWeDesktop(w)) return;
    w.onAllDesktops = true;
    check(w);
    w.fullScreenChanged.connect(function () { check(w); });
    w.keepBelowChanged.connect(function () { check(w); });
}

workspace.windowList().forEach(track);
workspace.windowAdded.connect(track);
