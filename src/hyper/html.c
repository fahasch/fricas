/*
 * html.c: the browser variant of HyperDoc (configure option html)
 * shows a page by writing it as HTML.  The writer walks the node tree
 * like show_text (show-types.c) and follows the layout rules of
 * compute_text_extent (extent1.c); the HTML structure is described in
 * planning/html-structure.md.  The file also defines the functions
 * that the shared files take from the X11 files display.c,
 * scrollbar.c, ReadBitmap.c and initx.c.
 */

#include "fricas_c_macros.h"
#include "debug.h"

#include <stdarg.h>

#include "hyper.h"
#include "html.h"

#include "all_hyper_proto.H1"
#include "html.H1"

/* growing strings */

void
hb_addn(HtmlBuf *b, const char *s, int n)
{
    if (b->len + n + 1 > b->size)
        b->data = resizeBuffer(2 * (b->len + n + 1), b->data, &b->size);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void
hb_add(HtmlBuf *b, const char *s)
{
    hb_addn(b, s, strlen(s));
}

void
hb_printf(HtmlBuf *b, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n >= (int) sizeof(buf))
        n = sizeof(buf) - 1;
    if (n > 0)
        hb_addn(b, buf, n);
}

/*
 * Add text with the characters that are special in HTML escaped;
 * bytes from 0x80 are read as Latin-1 and written as UTF-8.
 */

void
hb_add_escaped(HtmlBuf *b, const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char) *s;

        switch (c) {
          case '&': hb_add(b, "&amp;"); break;
          case '<': hb_add(b, "&lt;"); break;
          case '>': hb_add(b, "&gt;"); break;
          case '"': hb_add(b, "&quot;"); break;
          case '\'': hb_add(b, "&#39;"); break;
          default:
            if (c < 0x80)
                hb_addn(b, s, 1);
            else {
                char u[2];

                u[0] = (char) (0xc0 | (c >> 6));
                u[1] = (char) (0x80 | (c & 0x3f));
                hb_addn(b, u, 2);
            }
            break;
        }
    }
}

void
hb_free(HtmlBuf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->size = 0;
}

/*
 * The writer.  X11 keeps the indent (left margin of every line) and the
 * start of the current line; a line ends at \newline, \par, \item, ...
 * only if it has text (gInLine).  Here every line is a block
 *   <div class="ht-line" style="--ht-ind:..;--ht-first:..">
 * whose text wraps at the indent; the first line starts at --ht-first
 * relative to the indent.  Lengths are kept as character widths (ch)
 * plus pixels, as in X11 (\tab{n}: n character widths; \par: 30 px).
 */

typedef struct {
    double ch;
    int px;
} Length;

#define MaxInline 64

/* an open inline element: a font span, a link, a box, or a group mark */

typedef struct {
    char open[160];             /* the start tag, "" for a group mark */
    const char *close;          /* the end tag */
    int kind;                   /* one of the Inline* below */
    int font;                   /* the font for the text inside */
} InlineItem;

#define InlineGroup 0
#define InlineFont  1
#define InlineLink  2
#define InlineBox   3

#define FontRm 0
#define FontBf 1
#define FontEm 2
#define FontTt 3

static const char *font_class[] = { "ht-rm", "ht-bf", "ht-em", "ht-tt" };

typedef struct item_level {
    Length indent, item_indent, item_space;
    int in_item;
    struct item_level *next;
} ItemLevel;

static struct {
    HtmlBuf *out;
    int in_block;               /* a ht-line div is open */
    int in_line;                /* text on the current line (gInLine) */
    int center;                 /* inside \centerline */
    int in_table;               /* inside a table entry: no blocks */
    int in_item;                /* inside \beginitems (gInItem) */
    int form;                   /* the page has input fields or boxes */
    Length indent;              /* current indent */
    Length block_indent;        /* the indent of the open ht-line div */
    Length block_first;         /* and the start of its first line */
    Length wrap_indent;         /* the indent at its last word */
    int style_start, style_end; /* its style attribute in out */
    int seg_start;              /* where the text since the last tab starts */
    Length seg_x;               /* and its column */
    Length first;               /* start of the next line, relative to indent */
    Length item_indent, item_space;
    ItemLevel *items;
    InlineItem stack[MaxInline];
    int depth;
} w;

