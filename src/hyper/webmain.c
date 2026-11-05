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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
 * The values of a submitted form go into the input fields and boxes of
 * the current page, where print_to_string finds them, as typing and
 * clicking do in X11 (dialog.c, input.c).  Only if the form belongs
 * to the page shown now.
 */

/* the value of field name (with this value, if not NULL), or NULL */

static char *
field_value(FormField *f, int n, const char *name, const char *value)
{
    int i;

    for (i = 0; i < n; i++)
        if (strcmp(f[i].name, name) == 0
            && (value == NULL || strcmp(f[i].value, value) == 0))
            return f[i].value;
    return NULL;
}

/* the form being stored, for store_box */
static FormField *form_fields;
static int form_count;

/* the browser sends UTF-8; HyperDoc keeps Latin-1 (as hb_add_escaped) */

static void
to_latin1(char *s)
{
    unsigned char *p = (unsigned char *) s, *d = p;

    while (*p) {
        if ((p[0] == 0xc2 || p[0] == 0xc3) && (p[1] & 0xc0) == 0x80) {
            *d++ = (unsigned char) (((p[0] & 3) << 6) | (p[1] & 0x3f));
            p += 2;
        }
        else if (*p == '\r')
            p++;
        else
            *d++ = *p++;
    }
    *d = '\0';
}

static void
store_box(void *data)
{
    InputBox *box = (InputBox *) data;
    char name[256];
    char *value;

    if (box->rbs == NULL)
        box->picked = field_value(form_fields, form_count, "b", box->name) != NULL;
    else if (snprintf(name, sizeof(name), "r:%s", box->rbs->name) < (int) sizeof(name)
             && (value = field_value(form_fields, form_count, name, NULL)) != NULL)
        box->picked = strcmp(value, box->name) == 0;
}

static int
store_form(FormField *form, int nform)
{
    HyperDocPage *page = gWindow->page;
    char *page_name = field_value(form, nform, "page", NULL);
    int i;

    if (page_name == NULL || strcmp(page_name, page->name) != 0)
        return 0;
    form_fields = form;
    form_count = nform;
    for (i = 0; i < nform; i++) {
        InputItem *item;

        if (strncmp(form[i].name, "s:", 2) != 0
            || (item = return_item(form[i].name + 2)) == NULL)
            continue;
        to_latin1(form[i].value);
        /* the lines of the field anew, from the value */
        {
            char *name = alloc_string(item->name);

            free_input_item(item, 0);
            item->name = name;
        }
        initialize_default(item, form[i].value);
    }
    if (page->box_hash != NULL)
        hash_map(page->box_hash, store_box);
    return 1;
}

/*
 * The browser's history (plan.md, step 5, "Browser back button"):
 * the address of a page carries a running number and the page name,
 * w<n>/?v=<k>&p=<name>.  A lower number than the one shown last in the
 * window is a step back, which is Up (user, 2026-11-02: "Up button and
 * back on browser should be the same"); a higher one (forward) or none
 * (a bookmark) shows a static page as a down link does.
 */

#define MaxWindows 256

static struct {
    Window win;
    int v;
    HyperDocPage *page;
} shown[MaxWindows];
static int last_v = 0;

/* the slot of window win: the number and the page shown last */

static int
shown_slot(Window win)
{
    int i;

    for (i = 0; i < MaxWindows && shown[i].win != 0; i++)
        if (shown[i].win == win)
            return i;
    if (i == MaxWindows)
        i = (int) (win % MaxWindows);   /* reuse a slot; old windows */
    shown[i].win = win;
    shown[i].v = 0;
    shown[i].page = NULL;
    return i;
}

/*
 * Has the current page of gWindow been sent to the browser?  Only then
 * is it parsed (display_page) and has the fields and links that a
 * request names.
 */

static int
page_was_shown(void)
{
    return shown[shown_slot(gWindow->fMainWindow)].page == gWindow->page;
}

/* the address of the current page of gWindow, with a new number */

