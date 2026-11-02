/*
 * html.c: the browser variant of HyperDoc (configure option html)
 * shows a page by writing it as HTML.  It defines the functions that
 * the shared files take from the X11 files display.c, titlebar.c,
 * scrollbar.c, ReadBitmap.c and initx.c.
 */

#include "fricas_c_macros.h"
#include "debug.h"

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

/* add text with the characters that are special in HTML escaped */

void
hb_add_escaped(HtmlBuf *b, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
          case '&': hb_add(b, "&amp;"); break;
          case '<': hb_add(b, "&lt;"); break;
          case '>': hb_add(b, "&gt;"); break;
          case '"': hb_add(b, "&quot;"); break;
          case '\'': hb_add(b, "&#39;"); break;
          default:  hb_addn(b, s, 1); break;
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
 * The HTML document of the page called name.  Only the frame for now;
 * the writer over the node tree is step 4.
 */

void
html_page_response(const char *name, HttpResponse *r)
{
    HyperDocPage *page;

    page = (HyperDocPage *) hash_find(gWindow->fPageHashTable, (char *) name);
    if (page == NULL) {
        r->status = 404;
        return;
    }
    r->status = 200;
    r->content_type = "text/html; charset=utf-8";
    hb_add(&r->body, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>");
    hb_add_escaped(&r->body, page->name);
    hb_add(&r->body, "</title>\n"
           "<link rel=\"stylesheet\" href=\"../static/hyperdoc.css\">\n"
           "</head>\n<body>\n<h1 class=\"ht-title\">");
    hb_add_escaped(&r->body, page->name);
    hb_add(&r->body, "</h1>\n</body>\n</html>\n");
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

/* the title bar and the scroll bars are part of the HTML page */

void
linkTitleBarWindows(void)
{
}

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