static void write_nodes(TextNode *node, int ender);

static void
add_length_vars(const char *name, Length l)
{
    hb_printf(w.out, "--ht-%s:%g;--ht-%spx:%d;", name, l.ch, name, l.px);
}

static int
current_font(void)
{
    return w.depth > 0 ? w.stack[w.depth - 1].font : FontRm;
}

/* close the open inline elements, innermost first */

static void
close_inlines(void)
{
    int i;

    for (i = w.depth - 1; i >= 0; i--)
        hb_add(w.out, w.stack[i].close);
}

static void
reopen_inlines(void)
{
    int i;

    for (i = 0; i < w.depth; i++)
        hb_add(w.out, w.stack[i].open);
}

/*
 * X11 wraps a line at the indent of the moment (compute_word_extent);
 * a page may change it in the middle of a line (a label, then
 * \indentrel and text).  The div then gets the indent at its last
 * word, its first line keeps its start.
 */

static void
set_wrap_indent(void)
{
    char *tail;
    Length first;

    if (w.wrap_indent.ch == w.block_indent.ch && w.wrap_indent.px == w.block_indent.px)
        return;
    first.ch = w.block_indent.ch + w.block_first.ch - w.wrap_indent.ch;
    first.px = w.block_indent.px + w.block_first.px - w.wrap_indent.px;
    tail = alloc_string(w.out->data + w.style_end);
    w.out->len = w.style_start;
    w.out->data[w.out->len] = '\0';
    add_length_vars("ind", w.wrap_indent);
    add_length_vars("first", first);
    hb_add(w.out, tail);
    free(tail);
}

static void
close_block(void)
{
    if (w.in_block) {
        set_wrap_indent();
        close_inlines();
        hb_add(w.out, "</div>\n");
        w.in_block = 0;
    }
    w.in_line = 0;
}

/* make sure a block is open for text: the start of a new line */

static void
need_block(void)
{
    if (w.in_block || w.in_table)
        return;
    hb_add(w.out, w.center ? "<div class=\"ht-line ht-center\" style=\""
                           : "<div class=\"ht-line\" style=\"");
    w.style_start = w.out->len;
    add_length_vars("ind", w.indent);
    add_length_vars("first", w.first);
    w.style_end = w.out->len;
    hb_add(w.out, "\">");
    w.seg_start = w.out->len;
    w.seg_x.ch = w.indent.ch + w.first.ch;
    w.seg_x.px = w.indent.px + w.first.px;
    reopen_inlines();
    w.in_block = 1;
    w.block_indent = w.indent;
    w.block_first = w.first;
    w.wrap_indent = w.indent;
    w.first.ch = 0;
    w.first.px = 0;
}

/*
 * A \tab{n} in the middle of a line: X11 moves to the column x (also
 * back to the left).  The text since the start of the line or the last
 * tab becomes a segment of the width that reaches x; longer text runs
 * over, as in X11.
 */

static void
tab_in_line(Length x)
{
    char open[128];
    char *tail;

    close_inlines();
    /* a space for browsers without CSS; at the end of the segment it takes no room */
    hb_add(w.out, " </span>");
    snprintf(open, sizeof(open),
             "<span class=\"ht-seg\" style=\"--ht-w:%g;--ht-wpx:%d\">",
             x.ch - w.seg_x.ch, x.px - w.seg_x.px);
    tail = alloc_string(w.out->data + w.seg_start);
    w.out->len = w.seg_start;
    w.out->data[w.out->len] = '\0';
    hb_add(w.out, open);
    hb_add(w.out, tail);
    free(tail);
    w.seg_start = w.out->len;
    w.seg_x = x;
    reopen_inlines();
}

/* start_newline of extent2.c: end the line if it has text */

static void
new_line(void)
{
    if (w.in_table) {
        if (w.in_line)
            hb_add(w.out, "<br>");
        w.in_line = 0;
        return;
    }
    if (w.in_line)
        close_block();
}

