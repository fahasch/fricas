/*
 * webmain.c: start-up and main loop of the browser variant of
 * HyperDoc (configure option html).  It defines the functions and
 * globals that the shared files (hyper.c, parse*.c, mem.c, ...) take
 * from the X11 files initx.c, event.c, keyin.c and extent1.c.
 * A Window is a number here: it names a HyperDoc window, a link or an
 * input field.
 */

#include "fricas_c_macros.h"
#include "debug.h"

#include <sys/select.h>

#include "extent.h"
#include "hyper.h"
#include "html.h"

#include "all_hyper_proto.H1"
#include "sockio-c.H1"
#include "html.H1"
#include "httpd.H1"
#include "webmain.H1"

/* globals of initx.c used by the shared files */

XFontStruct *gActiveFont, *fricas_font, *gBfFont, *gEmFont, *gInputFont,
            *gItFont, *gRmFont, *gSlFont, *gTitleFont, *gTtFont;

int gActiveColor, fricas_color, gBackgroundColor, gBfColor, gControlBackgroundColor,
    gControlForegroundColor, gEmColor, gInputBackgroundColor,
    gInputForegroundColor, gItColor, gRmColor, gSlColor, gTtColor;

int gBorderColor;

/* globals of event.c and extent1.c */

Window gActiveWindow;
int gNeedIconName = 0;
short int gInDesc;
short int gInItem;
int indent;
int item_indent;

static Window last_window_id = 0;

static Window
new_window_id(void)
{
    return ++last_window_id;
}

/* the HTTP server takes the place of the X11 display */

void
initializeWindowSystem(void)
{
    if (httpd_open() == -1)
        exit(-1);
    /* initx.c does this when it loads the fonts */
    init_group_stack();
}

void
init_keyin(void)
{
}

/*
 * As init_top_window in initx.c, without opening an X11 window: the
 * new HyperDoc window gets a number as name.
 */

int
init_top_window(char *name)
{
    HyperDocPage *page;
    HDWindow *old_win = gWindow;

    gWindow = alloc_hd_window();

    if (name == NULL) {
        page = alloc_page((char *) NULL);
    }
    else {
        page = (HyperDocPage *) hash_find(gWindow->fPageHashTable, name);
        if (page == NULL) {
            fprintf(stderr, "(HyperDoc)  Could not find page %s in page hash table \n",
                    name);
            if (gParentWindow == NULL)
                exit(-1);
            else {
                gWindow = old_win;
                return -1;
            }
        }
    }
    gWindow->page = page;
    gWindow->fMainWindow = new_window_id();
    gWindow->fScrollWindow = gWindow->fMainWindow;
    gWindow->fDisplayedWindow = gWindow->fMainWindow;
    gWindow->fTitleBarButton1 = new_window_id();
    gWindow->fTitleBarButton2 = new_window_id();
    gWindow->fTitleBarButton3 = new_window_id();
    gWindow->fTitleBarButton4 = new_window_id();
    hash_insert(&gSessionHashTable, (char *)gWindow, (char *) &gWindow->fMainWindow);
    return 1;
}

/* the functions that hide X11 from the shared files, see initx.c */

/* every link gets a number: it names the link in the URL */

Window
make_link_subwindow(int is_sub)
{
    (void) is_sub;
    return new_window_id();
}

Window
make_input_subwindow(int border)
{
    (void) border;
    return new_window_id();
}

void
destroy_subwindow(Window w)
{
    (void) w;
}

void
free_window_gcs(HDWindow *w)
{
    (void) w;
}

void
unmap_page_subwindows(void)
{
}

void
set_icon_name(char *title)
{
    (void) title;
}

int
display_is_mono(void)
{
    return 0;
}

/*
 * As exitHyperDoc in event.c: close the current HyperDoc window; the
 * last one ends the program.
 */

