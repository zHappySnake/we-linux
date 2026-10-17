// Companion to the forced window rule that we-linux installs for Wallpaper Engine's desktop
// window (created by weshim.dll). The rule handles stacking, borders and geometry; this script:
//
//  - puts the window on every virtual desktop;
//  - minimizes it if it ever ends up fullscreen or not kept below, since it would then cover
//    every other window;
//  - reports the native windows on screen to we-linux-agent, so Wallpaper Engine can apply its
//    own "pause when another app is focused / maximized / fullscreen" rules to them.

const WE_CLASS = "steam_app_431960";
const DESKTOP_TITLE = "Wallpaper Engine Desktop";

let desktopWindow = null;
let ids = {};
let nextId = 1;
let lastReport = null;

function isWeWindow(w) { return w.resourceClass == WE_CLASS; }
function isWeDesktop(w) { return isWeWindow(w) && w.caption == DESKTOP_TITLE; }

function on(obj, signal, fn) {
    if (obj[signal] && obj[signal].connect) obj[signal].connect(fn);
}

// ---- keeping the desktop window in place ----

function checkDesktop(w) {
    if ((!w.fullScreen && w.keepBelow) || w.minimized) return;
    print("we-desktop: window would cover other windows (fullScreen=" + w.fullScreen +
          " keepBelow=" + w.keepBelow + "); minimizing it");
    w.minimized = true;
}

function trackDesktop(w) {
    desktopWindow = w;
    w.onAllDesktops = true;
    checkDesktop(w);
    on(w, "fullScreenChanged", function () { checkDesktop(w); });
    on(w, "keepBelowChanged", function () { checkDesktop(w); });
    on(w, "frameGeometryChanged", report);
    report();
}

// ---- reporting native windows ----

function onCurrentDesktop(w) {
    return w.onAllDesktops || w.desktops.indexOf(workspace.currentDesktop) >= 0;
}

function onCurrentActivity(w) {
    return w.activities.length == 0 || w.activities.indexOf(workspace.currentActivity) >= 0;
}

function shown(w) {
    // Wallpaper Engine sees its own (Wine) windows directly, so only native ones are reported.
    return w.normalWindow && !isWeWindow(w) && !w.minimized && !w.hidden && !w.deleted &&
           onCurrentDesktop(w) && onCurrentActivity(w);
}

function idOf(w) {
    const key = w.internalId.toString();
    if (!ids[key]) ids[key] = nextId++;
    return ids[key];
}

function rect(g) {
    return [g.x, g.y, g.width, g.height].map(Math.round).join(" ");
}

// One line per item, top of the stack first:
//   screen <x> <y> <w> <h>               desktop window geometry, for mapping to Windows coordinates
//   win <id> <x> <y> <w> <h> <flags>     M = maximized, F = fullscreen, - = neither
//   active <id>                          focused native window, 0 if none
function report() {
    if (!desktopWindow) return;
    const lines = ["screen " + rect(desktopWindow.frameGeometry)];
    let active = 0;
    workspace.stackingOrder.slice().reverse().forEach(function (w) {
        if (!shown(w)) return;
        const id = idOf(w);
        const flags = (w.maximizeMode == 3 ? "M" : "") + (w.fullScreen ? "F" : "");
        lines.push("win " + id + " " + rect(w.frameGeometry) + " " + (flags || "-"));
        if (w.active) active = id;
    });
    lines.push("active " + active);

    const text = lines.join("\n");
    if (text == lastReport) return;
    lastReport = text;
    callDBus("dev.welinux.Agent", "/dev/welinux/Agent", "dev.welinux.Agent", "Update", text);
}

function trackWindow(w) {
    if (isWeDesktop(w)) {
        trackDesktop(w);
        return;
    }
    if (isWeWindow(w)) return;
    // Skip the stream of geometry changes during an interactive move/resize; report when it ends.
    const changed = function () { if (!w.move && !w.resize) report(); };
    ["frameGeometryChanged", "minimizedChanged", "fullScreenChanged", "maximizedChanged",
     "desktopsChanged", "activitiesChanged", "hiddenChanged"].forEach(function (s) { on(w, s, changed); });
    on(w, "interactiveMoveResizeFinished", report);
    report();
}

workspace.windowList().forEach(trackWindow);
on(workspace, "windowAdded", trackWindow);
on(workspace, "windowRemoved", function (w) {
    if (w === desktopWindow) desktopWindow = null;
    delete ids[w.internalId.toString()];
    report();
});
["windowActivated", "currentDesktopChanged", "currentActivityChanged", "stackingOrderChanged"]
    .forEach(function (s) { on(workspace, s, report); });