static char *
page_location(const char *prefix)
{
    static char location[600];
    const unsigned char *s;
    int n;

    n = snprintf(location, sizeof(location), "%sw%lu/?v=%d&p=", prefix,
                 (unsigned long) gWindow->fMainWindow, ++last_v);
    for (s = (const unsigned char *) gWindow->page->name;
         *s && n < (int) sizeof(location) - 4; s++) {
        if (isalnum(*s) || *s == '-' || *s == '_' || *s == '.')
            location[n++] = (char) *s;
        else
            n += sprintf(location + n, "%%%02X", *s);
    }
    location[n] = '\0';
    return location;
}

/*
 * X11 keeps the scroll position of a page while it is on the page
 * stack, and a click that keeps the page does not move it.  Here the
 * browser loads the page again; the address then names a place on it:
 * the clicked link ("#l<id>", the id of the link element), or, when Up
 * or Return leads back to a page, the link last clicked on it (the
 * browser's Back button keeps the browser's own place).  Show/hide of
 * an example needs no request (html.c, write_toggle).
 */

#define MaxClicks 256

static Window anchored_window = 0;  /* the next page of it is shown at a place */

static struct {
    HyperDocPage *page;
    Window link;
} clicks[MaxClicks];
static int next_click = 0;

static void
remember_click(HyperDocPage *page, Window link)
{
    int i;

    for (i = 0; i < MaxClicks; i++)
        if (clicks[i].page == page) {
            clicks[i].link = link;
            return;
        }
    clicks[next_click].page = page;
    clicks[next_click].link = link;
    next_click = (next_click + 1) % MaxClicks;
}

/* the link last clicked on page, if it is still one of its links */

static Window
last_click(HyperDocPage *page)
{
    int i;

    for (i = 0; i < MaxClicks; i++)
        if (clicks[i].page == page && page->fLinkHashTable != NULL
            && hash_find(page->fLinkHashTable, (char *) &clicks[i].link) != NULL)
            return clicks[i].link;
    return 0;
}

/* the address of the current page of gWindow, at the place of link */

static char *
page_location_at(const char *prefix, Window link)
{
    char *location = page_location(prefix);

    if (link != 0 && strlen(location) + 24 < 600)
        sprintf(location + strlen(location), "#l%lu", (unsigned long) link);
    return location;
}

/* follow a clicked link; the answer names the page then shown */

static void
follow_link_request(HyperLink *link, const char *prefix, HttpResponse *r)
{
    HDWindow *window = gWindow;
    HyperDocPage *page = gWindow->page;
    Window id = link->win;
    int type = link->type;
    char *location;

    remember_click(page, id);
    follow_link(link, 1);
    if (gWindow == NULL)
        gWindow = gParentWindow;
    if (gWindow == window && gWindow->page == page)
        location = page_location_at(prefix, id);
    else if (gWindow == window && (type == Upbutton || type == Returnbutton))
        location = page_location_at(prefix, last_click(gWindow->page));
    else
        location = page_location(prefix);
    /* the page is shown at a place: no field takes the keyboard (html.c) */
    anchored_window = strchr(location, '#') != NULL ? gWindow->fMainWindow : 0;
    r->location = location;
}

/* the link of the current page named by the number s, or NULL */

static HyperLink *
find_link(const char *s)
{
    char *end;
    Window id = strtoul(s, &end, 10);

    if (end == s || *end != '\0')
        return NULL;
    return (HyperLink *) hash_find(gWindow->page->fLinkHashTable, (char *) &id);
}

/*
 * Bring page name (from the address) into gWindow: back is Up, until
 * the page is reached if it is on the stack, else once; forward or a
 * bookmark shows a static page as a down link.
 */

