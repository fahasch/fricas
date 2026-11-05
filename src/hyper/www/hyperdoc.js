/*
 * hyperdoc.js: optional; the pages work without it.
 *
 * After a click that keeps the page (show/hide of an example, a command)
 * the server answers with the page address plus the place ("#paste<k>",
 * "#l<id>"), and the browser shows that place near the top.  The
 * script keeps it where it was on the screen, as X11 does.
 *
 * A link that opens
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

const htPasteKey = "hyperdoc-place";

document.addEventListener("click", function (event) {
    const link = event.target.closest(".ht-active");
    if (!link || !link.id)
        return;
    /* a show/hide button: the place is the anchor in front of it */
    const before = link.previousElementSibling;
    const place = link.classList.contains("ht-pastebutton") && before
        && before.classList.contains("ht-anchor") ? before : link;
    try {
        sessionStorage.setItem(htPasteKey, JSON.stringify(
            {id: place.id, top: place.getBoundingClientRect().top}));
    } catch (e) {
        /* without storage the browser's place is kept */
    }
});

window.addEventListener("load", function () {
    let saved = null;
    try {
        saved = JSON.parse(sessionStorage.getItem(htPasteKey));
        sessionStorage.removeItem(htPasteKey);
    } catch (e) {
        return;
    }
    if (!saved || location.hash !== "#" + saved.id)
        return;
    const anchor = document.getElementById(saved.id);
    const scroll = anchor && anchor.closest(".ht-scroll");
    if (scroll)
        scroll.scrollTop += anchor.getBoundingClientRect().top - saved.top;
});

/*
 * Exit in a window that this script opened closes the window, as in
 * X11 (the server ends the HyperDoc window; without the script the tab
 * shows the main window's page instead).
 */
document.addEventListener("click", function (event) {
    const quit = event.target.closest("a.ht-quit");
    if (!quit || !htPlainClick(event) || !window.name.startsWith("hyperdoc")
        || !window.opener)
        return;
    event.preventDefault();
    fetch(quit.href, {redirect: "manual"}).finally(function () {
        window.close();
    });
});
