/*
 * links.c: what happens when a link of a HyperDoc page is followed:
 * the page stacks, the link types, quit and help.  Shared by the X11
 * and the browser variant; moved from event.c.
 */

#include "fricas_c_macros.h"
#include "debug.h"

#include "hyper.h"
#include "parse.h"

#include "all_hyper_proto.H1"

/*
 * This routine is called when the quitbutton is hit. For the moment I am
 * just going to leave it all behind
 */

void
quitHyperDoc(void)
{
    HyperDocPage *page;

    if (gSessionHashTable.num_entries == 1 || gParentWindow == gWindow) {
        if (!strcmp(gWindow->page->name, "ProtectedQuitPage")){
        exitHyperDoc();
                }
        page = (HyperDocPage *) hash_find(gWindow->fPageHashTable, "ProtectedQuitPage");
        if (page == NULL) {
            fprintf(stderr, "Unknown page name %s\n", "ProtectedQuitPage");
            exitHyperDoc();
            return;
        }
        if (gWindow->fDownLinkStackIndex == MaxDownlinkDepth)
            fprintf(stderr, "exceeded maximum link nesting level\n");
        else
            gWindow->fDownLinkStack[gWindow->fDownLinkStackIndex++] = gWindow->page;
        gWindow->page = page;
        display_page(gWindow->page);
        gWindow->fWindowHashTable = gWindow->page->fLinkHashTable;
    }
    else
        exitHyperDoc();
}


/*
 * find_page takes as an argument the HyperDoc for a page name and returns
 * the associated page
 */

static HyperDocPage *
find_page(TextNode * node)
{
    char *page_name;
    HyperDocPage *page;

    /* try and find the page name */
    page_name = print_to_string(node);
    page = (HyperDocPage *) hash_find(gWindow->fPageHashTable, page_name);

    if (page == NULL) {
        /* try to find the unknown page */
        page = (HyperDocPage *) hash_find(gWindow->fPageHashTable, "UnknownPage");
        if (page == NULL) {
            /* Yikes, Even that could not be found */
            fprintf(stderr, "Unknown page name %s\n", page_name);
        }
        else {
            if (page->type == UnloadedPageType)
                page->type = UlUnknownPage;
            else
                page->type = UnknownPage;
        }
    }
    return page;
}

/*
 * These are macros for taking care of the downlink stack, and the memolink
 * stack.
 */

#define NotSpecial(t) ((t == Quitbutton || t == Returnbutton || \
                        t == Upbutton || t == UnknownPage || \
                        t == UlUnknownPage || t == ErrorPage) ?(0):(1))

/* pushes a page onto the down link stack */

static void
downlink(void)
{
    if (gWindow->fDownLinkStackIndex == MaxDownlinkDepth)
        fprintf(stderr, "exceeded maximum link nesting level\n");
    else
        gWindow->fDownLinkStack[gWindow->fDownLinkStackIndex++] = gWindow->page;
}

static void
memolink(void)
{
    if (gWindow->fMemoStackIndex == MaxMemoDepth)
        fprintf(stderr, "exceeded maximum link nesting level\n");
    else {
        gWindow->fMemoStack[gWindow->fMemoStackIndex] = gWindow->page;
        gWindow->fDownLinkStackTop[gWindow->fMemoStackIndex++] = gWindow->fDownLinkStackIndex;
    }
}

static void
kill_fricas_page(HyperDocPage * page)
{
    char command[2048];

    sprintf(command, "(|htpDestroyPage| '%s)", page->name);
    send_lisp_command(command);
}

static void
kill_page(HyperDocPage * page)
{
    page->scroll_off = 0;
    if (page->type == SpadGen) {
        hash_delete(gWindow->fPageHashTable, page->name);
        kill_fricas_page(page);
        free_page(page);
    }
}

/* pops the memo stack */

static HyperDocPage *
returnlink(void)
{
    int i;

    if (gWindow->fMemoStackIndex == 0) {
        BeepAtTheUser();
        return NULL;
    }
    else {
        kill_page(gWindow->page);
        for (i = gWindow->fDownLinkStackIndex - 1;
             i >= gWindow->fDownLinkStackTop[gWindow->fMemoStackIndex - 1];
             i--)
        {
            kill_page(gWindow->fDownLinkStack[i]);
        }
        gWindow->fDownLinkStackIndex =
            gWindow->fDownLinkStackTop[--gWindow->fMemoStackIndex];
        return (gWindow->fMemoStack[gWindow->fMemoStackIndex]);
    }
}

