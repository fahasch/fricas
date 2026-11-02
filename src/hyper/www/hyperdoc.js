/*
 * hyperdoc.js: what the HTML of a HyperDoc page cannot do alone.
 * A \tab{n} in the middle of a line moves to column n of the line
 * (X11 sets the absolute position, also back to the left); the
 * span gets the margin that reaches that column from where it stands.
 */

"use strict";

function htPlaceTabs() {
    for (const tab of document.querySelectorAll(".ht-line .ht-tab")) {
        const line = tab.closest(".ht-line");
        const style = getComputedStyle(tab);
        tab.style.marginLeft = "0px";
        const unit = parseFloat(getComputedStyle(line).fontSize) / 2;
        const target = parseFloat(style.getPropertyValue("--ht-col")) * unit;
        const left = tab.getBoundingClientRect().left
            - line.getBoundingClientRect().left
            - parseFloat(getComputedStyle(line).paddingLeft);
        tab.style.marginLeft = (target - left) + "px";
    }
}

/*
 * \table as compute_table_extent (extent1.c): as many columns of the
 * widest entry as fit, then only as many as the entries need, spread
 * over the whole width, filled column by column.
 */
function htLayoutTables() {
    for (const table of document.querySelectorAll(".ht-table")) {
        const cells = table.children.length;
        const style = getComputedStyle(table);
        const colw = parseFloat(style.getPropertyValue("--ht-colw"))
            * parseFloat(style.fontSize) / 2 + 10;
        let cols = Math.max(1, Math.floor(table.clientWidth / colw));
        const lines = Math.ceil(cells / cols);
        cols = Math.max(1, Math.ceil(cells / lines));
        table.style.columnCount = cols;
        table.style.columnWidth = "auto";
    }
}

function htLayout() {
    htLayoutTables();
    htPlaceTabs();
}

document.addEventListener("DOMContentLoaded", htLayout);
window.addEventListener("resize", htLayout);