void
exitHyperDoc(void)
{
    if (gSessionHashTable.num_entries == 1 || gParentWindow == gWindow) {
        free_hd_window(gWindow);
        exit(0);
    }
    hash_delete(&gSessionHashTable, (char *)&gWindow->fMainWindow);
    free_hd_window(gWindow);
    gWindow = NULL;
    gActiveWindow = -1;
}

/*
 * A request for HyperDoc window n, path "<n>/" (its current page),
 * "<n>/link/<id>" (a click on the link with that id) or "<n>/goto/<name>"
 * (page <name> in a new window); the last two redirect to the page
 * that is shown now.
 */

void
html_window_request(char *path, HttpResponse *r)
{
    static char location[64];
    static int version = 0;
    HDWindow *htw;
    HyperLink *link;
    Window id;
    char *rest;

    id = strtoul(path, &rest, 10);
    if (rest == path || *rest != '/') {
        r->status = 404;
        return;
    }
    htw = (HDWindow *) hash_find(&gSessionHashTable, (char *) &id);
    if (htw == NULL) {
        r->status = 404;
        return;
    }
    gWindow = htw;
    rest++;
    if (*rest == '\0') {
        display_page(gWindow->page);
        r->status = 200;
        r->content_type = "text/html; charset=utf-8";
        html_write_page(&r->body);
        return;
    }
    if (strncmp(rest, "goto/", 5) == 0) {
        /* open page <name> in a new window, as the help key does */
        make_window_link(rest + 5);
        snprintf(location, sizeof(location), "../../w%lu/?v=%d",
                 (unsigned long) gWindow->fMainWindow, ++version);
        r->status = 303;
        r->location = location;
        return;
    }
    if (strncmp(rest, "link/", 5) != 0) {
        r->status = 404;
        return;
    }
    id = strtoul(rest + 5, &rest, 10);
    link = (HyperLink *) hash_find(gWindow->page->fLinkHashTable, (char *) &id);
    if (link != NULL && *rest == '\0')
        follow_link(link, 1);
    if (gWindow == NULL)
        gWindow = gParentWindow;
    snprintf(location, sizeof(location), "../../w%lu/?v=%d",
             (unsigned long) gWindow->fMainWindow, ++version);
    r->status = 303;
    r->location = location;
}

/*
 * The loop of mainEventLoop in event.c without X11: wait for the
 * browser, the FriCAS image and the session manager.  A HyperDoc that
 * is not connected to FriCAS (e.g. started by )hd while another one
 * holds the connection) has no window that the user could close: it
 * ends when the process that started it is gone.
 */

#define OrphanCheck 5           /* seconds between the checks */

void
mainEventLoop(void)
{
    fd_set rd;
    struct timeval tv;
    int ret;

    gActiveWindow = -1;
    while (1) {
        while (gSessionHashTable.num_entries == 0)
            pause();
        FD_ZERO(&rd);
        FD_SET(httpd_socket, &rd);
        if (spad_socket)
            FD_SET(spad_socket->socket, &rd);
        if (session_server)
            FD_SET(session_server->socket, &rd);
        tv.tv_sec = OrphanCheck;
        tv.tv_usec = 0;
        ret = sselect(FD_SETSIZE, &rd, NULL, NULL, spad_socket ? NULL : &tv);
        if (ret == -1) {
            free_hd_window(gWindow);
            exit(1);
        }
        if (ret == 0) {
            if (getppid() == 1)
                exit(0);
            continue;
        }
        if (FD_ISSET(httpd_socket, &rd))
            httpd_serve();
        else if (spad_socket && FD_ISSET(spad_socket->socket, &rd)) {
            int cmd = get_int(spad_socket);

            if (cmd == -1) {
                /* the FriCAS image has shut down */
                free_hd_window(gWindow);
                exit(0);
            }
            if (cmd == 100)
                fprintf(stderr,
                        "(HyperDoc) pages from FriCAS are not shown yet\n");
        }
        else if (session_server && FD_ISSET(session_server->socket, &rd))
            service_session_socket();
    }
}