/* pops a page if it can from the downlink stack */

static HyperDocPage *
uplink(void)
{
    if (gWindow->fDownLinkStackIndex == 0)
        return returnlink();
    else {
        kill_page(gWindow->page);
        return (gWindow->fDownLinkStack[--gWindow->fDownLinkStackIndex]);
    }
}

static void
windowlink_handler(TextNode * node)
{
    char *page_name;

    /* first try and find the page */
    page_name = print_to_string(node);

    if (init_top_window(page_name) == -1) {
        return;
    }
/*    gWindow->fWindowHashTable = gWindow->page->fLinkHashTable;*/
}

void
make_window_link(char *name)
{
    if (init_top_window(name) != -1)
{}/*        gWindow->fWindowHashTable = gWindow->page->fLinkHashTable; */
}


static void
lispwindowlink_handler(HyperLink * link)
{

    /*
     * Since we are popping up a new window, then we had better change all
     * the cursors right away. We won't get another chance at it.
     */

    if (init_top_window(NULL) != -1) {
        HyperDocPage *page = NULL;
        int frame = gWindow->fricas_frame;

        page = issue_server_command(link);
        gWindow->fricas_frame = frame;
        gWindow->page = page;
/*        gWindow->fWindowHashTable = gWindow->page->fLinkHashTable;*/
    }
}

static HyperDocPage *
paste_button(PasteNode * paste)
{
    HyperDocPage *page = NULL;
    int pastewhere=paste->where;


    if ( paste->end_node ==NULL || paste->begin_node==NULL || paste->arg_node==NULL ){
        BeepAtTheUser();
        return NULL;
        }

    page=parse_patch(paste);
/* paste has changed after this call so use pastewhere*/

    if (pastewhere && page ) {
        if (0 == strcmp(page->name, "ErrorPage"))
            page = NULL;
    }
    else
        BeepAtTheUser();

    return page;
}

void
helpForHyperDoc(void)
{
    HyperDocPage *page = NULL;

    /* do not do anything if we are already at the "no more help" page */

    if (0 == strcmp(gWindow->page->name, NoMoreHelpPage))
        return;

    /* if no help page recorded, use the standard "no more help" page */

    if (!gWindow->page->helppage)
        gWindow->page->helppage = alloc_string(NoMoreHelpPage);

    /* if we are on the main help page, use "no more help" page */

    if (0 == strcmp(gWindow->page->name, TopLevelHelpPage))
        gWindow->page->helppage = alloc_string(NoMoreHelpPage);

    page = (HyperDocPage *) hash_find(gWindow->fPageHashTable, gWindow->page->helppage);

    if (page)
        make_window_link(gWindow->page->helppage);
    else
        BeepAtTheUser();
}

/*
 * Follow a link that was clicked: the part of handle_button (event.c)
 * that does not depend on the window system.  button1 is true for the
 * first mouse button.
 */