static void
go_to_named_page(const char *name, const char *number)
{
    int *cur = &shown[shown_slot(gWindow->fMainWindow)].v;
    HyperDocPage *page;

    if (strcmp(gWindow->page->name, name) == 0)
        return;
    if (number != NULL && atoi(number) < *cur) {
        if (page_on_stack(name)) {
            while (gWindow != NULL && strcmp(gWindow->page->name, name) != 0
                   && press_up())
                ;
        }
        else
            press_up();
        return;
    }
    page = (HyperDocPage *) hash_find(gWindow->fPageHashTable, (char *) name);
    if (page != NULL && page->type != SpadGen)
        show_as_downlink(page);
}

/*
 * A request for HyperDoc window n, path "<n>/" (a page of it, see
 * above), "<n>/link/<id>" (a click on the link with that id) or
 * "<n>/goto/<name>" (page <name> in a new window); the last two
 * redirect to the page that is shown now.  A form (POST) first stores
 * its values; posted to "<n>/", it only stores them.
 */

void
html_window_request(char *path, FormField *query, int nquery,
                    FormField *form, int nform, HttpResponse *r)
{
    HDWindow *htw;
    HyperLink *link;
    Window id;
    char *rest, *name, *number;

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
    r->status = 303;
    if ((form != NULL || *rest != '\0') && strncmp(rest, "goto/", 5) != 0
        && !page_was_shown()) {
        /* a form or link of a page the browser has not got: show it */
        r->location = page_location(*rest == '\0' ? "../" : "../../");
        return;
    }
    if (form != NULL) {
        int stored = store_form(form, nform);

        if (*rest == '\0') {
            /* the button that was pressed names its link (html.c) */
            number = field_value(form, nform, "link", NULL);
            if (number != NULL && (link = find_link(number)) != NULL)
                follow_link_request(link, "../", r);
            else if (number == NULL && stored)
                /*
                 * Return in a field: the values are stored; the browser
                 * stays where it is, with the cursor in the field, as X11
                 */
                r->status = 204;
            else
                r->location = page_location("../");
            return;
        }
    }
    if (*rest == '\0') {
        name = field_value(query, nquery, "p", NULL);
        number = field_value(query, nquery, "v", NULL);
        if (name != NULL)
            go_to_named_page(name, number);
        if (gWindow == NULL)
            gWindow = gParentWindow;
        if (name == NULL || gWindow->fMainWindow != id
            || strcmp(gWindow->page->name, name) != 0) {
            /* the address of the page that is shown */
            r->location = page_location("../");
            return;
        }
        shown[shown_slot(id)].v = number != NULL ? atoi(number) : ++last_v;
        display_page(gWindow->page);
        shown[shown_slot(id)].page = gWindow->page;
        r->status = 200;
        r->content_type = "text/html; charset=utf-8";
        /* shown at a place: no field takes the keyboard (it would scroll) */
        html_write_page(&r->body, anchored_window != id);
        anchored_window = 0;
        return;
    }
    if (strncmp(rest, "goto/", 5) == 0)
        /* open page <name> in a new window, as the help key does */
        make_window_link(rest + 5);
    else if (strncmp(rest, "link/", 5) == 0) {
        if ((link = find_link(rest + 5)) != NULL) {
            follow_link_request(link, "../../", r);
            return;
        }
    }
    else {
        r->status = 404;
        return;
    }
    if (gWindow == NULL)
        gWindow = gParentWindow;
    r->location = page_location("../../");
}

/*
 * Opening the browser (plan.md, "Opening the browser").  The opener
 * gets a small HTML file that redirects to the root page, so the token
 * is not on a command line.  The file is named after the FriCAS
 * session (SPADNUM), so that a HyperDoc started by )hd while another
 * one runs can open that one's page instead.
 */

static char redirect_file[1024];

static void
remove_redirect_file(void)
{
    unlink(redirect_file);
}

/* the directory for the files given to the browser or the terminal */

static char *
runtime_dir(void)
{
    char *dir = getenv("XDG_RUNTIME_DIR");

    if (dir == NULL || *dir == '\0')
        dir = getenv("TMPDIR");
    if (dir == NULL || *dir == '\0')
        dir = getenv("HOME");
    if (dir == NULL || *dir == '\0')
        return NULL;
    return dir;
}

