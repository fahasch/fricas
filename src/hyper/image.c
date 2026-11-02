/*
 * image.c: pictures of the browser variant of HyperDoc.  The writer
 * registers every picture file of a page here and refers to it by its
 * number (img/<n>.png); only registered files are served, converted
 * to PNG: X11 bitmaps (.bitmap, .xbm, read by read_bitmap_data) with
 * a transparent background, and X11 pixmaps (.xpm, written by the
 * viewers for \inputimage).  The PNG is not compressed (deflate
 * "stored" blocks), so no zlib is needed.
 */

#include "fricas_c_macros.h"
#include "debug.h"

#include <ctype.h>
#include <strings.h>

#include "hyper.h"
#include "html.h"

#include "all_hyper_proto.H1"
#include "html.H1"

#define MaxImages 4096
#define MaxPixels (4096 * 4096)

static char *image_files[MaxImages];
static int num_images = 0;

/* the number of the picture file name, registered on first use */

int
html_image_number(const char *name)
{
    int i;

    if (*name == ' ')
        name++;
    for (i = 0; i < num_images; i++)
        if (strcmp(image_files[i], name) == 0)
            return i;
    if (num_images == MaxImages)
        return -1;
    image_files[num_images] = alloc_string((char *) name);
    return num_images++;
}

/* PNG: chunks with CRC-32, image data as zlib stream of stored blocks */

static unsigned long crc_table[256];