static void
push_inline(int kind, int font, const char *open, const char *close)
{
    InlineItem *it;

    if (w.depth == MaxInline)
        return;
    it = &w.stack[w.depth++];
    snprintf(it->open, sizeof(it->open), "%s", open);
    it->close = close;
    it->kind = kind;
    it->font = font;
    if (w.in_block || w.in_table)
        hb_add(w.out, open);
}

static void
pop_inline(void)
{
    if (w.depth == 0)
        return;
    w.depth--;
    if (w.in_block || w.in_table)
        hb_add(w.out, w.stack[w.depth].close);
}

/* end a group: close everything back to its mark (pop_group_stack) */

static void
end_group(void)
{
    while (w.depth > 0 && w.stack[w.depth - 1].kind != InlineGroup)
        pop_inline();
    pop_inline();
}

/* end a link or box: close back to the innermost element of this kind */

static void
end_inline(int kind)
{
    int i;

    for (i = w.depth - 1; i >= 0 && w.stack[i].kind != kind; i--)
        ;
    if (i < 0)
        return;
    while (w.depth > i)
        pop_inline();
}

static void
set_font(int font)
{
    char open[64];

    snprintf(open, sizeof(open), "<span class=\"%s\">", font_class[font]);
    push_inline(InlineFont, font, open, "</span>");
}

static void
push_group(void)
{
    push_inline(InlineGroup, current_font(), "", "");
}

/* a word of text; space is the space flag of its node */

static void
write_word(const char *text, int space, const char *cls)
{
    need_block();
    if (w.in_line && space)
        hb_add(w.out, " ");
    if (cls) {
        hb_printf(w.out, "<span class=\"%s\">", cls);
        hb_add_escaped(w.out, text);
        hb_add(w.out, "</span>");
    }
    else
        hb_add_escaped(w.out, text);
    w.in_line = 1;
    w.wrap_indent = w.indent;
}

static void
write_dash(TextNode *node)
{
    int n = strlen(node->data.text);

    need_block();
    if (w.in_line && node->space)
        hb_add(w.out, " ");
    hb_add(w.out, n == 1 ? "-" : (n == 2 ? "&ndash;" : "&mdash;"));
    w.in_line = 1;
}

/* the text width of the nodes up to ender, in characters (text_width) */

static int
text_chars(TextNode *node, int ender)
{
    int n = 0;

    for (; node != NULL && node->type != ender; node = node->next) {
        switch (node->type) {
          case Word:
          case Punctuation:
          case Math:
          case Dash:
          case Lsquarebrace:
          case Rsquarebrace:
          case WindowId:
          case Verbatim:
          case Spadsrctxt:
            if (node->space && n > 0)
                n++;
            n += strlen(node->data.text);
            break;
          case Inputbitmap:
          case Inputpixmap:
          case Controlbitmap:
          case SimpleBox:
          case Radiobox:
            /* a picture: about two characters wide */
            n += 2;
            break;
          case Inputstring:
            /* as input_string_width: one character more than the size */
            n += node->link->reference.string->size + 1;
            break;
          case Endtableitem:
          case Endtable:
            return n;
          default:
            break;
        }
    }
    return n;
}

static int
arg_number(TextNode *node)
{
    return node->data.node != NULL ? atoi(node->data.node->data.text) : 1;
}

static const char *
link_class(int type)
{
    switch (type) {
      case Link: return "ht-link";
      case Downlink: return "ht-downlink";
      case Memolink: return "ht-memolink";
      case Windowlink: return "ht-windowlink";
      case Lisplink: return "ht-lisplink";
      case Lispwindowlink: return "ht-lispwindowlink";
      case LispMemoLink: return "ht-lispmemolink";
      case LispDownLink: return "ht-lispdownlink";
      case Lispcommand: return "ht-lispcommand";
      case Lispcommandquit: return "ht-lispcommandquit";
      case Spadcall: return "ht-spadcall";
      case Spadcallquit: return "ht-spadcallquit";
      case Qspadcall: return "ht-qspadcall";
      case Qspadcallquit: return "ht-qspadcallquit";
      case Spadlink: return "ht-spadlink";
      case Spaddownlink: return "ht-spaddownlink";
      case Spadmemolink: return "ht-spadmemolink";
      case Unixlink: return "ht-unixlink";
      case Unixcommand: return "ht-unixcommand";
      case Upbutton: return "ht-upbutton";
      case Returnbutton: return "ht-returnbutton";
      case Helpbutton: return "ht-helpbutton";
      case Spadcommand: return "ht-spadcommand";
      case Spadgraph: return "ht-spadgraph";
      case Spadsrc: return "ht-spadsrc";
      case Pastebutton: return "ht-pastebutton";
      default: return "ht-link";
    }
}