void
follow_link(HyperLink *link, int button1)
{
    HyperDocPage *page = NULL;
    char *page_name;

    switch (link->type) {
      case Pastebutton:
        page = paste_button(link->reference.paste);
        break;
      case Link:
        page_name = print_to_string(link->reference.node);
        page = (HyperDocPage *) hash_find(gWindow->fPageHashTable, page_name);
        break;
      case Helpbutton:
        helpForHyperDoc();
        page = NULL;
        break;
      case Quitbutton:
        quitHyperDoc();
        break;
      case Returnbutton:        /* pop memo information */
        page = returnlink();
        break;
      case Upbutton:            /* pop downlink information */
        page = uplink();
        break;
      case Downlink:
        page = find_page(link->reference.node);
        if (page  && NotSpecial(page->type))
            downlink();
        break;
      case Memolink:
        page = find_page(link->reference.node);
        if (page && NotSpecial(page->type))
            memolink();
        break;
      case Windowlink:
        page = find_page(link->reference.node);
        if (page && NotSpecial(page->type)) {
            windowlink_handler(link->reference.node);
            gNeedIconName = 1;
            page = NULL;
        }
        break;
      case Lispwindowlink:
        lispwindowlink_handler(link);
        gNeedIconName = 1;
        page = NULL;
        break;
      case LispMemoLink:
      case Spadmemolink:
        page = issue_server_command(link);
        if (page && NotSpecial(page->type))
            memolink();
        break;
      case LispDownLink:
      case Spaddownlink:
        page = issue_server_command(link);
        if (page && NotSpecial(page->type))
            downlink();
        break;
      case Spadlink:
      case Lisplink:
        page = issue_server_command(link);
        break;
      case Lispcommand:
      case Qspadcall:
      case Spadcall:
        page = issue_server_command(link);
        break;
      case Lispcommandquit:
      case Spadcallquit:
      case Qspadcallquit:
        page = issue_server_command(link);
        exitHyperDoc();
        break;
      case Spadcommand:
      case Spadgraph:
      case Spadsrc:
        issue_spadcommand(gWindow->page, link->reference.node,
                          button1, link->type);
        break;
      case Unixlink:
        page = issue_unixlink(link->reference.node);
        if (page && NotSpecial(page->type)) {
            downlink();
        }
        break;
      case Unixcommand:
        issue_unixcommand(link->reference.node);
        break;
      default:
        break;
    }

    if (page) {
        switch (page->type) {   /* check for special button types */
          case Quitbutton:
            exitHyperDoc();
            return;
          case Returnbutton:
            gWindow->page = returnlink();
            break;
          case Upbutton:
            gWindow->page = uplink();
            break;
          case ErrorPage:
          case UnknownPage:
          case UlUnknownPage:
            if (page->type == UlUnknownPage)
                page->type = UnloadedPageType;
            downlink();
            gWindow->page = page;
            break;
          default:              /* a normal link */
            gWindow->page = page;
            break;
        }
        if (link->type != Pastebutton)
            display_page(gWindow->page);
        gWindow->fWindowHashTable = gWindow->page->fLinkHashTable;      /* reset the window hash */
    }
}

/*
 * For the browser's history: is page name on the down link or memo
 * stack of the window, so that Up reaches it?
 */

int
page_on_stack(const char *name)
{
    int i;

    for (i = 0; i < gWindow->fDownLinkStackIndex; i++)
        if (strcmp(gWindow->fDownLinkStack[i]->name, name) == 0)
            return 1;
    for (i = 0; i < gWindow->fMemoStackIndex; i++)
        if (strcmp(gWindow->fMemoStack[i]->name, name) == 0)
            return 1;
    return 0;
}

/* press Up, as the title bar button does; 0 if there is nothing above */

int
press_up(void)
{
    HyperLink up;

    if (gWindow->fDownLinkStackIndex == 0 && gWindow->fMemoStackIndex == 0)
        return 0;
    memset(&up, 0, sizeof(up));
    up.type = Upbutton;
    follow_link(&up, 1);
    return 1;
}

/* show page as a down link to it would */

void
show_as_downlink(HyperDocPage *page)
{
    downlink();
    gWindow->page = page;
    display_page(gWindow->page);
    gWindow->fWindowHashTable = gWindow->page->fLinkHashTable;
}

/* the links of the buttons in the title bar; moved from titlebar.c */

void
linkTitleBarWindows(void)
{
    HyperLink *tw1link = (HyperLink *) halloc(sizeof(HyperLink), "HyperLink"),
              *tw2link = (HyperLink *) halloc(sizeof(HyperLink), "HyperLink"),
              *tw3link = (HyperLink *) halloc(sizeof(HyperLink), "HyperLink"),
              *tw4link = (HyperLink *) halloc(sizeof(HyperLink), "HyperLink");

    tw1link->win = gWindow->fTitleBarButton1;
    tw1link->type = Quitbutton;
    tw1link->reference.node = NULL;
    tw1link->x = tw1link->y = 0;

    tw2link->win = gWindow->fTitleBarButton2;
    tw2link->type = Helpbutton;
    tw2link->reference.node = NULL;
    tw2link->x = tw2link->y = 0;

    tw3link->win = gWindow->fTitleBarButton3;
    tw3link->type = Returnbutton;
    tw3link->reference.node = NULL;
    tw3link->x = tw3link->y = 0;

    tw4link->win = gWindow->fTitleBarButton4;
    tw4link->type = Upbutton;
    tw4link->reference.node = NULL;
    tw4link->x = tw4link->y = 0;

    hash_insert(gLinkHashTable, (char *)tw1link,(char *) &tw1link->win);
    hash_insert(gLinkHashTable, (char *)tw2link,(char *) &tw2link->win);
    hash_insert(gLinkHashTable, (char *)tw3link,(char *) &tw3link->win);
    hash_insert(gLinkHashTable, (char *)tw4link,(char *) &tw4link->win);
}