static int
redirect_file_name(char *name, int size)
{
    char *dir = runtime_dir();
    char *num = getenv("SPADNUM");

    if (dir == NULL)
        return -1;
    if (num != NULL)
        return snprintf(name, size, "%s/fricas-hyperdoc-%s.html", dir, num)
            < size ? 0 : -1;
    return snprintf(name, size, "%s/fricas-hyperdoc-p%ld.html", dir, (long) getpid())
        < size ? 0 : -1;
}

/* write the redirect file for url, readable only by the user */

static int
write_redirect_file(const char *name, const char *url)
{
    FILE *f;
    int fd;

    unlink(name);
    fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd == -1 || (f = fdopen(fd, "w")) == NULL) {
        if (fd != -1)
            close(fd);
        return -1;
    }
    fprintf(f, "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n"
            "<meta http-equiv=\"refresh\" content=\"0;url=%s\">\n"
            "<title>FriCAS HyperDoc</title></head>\n"
            "<body><a href=\"%s\">FriCAS HyperDoc</a></body></html>\n",
            url, url);
    return fclose(f) == 0 ? 0 : -1;
}

/*
 * Is the redirect file name that of a HyperDoc that runs now?  Its URL
 * must name a port on which a server accepts connections.
 */

static int
running_hyperdoc(const char *name)
{
    char buf[512];
    char *p;
    FILE *f = fopen(name, "r");
    struct sockaddr_in addr;
    int n, port, fd, ok;

    if (f == NULL)
        return 0;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    if ((p = strstr(buf, "url=http://127.0.0.1:")) == NULL
        || (port = atoi(p + 21)) <= 0 || port > 65535)
        return 0;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    do
        ok = connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == 0;
    while (!ok && errno == EINTR);
    close(fd);
    return ok;
}

/*
 * Give file to the opener: $BROWSER if set (BROWSER=true opens
 * nothing), else the system's one; not in an SSH session nor on a
 * Linux console without a display, where only the URL is printed.
 */