/* the start of an active area (show_link); inactive buttons are text */

static void
start_link(TextNode *node)
{
    char open[160];
    int active = 1, new_window;

    switch (node->type) {
      case Upbutton:
        active = need_up_button;
        break;
      case Returnbutton:
        active = need_return_button;
        break;
      case Helpbutton:
        active = need_help_button;
        break;
    }
    if (!active || node->link == NULL) {
        push_inline(InlineLink, FontRm, "<span class=\"ht-inactive\">", "</span>");
        return;
    }
    need_block();
    if (w.in_line && node->space)
        hb_add(w.out, " ");
    new_window = node->type == Windowlink || node->type == Lispwindowlink;
    /*
     * on a page with input fields a link sends their values along; the
     * button names its link (HTML 2, unlike formaction known to every
     * browser)
     */
    if (w.form)
        snprintf(open, sizeof(open),
                 "<button type=\"submit\" class=\"ht-active %s\" name=\"link\" value=\"%lu\"%s>",
                 link_class(node->type), (unsigned long) node->link->win,
                 new_window ? " formtarget=\"_blank\"" : "");
    else
        snprintf(open, sizeof(open),
                 "<a class=\"ht-active %s\" href=\"link/%lu\"%s>",
                 link_class(node->type), (unsigned long) node->link->win,
                 new_window ? " target=\"_blank\"" : "");
    push_inline(InlineLink, current_font(), open, w.form ? "</button>" : "</a>");
}

static void
write_image(TextNode *node)
{
    int n = html_image_number(node->data.text);

    need_block();
    if (w.in_line && node->space)
        hb_add(w.out, " ");
    if (n >= 0)
        hb_printf(w.out, "<img class=\"ht-image\" src=\"../img/%d.png\" alt=\"\">", n);
    w.in_line = 1;
}

/*
 * An input box or radio box (show_simple_box): a check box or radio
 * button of the form, hidden by the CSS behind the pictures of the
 * two states; a radio box has the group as name.
 */

static void
write_box_image(ImageStruct *image, const char *cls, const char *alt)
{
    int n = image ? html_image_number(image->filename) : -1;

    if (n >= 0)
        hb_printf(w.out, "<img class=\"%s\" src=\"../img/%d.png\" alt=\"%s\">",
                  cls, n, alt);
    else
        hb_printf(w.out, "<span class=\"%s\">%s</span>", cls, alt);
}

static void
write_box(TextNode *node)
{
    InputBox *box = node->link->reference.box;

    need_block();
    if (w.in_line && node->space)
        hb_add(w.out, " ");
    hb_add(w.out, "<label class=\"ht-check\">");
    if (box->rbs != NULL) {
        hb_add(w.out, "<input type=\"radio\" name=\"r:");
        hb_add_escaped(w.out, box->rbs->name);
    }
    else
        hb_add(w.out, "<input type=\"checkbox\" name=\"b");
    hb_add(w.out, "\" value=\"");
    hb_add_escaped(w.out, box->name);
    hb_printf(w.out, "\"%s>", box->picked ? " checked" : "");
    write_box_image(box->selected, "ht-box-on", "[x]");
    write_box_image(box->unselected, "ht-box-off", "[ ]");
    hb_add(w.out, "</label>");
    w.in_line = 1;
}

/*
 * An input field (show_input): its value as print_to_string reads it,
 * the lines joined; a line longer than the field ends in '_' and goes
 * on in the next one (initialize_default).
 */

