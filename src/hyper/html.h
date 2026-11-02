/*
 * html.h: declarations shared by the files of the browser variant of
 * HyperDoc (html.c, httpd.c, webmain.c).
 */

#ifndef _HTML_H_
#define _HTML_H_ 1

/* a growing string, for an HTTP response */

typedef struct {
    char *data;                 /* NUL terminated */
    int len;
    int size;
} HtmlBuf;

/* an HTTP response before it is sent */

typedef struct {
    int status;                 /* 200, 302, 404, ... */
    const char *content_type;
    const char *location;       /* for a redirect, relative URL */
    HtmlBuf body;
} HttpResponse;

/* from httpd.c: the listening socket of the HTTP server */
extern int httpd_socket;

#endif