static void
make_crc_table(void)
{
    unsigned long c;
    int n, k;

    for (n = 0; n < 256; n++) {
        c = (unsigned long) n;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static unsigned long
crc(unsigned long c, const unsigned char *buf, int len)
{
    int n;

    for (n = 0; n < len; n++)
        c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    return c;
}

static void
put_u32(HtmlBuf *b, unsigned long v)
{
    char s[4];

    s[0] = (char) ((v >> 24) & 0xff);
    s[1] = (char) ((v >> 16) & 0xff);
    s[2] = (char) ((v >> 8) & 0xff);
    s[3] = (char) (v & 0xff);
    hb_addn(b, s, 4);
}

static void
put_chunk(HtmlBuf *b, const char *type, const unsigned char *data, int len)
{
    unsigned long c;

    put_u32(b, (unsigned long) len);
    hb_addn(b, type, 4);
    hb_addn(b, (const char *) data, len);
    c = crc(0xffffffffUL, (const unsigned char *) type, 4);
    c = crc(c, data, len);
    put_u32(b, c ^ 0xffffffffUL);
}

/*
 * Write width x height RGBA pixels (raw: one filter byte 0 and then
 * width * 4 bytes per row) as PNG.
 */

static void
write_png(HtmlBuf *b, const unsigned char *raw, int width, int height)
{
    HtmlBuf z;
    unsigned char ihdr[13];
    unsigned long a = 1, s2 = 0;
    long len = (long) height * (1 + 4L * width), pos;

    if (crc_table[1] == 0)
        make_crc_table();
    hb_add(b, "\211PNG\r\n\032\n");
    ihdr[0] = (unsigned char) (width >> 24);
    ihdr[1] = (unsigned char) (width >> 16);
    ihdr[2] = (unsigned char) (width >> 8);
    ihdr[3] = (unsigned char) width;
    ihdr[4] = (unsigned char) (height >> 24);
    ihdr[5] = (unsigned char) (height >> 16);
    ihdr[6] = (unsigned char) (height >> 8);
    ihdr[7] = (unsigned char) height;
    ihdr[8] = 8;                /* bit depth */
    ihdr[9] = 6;                /* RGBA */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    put_chunk(b, "IHDR", ihdr, 13);

    memset(&z, 0, sizeof(z));
    hb_addn(&z, "\170\001", 2); /* zlib header: deflate, no dictionary */
    for (pos = 0; pos < len || len == 0; ) {
        int n = len - pos > 65535 ? 65535 : (int) (len - pos);
        char h[5];

        h[0] = (char) (pos + n == len);
        h[1] = (char) (n & 0xff);
        h[2] = (char) (n >> 8);
        h[3] = (char) (~n & 0xff);
        h[4] = (char) ((~n >> 8) & 0xff);
        hb_addn(&z, h, 5);
        hb_addn(&z, (const char *) raw + pos, n);
        pos += n;
        if (len == 0)
            break;
    }
    for (pos = 0; pos < len; pos++) {
        a = (a + raw[pos]) % 65521;
        s2 = (s2 + a) % 65521;
    }
    put_u32(&z, (s2 << 16) | a);
    put_chunk(b, "IDAT", (const unsigned char *) z.data, z.len);
    hb_free(&z);
    put_chunk(b, "IEND", (const unsigned char *) "", 0);
}

static unsigned char *
new_raw(int width, int height)
{
    long size = (long) height * (1 + 4L * width);
    unsigned char *raw = (unsigned char *) halloc(size > 0 ? size : 1, "PNG data");

    memset(raw, 0, size);
    return raw;
}

/* an X11 bitmap: set bits black, the rest transparent */

static int
bitmap_png(FILE *fd, HtmlBuf *b)
{
    int width, height, x, y, bpl;
    char *bits = read_bitmap_data(fd, &width, &height);
    unsigned char *raw;

    if (bits == NULL || width <= 0 || height <= 0
        || (long) width * height > MaxPixels)
        return -1;
    bpl = (width + 7) / 8;
    raw = new_raw(width, height);
    for (y = 0; y < height; y++) {
        unsigned char *row = raw + (long) y * (1 + 4L * width) + 1;

        for (x = 0; x < width; x++)
            if (bits[y * bpl + x / 8] & (1 << (x % 8)))
                row[4 * x + 3] = 255;
    }
    write_png(b, raw, width, height);
    free(raw);
    free(bits);
    return 0;
}

/* the next string constant "..." of an XPM file, into buf */

static int
xpm_string(FILE *fd, char *buf, int size)
{
    int c, n = 0;

    while ((c = getc(fd)) != EOF && c != '"')
        ;
    if (c == EOF)
        return -1;
    while ((c = getc(fd)) != EOF && c != '"') {
        if (n < size - 1)
            buf[n++] = (char) c;
    }
    buf[n] = '\0';
    return c == EOF ? -1 : n;
}

static int
hex_value(const char *s, int n)
{
    int v = 0, i;

    for (i = 0; i < n; i++) {
        int c = s[i];

        v = 16 * v + (isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10));
    }
    return v;
}

/* an XPM colour: #RGB, #RRGGBB, #RRRRGGGGBBBB, None, black, white */

static void
xpm_colour(const char *spec, unsigned char rgba[4])
{
    int n = 0;

    rgba[0] = rgba[1] = rgba[2] = 0;
    rgba[3] = 255;
    if (spec[0] == '#') {
        for (n = 1; isxdigit((unsigned char) spec[n]); n++)
            ;
        n = (n - 1) / 3;
        if (n >= 1 && n <= 4) {
            int i;

            for (i = 0; i < 3; i++) {
                int v = hex_value(spec + 1 + i * n, n);

                /* the top 8 bits of each component */
                rgba[i] = (unsigned char) (n == 1 ? 17 * v : v >> (4 * (n - 2)));
            }
        }
    }
    else if (strncasecmp(spec, "none", 4) == 0)
        rgba[3] = 0;
    else if (strncasecmp(spec, "white", 5) == 0)
        rgba[0] = rgba[1] = rgba[2] = 255;
}

static int
pixmap_png(FILE *fd, HtmlBuf *b)
{
    char line[8192];
    int width, height, ncolours, cpp, i, x, y;
    char *keys;
    unsigned char *colours, *raw;

    if (xpm_string(fd, line, sizeof(line)) < 0
        || sscanf(line, "%d %d %d %d", &width, &height, &ncolours, &cpp) != 4
        || width <= 0 || height <= 0 || ncolours <= 0 || cpp <= 0 || cpp > 4
        || ncolours > 65536 || (long) width * height > MaxPixels
        || (long) width * cpp >= (long) sizeof(line))
        return -1;
    keys = (char *) halloc(ncolours * cpp, "XPM keys");
    colours = (unsigned char *) halloc(ncolours * 4, "XPM colours");
    for (i = 0; i < ncolours; i++) {
        char *c;

        if (xpm_string(fd, line, sizeof(line)) < cpp) {
            free(keys);
            free(colours);
            return -1;
        }
        memcpy(keys + i * cpp, line, cpp);
        /* the colour after the key " c " */
        for (c = line + cpp; *c && !(c[0] == 'c' && isspace((unsigned char) c[1])
                                     && isspace((unsigned char) c[-1])); c++)
            ;
        if (*c) {
            for (c++; isspace((unsigned char) *c); c++)
                ;
        }
        xpm_colour(c, colours + 4 * i);
    }
    raw = new_raw(width, height);
    for (y = 0; y < height; y++) {
        unsigned char *row = raw + (long) y * (1 + 4L * width) + 1;

        if (xpm_string(fd, line, sizeof(line)) < width * cpp)
            break;
        for (x = 0; x < width; x++) {
            for (i = 0; i < ncolours; i++)
                if (memcmp(keys + i * cpp, line + x * cpp, cpp) == 0)
                    break;
            if (i < ncolours)
                memcpy(row + 4 * x, colours + 4 * i, 4);
        }
    }
    write_png(b, raw, width, height);
    free(raw);
    free(keys);
    free(colours);
    return 0;
}

/* answer img/<n>.png */

void
html_image_response(const char *name, HttpResponse *r)
{
    char *end;
    long n = strtol(name, &end, 10);
    const char *file;
    const char *dot;
    FILE *fd;
    int ret;

    r->status = 404;
    if (end == name || strcmp(end, ".png") != 0 || n < 0 || n >= num_images)
        return;
    file = image_files[n];
    if ((fd = fopen(file, "r")) == NULL)
        return;
    dot = strrchr(file, '.');
    if (dot != NULL && strcmp(dot, ".xpm") == 0)
        ret = pixmap_png(fd, &r->body);
    else
        ret = bitmap_png(fd, &r->body);
    fclose(fd);
    if (ret == 0) {
        r->status = 200;
        r->content_type = "image/png";
    }
    else {
        fprintf(stderr, "(HyperDoc) cannot read picture %s\n", file);
        hb_free(&r->body);
        r->status = 404;
    }
}