static void
write_input_value(InputItem *item)
{
    LineStruct *line;

    for (line = item->lines; line != NULL; line = line->next) {
        char save = '\0';

        if (line->len > item->size) {
            save = line->buffer[item->size];
            line->buffer[item->size] = '\0';
        }
        hb_add_escaped(w.out, line->buffer);
        if (save != '\0')
            line->buffer[item->size] = save;
        if (line->len <= item->size && line->next != NULL)
            hb_add(w.out, "\n");
    }
}

static int
has_newline(InputItem *item)
{
    LineStruct *line;

    for (line = item->lines; line != NULL; line = line->next)
        if (line->len <= item->size && line->next != NULL)
            return 1;
    return 0;
}

static void
write_input(TextNode *node)
{
    InputItem *item = node->link->reference.string;

    need_block();
    if (w.in_line && node->space)
        hb_add(w.out, " ");
    if (has_newline(item)) {
        /* a value with several lines (Shift-Return in X11) */
        hb_printf(w.out, "<textarea class=\"ht-input\" rows=\"%d\" cols=\"%d\" name=\"s:",
                  item->num_lines, item->size);
        hb_add_escaped(w.out, item->name);
        hb_add(w.out, "\">");
        write_input_value(item);
        hb_add(w.out, "</textarea>");
    }
    else {
        hb_printf(w.out, "<input type=\"text\" class=\"ht-input\" size=\"%d\" "
                  "style=\"--ht-size:%d\" name=\"s:", item->size, item->size + 1);
        hb_add_escaped(w.out, item->name);
        hb_add(w.out, "\" value=\"");
        write_input_value(item);
        hb_add(w.out, "\">");
    }
    w.in_line = 1;
}

/* a table: entries in columns of equal width, column by column */

static void
write_table(TextNode **nodep)
{
    TextNode *tn;
    int width = 0, cells = 0;

    new_line();
    close_block();
    for (tn = (*nodep)->next; tn->type != Endtable; tn = tn->next) {
        int n = text_chars(tn->next, Endtableitem);

        if (n > width)
            width = n;
        cells++;
        for (; tn->type != Endtableitem; tn = tn->next)
            ;
    }
    hb_printf(w.out, "<div class=\"ht-table\" style=\"--ht-colw:%d;--ht-cells:%d;",
              width, cells);
    add_length_vars("ind", w.indent);
    hb_add(w.out, "\">\n");
    for (tn = (*nodep)->next; tn->type != Endtable; tn = tn->next) {
        hb_add(w.out, "<div class=\"ht-cell\">");
        w.in_table = 1;
        w.in_line = 0;
        reopen_inlines();
        write_nodes(tn, Endtableitem);
        close_inlines();
        w.in_table = 0;
        hb_add(w.out, "</div>\n");
        for (; tn->type != Endtableitem; tn = tn->next)
            ;
    }
    hb_add(w.out, "</div>\n");
    w.in_line = 0;
    w.first.ch = 0;
    w.first.px = 0;
    *nodep = tn;
}

static void
begin_items(TextNode *node)
{
    ItemLevel *l = (ItemLevel *) halloc(sizeof(ItemLevel), "ItemLevel");

    new_line();
    l->indent = w.indent;
    l->item_indent = w.item_indent;
    l->item_space = w.item_space;
    l->in_item = w.in_item;
    l->next = w.items;
    w.items = l;
    w.in_item = 1;
    w.item_indent = w.indent;
    if (node->data.node != NULL) {
        w.item_space.ch = text_chars(node->data.node, Enddescription);
        w.item_space.px = 0;
    }
    else {
        w.item_space.ch = 0;
        w.item_space.px = 30;
    }
    w.indent.ch = w.item_indent.ch + w.item_space.ch;
    w.indent.px = w.item_indent.px + w.item_space.px;
    w.first.ch = 0;
    w.first.px = 0;
}

