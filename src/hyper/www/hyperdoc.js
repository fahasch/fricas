/*
 * hyperdoc.js: optional; the pages work without it.  A link that opens
 * a new HyperDoc window (\windowlink, ...; target or formtarget
 * "_blank") gets a window of the X11 size instead of a tab: the size
 * of the window it comes from, 40 pixels down and right (set_size_hints
 * in initx.c); from a window that HyperDoc did not open, the size of
 * the first X11 window, 700 x 450.
 */

"use strict";

function htWindowFeatures() {
    const own = window.name.startsWith("hyperdoc");
    const width = own ? window.innerWidth : 700;
    const height = own ? window.innerHeight : 450;
    return "popup,width=" + width + ",height=" + height
        + ",left=" + (window.screenX + 40) + ",top=" + (window.screenY + 40);
}

let htWindowCount = 0;

function htNewWindow() {
    const name = "hyperdoc" + Date.now() + "-" + (++htWindowCount);
    return window.open("", name, htWindowFeatures()) ? name : null;
}

/* a plain click only; with a modifier the browser decides */
function htPlainClick(event) {
    return event.button === 0 && !event.ctrlKey && !event.metaKey
        && !event.shiftKey && !event.altKey;
}

document.addEventListener("click", function (event) {
    const link = event.target.closest("a[target=_blank]");
    if (!link || !htPlainClick(event))
        return;
    const name = htNewWindow();
    if (name) {
        link.target = name;
        setTimeout(function () { link.target = "_blank"; });
    }
});

document.addEventListener("submit", function (event) {
    const button = event.submitter;
    if (!button || button.formTarget !== "_blank")
        return;
    const name = htNewWindow();
    if (name) {
        button.formTarget = name;
        setTimeout(function () { button.formTarget = "_blank"; });
    }
});
