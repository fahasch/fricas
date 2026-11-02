/*
Copyright (c) 1991-2002, The Numerical ALgorithms Group Ltd.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.

    - Neither the name of The Numerical ALgorithms Group Ltd. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "fricas_c_macros.h"
#include "debug.h"
#include "hyper.h"

#include "all_hyper_proto.H1"
#include "pixmap.H1"


/*
 * This file was produced by J.M. Wiley with some help from the bitmap editor
 * routine. It reads in a bitmap file, and calls XCreatePixmapFromBitmapData
 * to transform it into a Pixmap. He did this because the routine
 * XReadBitmapFile does not seem to work too well (whatever that means).
 */

XImage *
HTReadBitmapFile(Display *display,int screen,char * filename,
                 int *width, int *height)
{
    XImage *image;
    FILE *fd;


    image = XCreateImage(display, DefaultVisual(display, screen), 1,
                         XYBitmap, 0, NULL, 0, 0, 8, 0);


    (image)->byte_order = LSBFirst;     /* byte_order    */
    (image)->bitmap_unit = 8;   /* bitmap-unit   */
    (image)->bitmap_bit_order = LSBFirst;       /* bitmap-bit-order */

    if (!(fd = zzopen(filename, "r"))) {
        fprintf(stderr, "ReadBitmapFile: File >%s< not found\n", filename);
        exit(-1);
    }
    (image)->data = read_bitmap_data(fd, width, height);
    if ((image)->data == NULL) {
        fprintf(stderr, "ReadBitmapFile: Bad file format in %s\n", filename);
        exit(-1);
    }
    (image)->width = *width;
    (image)->height = *height;
    (image)->bytes_per_line = (*width + 7) / 8;

    fclose(fd);

    return image;
}

/* read a bitmap file into memory */

ImageStruct *
insert_image_struct(char *filename)
{
    int bm_width, bm_height;
    XImage *im;
    ImageStruct *image;

    if (*filename == ' ')
        filename++;
    if ((image = (ImageStruct *) hash_find(&gImageHashTable, filename)) == NULL) {
        im = HTReadBitmapFile(gXDisplay, gXScreenNumber, filename,
                              &bm_width, &bm_height);

        /*
         * now add the image to the gImageHashTable
         */

        image = (ImageStruct *) halloc(sizeof(ImageStruct), "ImageStruct");
        image->image.xi = im;
        image->width = image->image.xi->width;
        image->height = image->image.xi->height;
        image->filename = (char *) halloc(sizeof(char) * strlen(filename) +1,
                                          "insert_image--filename");

        /* strcpy(image->filename, filename); */

        sprintf(image->filename, "%s", filename);
        hash_insert(&gImageHashTable,(char *) image, image->filename);
    }
    return image;
}