static void
end_items(void)
{
    ItemLevel *l = w.items;

    if (l != NULL) {
        w.indent = l->indent;
        w.item_indent = l->item_indent;
        w.item_space = l->item_space;
        w.in_item = l->in_item;
        w.items = l->next;
        free(l);
    }
    new_line();
    w.first.ch = 0;
    w.first.px = 0;
}

/* the next line starts at x (absolute): store it relative to the indent */

static void
start_at(Length x)
{
    w.first.ch = x.ch - w.indent.ch;
    w.first.px = x.px - w.indent.px;
}

static void
write_nodes(TextNode *node, int ender)
{
    for (; node != NULL; node = node->next) {
        switch (node->type) {
          case Word:
          case Lsquarebrace:
          case Rsquarebrace:
          case WindowId:
            write_word(node->data.text, node->space, NULL);
            break;
          case Punctuation:
            write_word(node->data.text, node->space & FRONTSPACE, NULL);
            break;
          case Math:
            write_word(node->data.text, node->space, "ht-math");
            break;
          case Verbatim:
            write_word(node->data.text, 0, "ht-verbatim");
            break;
          case Spadsrctxt:
            new_line();
            write_word(node->data.text, 0, "ht-verbatim");
            break;
          case Dash:
            write_dash(node);
            break;
          case Pound:
          case Macro:
            /* the space in front of a macro or parameter is on this node */
            if (node->space && w.in_line && w.in_block)
                hb_add(w.out, " ");
            break;

          case Newline:
          case Endverbatim:
          case Endpaste:
            new_line();
            break;
          case Par:
            new_line();
            w.first.ch = 0;
            w.first.px = w.in_item ? 0 : 30;
            break;
          case Tab: {
            Length x;

            x.ch = w.indent.ch + arg_number(node);
            x.px = w.indent.px;
            if (w.in_table)
                hb_printf(w.out, "<span class=\"ht-tab\" style=\"--ht-col:%d\"></span>",
                          arg_number(node));
            else if (w.in_block)
                tab_in_line(x);
            else
                start_at(x);
            w.in_line = 0;
            break;
          }
          case Indent:
            w.indent.ch = atoi(node->data.node->data.text);
            w.indent.px = 0;
            if (!w.in_line)
                w.first.ch = w.first.px = 0;
            break;
          case Indentrel:
            w.indent.ch += atoi(node->data.node->data.text);
            if (!w.in_line)
                w.first.ch = w.first.px = 0;
            break;
          case Space:
            need_block();
            hb_printf(w.out, "<span class=\"ht-space\" style=\"--ht-n:%d\"></span>",
                      arg_number(node));
            break;
          case HSpace:
            need_block();
            hb_printf(w.out, "<span class=\"ht-hspace\" style=\"--ht-n:%d\"></span>",
                      arg_number(node));
            break;
          case VSpace:
            /*
             * X11 moves the following text down by n pixels plus a line
             * (text_y), without starting a new line: in a line a shifted
             * span (nested shifts add up), at the start of a line space.
             */
            if (w.in_line || w.in_table) {
                char open[96];

                snprintf(open, sizeof(open),
                         "<span class=\"ht-vshift\" style=\"--ht-v:%d\">",
                         arg_number(node));
                push_inline(InlineFont, current_font(), open, "</span>");
            }
            else {
                close_block();
                hb_printf(w.out, "<div class=\"ht-vspace\" style=\"--ht-v:%d\"></div>\n",
                          arg_number(node));
            }
            break;
          case Horizontalline:
            new_line();
            close_block();
            hb_add(w.out, "<hr class=\"ht-rule\">\n");
            break;

          case Center:
            new_line();
            close_block();
            w.center = 1;
            push_group();
            break;
          case Endcenter:
            close_block();
            end_group();
            w.center = 0;
            w.first.ch = w.first.px = 0;
            if (node->type == ender)
                return;
            break;

          case Beginitems:
          case Begintitems:
            begin_items(node);
            break;
          case Enditems:
          case Endtitems:
            end_items();
            if (node->type == ender)
                return;
            break;
          case Item:
          case Mitem:
            new_line();
            start_at(w.item_indent);
            break;
          case Titem:
            new_line();
            w.first.ch = -w.item_space.ch;
            w.first.px = -w.item_space.px;
            break;
          case Description:
            push_group();
            set_font(FontBf);
            break;
          case Enddescription:
            end_group();
            break;

          case Table:
            write_table(&node);
            break;

          case Group:
          case Tableitem:
            /* the space in front of a group is on this node */
            if (node->space && w.in_line && w.in_block)
                hb_add(w.out, " ");
            push_group();
            break;
          case Endgroup:
          case Endtableitem:
            end_group();
            if (node->type == ender)
                return;
            break;
          case BoldFace:
            set_font(FontBf);
            break;
          case Emphasize:
            set_font(current_font() == FontRm ? FontEm : FontRm);
            break;
          case It:
            set_font(FontEm);
            break;
          case Sl:
          case Rm:
            set_font(FontRm);
            break;
          case Tt:
            set_font(FontTt);
            break;

          case Box:
            need_block();
            if (w.in_line && node->space)
                hb_add(w.out, " ");
            push_inline(InlineBox, current_font(), "<span class=\"ht-box\">", "</span>");
            w.in_line = 1;
            break;
          case Mbox:
            need_block();
            if (w.in_line && node->space)
                hb_add(w.out, " ");
            push_inline(InlineBox, current_font(), "<span class=\"ht-mbox\">", "</span>");
            break;
          case Endbox:
          case Endmbox:
            end_inline(InlineBox);
            if (node->type == ender)
                return;
            break;

          case Downlink:
          case Link:
          case LispDownLink:
          case LispMemoLink:
          case Lispcommand:
          case Lispcommandquit:
          case Lisplink:
          case Lispwindowlink:
          case Memolink:
          case Qspadcall:
          case Qspadcallquit:
          case Returnbutton:
          case Spadcall:
          case Spadcallquit:
          case Spaddownlink:
          case Spadlink:
          case Spadmemolink:
          case Unixcommand:
          case Unixlink:
          case Upbutton:
          case Windowlink:
          case Pastebutton:
            start_link(node);
            break;
          case Spadcommand:
          case Spadgraph:
          case Spadsrc:
            start_link(node);
            set_font(FontTt);
            break;
          case Endbutton:
          case Endlink:
          case Endpastebutton:
          case Endspadcommand:
          case Endspadsrc:
            end_inline(InlineLink);
            if (node->type == ender)
                return;
            break;

          case Controlbitmap:
          case Inputbitmap:
          case Inputpixmap:
            write_image(node);
            break;

          case SimpleBox:
          case Radiobox:
            write_box(node);
            break;
          case Inputstring:
            write_input(node);
            break;

          case Ifcond:
            /* choose the branch as compute_ifcond_extent does */
            if (check_condition(node->data.ifnode->cond))
                node->next = node->data.ifnode->thennode;
            else
                node->next = node->data.ifnode->elsenode;
            break;

          case Endscrolling:
          case Endheader:
          case Endfooter:
          case Endtitle:
            return;

          case Endmath:
          case Endparameter:
          case Endinputbox:
          case Endmacro:
          case Endif:
            if (node->type == ender)
                return;
            break;

          default:
            /* Paste, Noop, Fi, Free, Bound, Beep, ... */
            break;
        }
    }
}