static void
open_browser(const char *file)
{
    char *browser = getenv("BROWSER");
    pid_t pid;

    if (browser != NULL && *browser != '\0') {
        if (strcmp(browser, "true") == 0)
            return;
    }
    else {
        if (getenv("SSH_CONNECTION") != NULL)
            return;
#ifdef MACOSXplatform
        browser = "open";
#else
        if (getenv("DISPLAY") == NULL && getenv("WAYLAND_DISPLAY") == NULL)
            return;
        browser = "xdg-open";
#endif
    }
    pid = fork();
    if (pid == 0) {
        int fd;

        /* the browser must not keep the sockets of HyperDoc open */
        for (fd = 3; fd < FD_SETSIZE; fd++)
            close(fd);
        signal(SIGINT, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        /* BROWSER may hold arguments, as for xdg-open */
        execl("/bin/sh", "sh", "-c", "exec $0 \"$1\"", browser, file, (char *) NULL);
        _exit(127);
    }
}

/*
 * The terminal for the FriCAS buffer of an example page: X11 runs
 * spadbuf in xterm (start_user_buffer in spadint.c); here the terminal
 * of the system runs it.  macOS: a script that the terminal opens for
 * files *.command (it does not inherit the environment, so the script
 * sets SPADNUM, which spadbuf needs to find HyperDoc); other systems:
 * the first terminal program found.  Returns -1 if none is started.
 */

#ifdef MACOSXplatform
static void
write_quoted(FILE *f, const char *s)
{
    putc('\'', f);
    for (; *s; s++) {
        if (*s == '\'')
            fputs("'\\''", f);
        else
            putc(*s, f);
    }
    putc('\'', f);
}
#else
static int
in_path(const char *name)
{
    char *path = getenv("PATH");
    char file[1024];
    const char *p, *end;

    for (p = path; p != NULL && *p; p = *end ? end + 1 : end) {
        end = strchr(p, ':');
        if (end == NULL)
            end = p + strlen(p);
        if (snprintf(file, sizeof(file), "%.*s/%s", (int) (end - p), p, name)
            < (int) sizeof(file) && access(file, X_OK) == 0)
            return 1;
    }
    return 0;
}
#endif

static int
run_program(char *const argv[])
{
    pid_t pid = fork();

    if (pid == -1)
        return -1;
    if (pid == 0) {
        int fd;

        /* the terminal must not keep the sockets of HyperDoc open */
        for (fd = 3; fd < FD_SETSIZE; fd++)
            close(fd);
        signal(SIGINT, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        execvp(argv[0], argv);
        _exit(127);
    }
    return 0;
}

int
start_terminal(char *title, char *program, char *page_name, char *complfile)
{
    if (getenv("SSH_CONNECTION") != NULL)
        return -1;
#ifdef MACOSXplatform
    {
        static int count = 0;
        char *dir = runtime_dir(), *num = getenv("SPADNUM");
        char name[1024];
        char *argv[3];
        FILE *f;
        int fd;

        if (dir == NULL || num == NULL
            || snprintf(name, sizeof(name), "%s/fricas-buffer-%s-%d.command",
                        dir, num, ++count) >= (int) sizeof(name))
            return -1;
        unlink(name);
        fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0700);
        if (fd == -1 || (f = fdopen(fd, "w")) == NULL) {
            if (fd != -1)
                close(fd);
            return -1;
        }
        fputs("#!/bin/sh\nrm -f \"$0\"\nprintf '\\033]0;%s\\007' ", f);
        write_quoted(f, title);
        fputs("\nSPADNUM=", f);
        write_quoted(f, num);
        fputs("; export SPADNUM\nexec ", f);
        write_quoted(f, program);
        putc(' ', f);
        write_quoted(f, page_name);
        if (complfile != NULL) {
            putc(' ', f);
            write_quoted(f, complfile);
        }
        putc('\n', f);
        if (fclose(f) != 0) {
            unlink(name);
            return -1;
        }
        argv[0] = "open";
        argv[1] = name;
        argv[2] = NULL;
        return run_program(argv);
    }
#else
    {
        /* the terminal, the option for the title and the one before the command */
        static char *terminals[][3] = {
            {"x-terminal-emulator", "-T", "-e"},
            {"gnome-terminal", "--title", "--"},
            {"konsole", NULL, "-e"},
            {"xfce4-terminal", "-T", "-x"},
            {"xterm", "-T", "-e"}
        };
        char *argv[8];
        int i, n = 0;

        if (getenv("DISPLAY") == NULL && getenv("WAYLAND_DISPLAY") == NULL)
            return -1;
        for (i = 0; i < (int) (sizeof(terminals) / sizeof(terminals[0])); i++)
            if (in_path(terminals[i][0]))
                break;
        if (i == (int) (sizeof(terminals) / sizeof(terminals[0])))
            return -1;
        argv[n++] = terminals[i][0];
        if (terminals[i][1] != NULL) {
            argv[n++] = terminals[i][1];
            argv[n++] = title;
        }
        argv[n++] = terminals[i][2];
        argv[n++] = program;
        argv[n++] = page_name;
        if (complfile != NULL)
            argv[n++] = complfile;
        argv[n] = NULL;
        return run_program(argv);
    }
#endif
}

/*
 * At the start: a HyperDoc that could not connect to FriCAS while one
 * for the same session runs (as after )hd) shows that one's page and
 * ends; otherwise this one writes its redirect file and opens it.
 */

static void
show_hyperdoc(void)
{
    if (redirect_file_name(redirect_file, sizeof(redirect_file)) == -1)
        return;
    if (spad_socket == NULL && getenv("SPADNUM") != NULL
        && running_hyperdoc(redirect_file)) {
        open_browser(redirect_file);
        exit(0);
    }
    if (write_redirect_file(redirect_file, httpd_url()) == -1) {
        redirect_file[0] = '\0';
        return;
    }
    atexit(remove_redirect_file);
    open_browser(redirect_file);
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
    show_hyperdoc();
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
