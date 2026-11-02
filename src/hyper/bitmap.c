/*
 * bitmap.c: read an X11 bitmap file (.bitmap, .xbm) into memory, one
 * bit per pixel, rows of (width + 7) / 8 bytes, least significant bit
 * first.  Moved from HTReadBitmapFile (ReadBitmap.c), which turns the
 * data into an XImage; the browser variant turns it into a PNG.
 */

#include "fricas_c_macros.h"
#include "debug.h"
#include "hyper.h"

#include "all_hyper_proto.H1"

#define MAXLINE      256

static int read_hot(FILE * fd, char Line[], int * x_hot, int * y_hot);
static int read_w_and_h(FILE * fd, unsigned int * width,
                        unsigned int * height);

/* the bits of the bitmap in the open file fd; NULL for a bad format */

char *
read_bitmap_data(FILE *fd, int *width, int *height)
{
    char Line[256], Buff[256];
    char *ptr, *data;
    int rch;
    int version;
    int padding, chars_line, file_chars_line, file_chars;
    int bytes;
    int x_hot, y_hot;

    /*
     * Once it is open, lets get the width and height
     */

    if ((read_w_and_h(fd, (unsigned int *)width,(unsigned int *) height)) < 0) {
        return NULL;
    }


    /*
     * Now get the next line, and see if it is hot spots or bits
     */
    if (fgets(Line, MAXLINE, fd) == NULL) {
        return NULL;
    }

    /*
     * Now check the first character to see if it is a # or an s
     */

    if (Line[0] == '#') {
        if ((read_hot(fd, Line, &x_hot, &y_hot)) < 0) {
            return NULL;
        }
    }

    /*
     * figure out what version
     */

    if (sscanf(Line, "static short %s = {", Buff) == 1)
        version = 10;
    else if (sscanf(Line, "static unsigned char %s = {", Buff) == 1)
        version = 11;
    else if (sscanf(Line, "static char %s = {", Buff) == 1)
        version = 11;
    else {
        return NULL;
    }

    padding = 0;
    if ((*width % 16) && ((*width % 16) < 9) && (version == 10))
        padding = 1;

    chars_line = (*width + 7) / 8;
    file_chars_line = chars_line + padding;

    file_chars = file_chars_line * (*height);
    data = (char *) halloc(chars_line * (*height), "Read Pixmap--Image data");

    /*
     * Since we are just holding the first line of the declaration, we can
     * just start reading from fd
     */

    if (version == 10)
        for (bytes = 0, ptr = data; bytes < file_chars; (bytes += 2)) {
            if (fscanf(fd, " 0x%x%*[,}]%*[ \n]", &rch) != 1) {
                free(data);
                return NULL;
            }
            *(ptr++) = rch & 0xff;
            if (!padding || ((bytes + 2) % file_chars_line))
                *(ptr++) = rch >> 8;
        }
    else
        for (bytes = 0, ptr = data; bytes < file_chars; bytes++, ptr++) {
            if (fscanf(fd, " 0x%x%*[,}]%*[ \n]", &rch) != 1) {
                free(data);
                return NULL;
            }
            *ptr = rch;
        }

    return data;
}

static int
read_hot(FILE *fd,char Line[],int *x_hot,int *y_hot)
{
    char Buff[256];

    /*
     * Works much the same as get width and height, just new variables
     */

    if (sscanf(Line, "#define %s %d", Buff, x_hot) != 2)
        return -1;

    if (fgets(Line, MAXLINE, fd) == NULL)
        return -1;

    if (sscanf(Line, "#define %s %d", Buff, y_hot) != 2)
        return -1;

    if (fgets(Line, MAXLINE, fd) == NULL)
        return -1;
    return 1;
}

static int
read_w_and_h(FILE *fd,unsigned int *width,unsigned int *height)
{
    char Line[256], Buff[256];

    if (fgets(Line, MAXLINE, fd) == NULL)
        return -1;

    /*
     * Once we have the line, scan it for the width
     */

    if (sscanf(Line, "#define %s %d", Buff, width) != 2)
        return -1;

    /*
     * Hopefully we have the width, now get the height the same way
     */

    if (fgets(Line, MAXLINE, fd) == NULL)
        return -1;


    /*
     * Once we have the line, scan it for the height
     */

    if (sscanf(Line, "#define %s %d", Buff, height) != 2)
        return -1;

    return 1;
}