/* write one region of the page (title, header, scroll, footer) */

static void
write_region(const char *cls, TextNode *node, int ender)
{
    int start;

    if (node == NULL)
        return;
    hb_printf(w.out, "<div class=\"%s\">\n", cls);
    start = w.out->len;
    while (w.items != NULL)
        end_items();
    w.in_block = w.in_line = w.center = w.in_table = w.in_item = 0;
    w.indent.ch = w.first.ch = w.item_indent.ch = w.item_space.ch = 0;
    w.indent.px = w.first.px = w.item_indent.px = w.item_space.px = 0;
    w.depth = 0;
    push_group();
    write_nodes(node->next, ender);
    close_block();
    w.depth = 0;
    /*
     * X11 frames the scroll region with a rule under a header and over
     * a footer that are not empty (drawScrollLines), unless \nolines
     */
    if (w.out->len > start && !(gWindow->page->page_flags & NOLINES)
        && gWindow->page->scrolling != NULL)
        hb_add(w.out, "<span class=\"ht-frame\"></span>");
    hb_add(w.out, "</div>\n");
}

static void
write_title_button(Window id, const char *cls, const char *label, int active)
{
    if (active && w.form)
        hb_printf(w.out, "<button type=\"submit\" class=\"ht-button %s\" "
                  "name=\"link\" value=\"%lu\">%s</button>",
                  cls, (unsigned long) id, label);
    else if (active)
        hb_printf(w.out, "<a class=\"ht-button %s\" href=\"link/%lu\">%s</a>",
                  cls, (unsigned long) id, label);
    else
        hb_add(w.out, "<span class=\"ht-button ht-noop\"></span>");
    /* for browsers without CSS; the title bar is a flex box */
    hb_add(w.out, "\n");
}

/* the HTML document of the current page of gWindow */

void
html_write_page(HtmlBuf *out)
{
    HyperDocPage *page = gWindow->page;

    w.out = out;
    w.form = page->input_list != NULL || page->box_hash != NULL;
    hb_add(out, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>");
    hb_add_escaped(out, page->name);
    hb_add(out, "</title>\n"
           "<link rel=\"stylesheet\" href=\"../static/hyperdoc.css\">\n"
           "<script src=\"../static/hyperdoc.js\" defer></script>\n"
           "</head>\n<body class=\"ht-window\">\n");
    if (w.form) {
        /*
         * One form around the page: every link is a submit button that
         * sends the values of the fields and boxes.  The page name
         * keeps them off another page; Return in a field only
         * stores them, through the first button.
         */
        hb_add(out, "<form class=\"ht-form\" method=\"post\" action=\"./\">\n"
               "<input type=\"hidden\" name=\"page\" value=\"");
        hb_add_escaped(out, page->name);
        hb_add(out, "\">\n<button type=\"submit\" class=\"ht-default\" "
               "tabindex=\"-1\" aria-hidden=\"true\"></button>\n");
    }
    hb_add(out, "<div class=\"ht-titlebar\">");
    write_title_button(gWindow->fTitleBarButton1, "ht-quit", "Exit", 1);
    write_title_button(gWindow->fTitleBarButton2, "ht-help", "Help", need_help_button);
    write_region("ht-title", page->title, Endtitle);
    write_title_button(gWindow->fTitleBarButton3, "ht-return", "Home", need_return_button);
    write_title_button(gWindow->fTitleBarButton4, "ht-up", "Up", need_up_button);
    hb_add(out, "</div>\n");
    write_region("ht-header", page->header, Endheader);
    write_region("ht-scroll", page->scrolling, Endscrolling);
    write_region("ht-footer", page->footer, Endfooter);
    if (w.form)
        hb_add(out, "</form>\n");
    hb_add(out, "</body>\n</html>\n");
}

void
show_page(HyperDocPage *page)
{
    (void) page;
}

void
paste_page(TextNode *node)
{
    (void) node;
}

/* the scroll bars are part of the HTML page */

void
linkScrollBars(void)
{
}

/* fonts and colours are classes in the HTML, chosen by the writer */

void
change_text(int color, XFontStruct *font)
{
    (void) color;
    (void) font;
}

/*
 * As insert_image_struct in ReadBitmap.c, without reading the file:
 * the browser loads the picture itself.
 */

ImageStruct *
insert_image_struct(char *filename)
{
    ImageStruct *image;

    if (*filename == ' ')
        filename++;
    if ((image = (ImageStruct *) hash_find(&gImageHashTable, filename)) == NULL) {
        image = (ImageStruct *) halloc(sizeof(ImageStruct), "ImageStruct");
        image->image.xi = NULL;
        image->width = 0;
        image->height = 0;
        image->filename = (char *) halloc(sizeof(char) * strlen(filename) + 1,
                                          "insert_image--filename");
        sprintf(image->filename, "%s", filename);
        hash_insert(&gImageHashTable, (char *) image, image->filename);
    }
    return image;
}
