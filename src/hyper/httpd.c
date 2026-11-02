/*
 * httpd.c: the small HTTP server of the browser variant of HyperDoc
 * (configure option html).  It answers only the browser on the same
 * machine: it listens on 127.0.0.1, every URL starts with a random
 * token, the Host header must name this server, and requests have
 * fixed size limits.  One connection at a time, Connection: close.
 * Rules and routes: planning/step3-httpd.md.
 */

#include "fricas_c_macros.h"
#include "debug.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hyper.h"
#include "html.h"

#include "all_hyper_proto.H1"
#include "html.H1"
#include "httpd.H1"
#include "bsdsignal.h"
#include "bsdsignal.H1"

#define MaxHeaderSize   8192        /* request line and headers */
#define MaxBodySize     65536
#define RequestTimeout  5           /* seconds to send the request */
#define TokenBytes      16

int httpd_socket = -1;              /* the listening socket */

static int httpd_port;
static char access_token[2 * TokenBytes + 1];

/* read the token from the system's random source; no weak fallback */

static int
make_token(void)
{
    unsigned char bytes[TokenBytes];
    FILE *f = fopen("/dev/urandom", "rb");
    int i;

    if (f == NULL)
        return -1;
    if (fread(bytes, 1, TokenBytes, f) != TokenBytes) {
        fclose(f);
        return -1;
    }
    fclose(f);
    for (i = 0; i < TokenBytes; i++)
        sprintf(access_token + 2 * i, "%02x", bytes[i]);
    return 0;
}

/*
 * Open the listening socket on 127.0.0.1 with a port chosen by the
 * system, and print the URL of the root page.
 */

int
httpd_open(void)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    if (make_token() == -1) {
        fprintf(stderr, "(HyperDoc) cannot read /dev/urandom for the access token\n");
        return -1;
    }
    httpd_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (httpd_socket == -1) {
        perror("(HyperDoc) socket");
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(httpd_socket, (struct sockaddr *) &addr, sizeof(addr)) == -1
        || listen(httpd_socket, 8) == -1
        || getsockname(httpd_socket, (struct sockaddr *) &addr, &len) == -1) {
        perror("(HyperDoc) cannot listen on 127.0.0.1");
        close(httpd_socket);
        httpd_socket = -1;
        return -1;
    }
    httpd_port = ntohs(addr.sin_port);
    /* a browser that closes the connection early must not end HyperDoc */
    bsdSignal(SIGPIPE, SIG_IGN, RestartSystemCalls);
    fprintf(stderr, "(HyperDoc) http://127.0.0.1:%d/%s/\n", httpd_port, access_token);
    return httpd_socket;
}

static const char *
status_text(int status)
{
    switch (status) {
      case 200: return "OK";
      case 302: return "Found";
      case 400: return "Bad Request";
      case 403: return "Forbidden";
      case 404: return "Not Found";
      case 405: return "Method Not Allowed";
      case 408: return "Request Timeout";
      case 413: return "Content Too Large";
      case 431: return "Request Header Fields Too Large";
      case 501: return "Not Implemented";
      default:  return "Internal Server Error";
    }
}

static void
send_all(int fd, const char *s, int n)
{
    while (n > 0) {
        int k = send(fd, s, n, 0);

        if (k <= 0) {
            if (k == -1 && errno == EINTR)
                continue;
            return;
        }
        s += k;
        n -= k;
    }
}

static void
send_response(int fd, HttpResponse *r)
{
    char head[1024];
    int n;

    if (r->status != 200 && r->body.len == 0) {
        hb_add(&r->body, status_text(r->status));
        hb_add(&r->body, "\n");
        r->content_type = "text/plain; charset=utf-8";
    }
    n = snprintf(head, sizeof(head),
                 "HTTP/1.1 %d %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %d\r\n"
                 "%s%s%s"
                 "Connection: close\r\n"
                 "Cache-Control: no-store\r\n"
                 "Referrer-Policy: no-referrer\r\n"
                 "X-Content-Type-Options: nosniff\r\n"
                 "Content-Security-Policy: default-src 'self'\r\n"
                 "\r\n",
                 r->status, status_text(r->status),
                 r->content_type ? r->content_type : "text/plain; charset=utf-8",
                 r->body.len,
                 r->location ? "Location: " : "",
                 r->location ? r->location : "",
                 r->location ? "\r\n" : "");
    if (n > 0 && n < (int) sizeof(head)) {
        send_all(fd, head, n);
        send_all(fd, r->body.data, r->body.len);
    }
}

/*
 * Read from the connection until the headers are complete (and then
 * the body), within the size limits and the time limit.  Returns the
 * HTTP status for an error, 0 when the request is complete.
 */

static int
read_request(int fd, char *buf, int *len, int *head_len)
{
    time_t deadline = time(NULL) + RequestTimeout;
    int want = -1;              /* total length once the headers are read */

    *len = 0;
    *head_len = 0;
    while (want == -1 || *len < want) {
        fd_set rd;
        struct timeval tv;
        int k;

        tv.tv_sec = deadline - time(NULL);
        tv.tv_usec = 0;
        if (tv.tv_sec <= 0)
            return 408;
        FD_ZERO(&rd);
        FD_SET(fd, &rd);
        k = select(fd + 1, &rd, NULL, NULL, &tv);
        if (k == -1 && errno == EINTR)
            continue;
        if (k <= 0)
            return 408;
        k = recv(fd, buf + *len, MaxHeaderSize + MaxBodySize - *len, 0);
        if (k == -1 && errno == EINTR)
            continue;
        if (k <= 0)
            return 400;
        *len += k;
        buf[*len] = '\0';
        if (want == -1) {
            char *end = strstr(buf, "\r\n\r\n");
            char *cl;

            if (end == NULL) {
                if (*len >= MaxHeaderSize)
                    return 431;
                continue;
            }
            *head_len = end + 4 - buf;
            if (*head_len > MaxHeaderSize)
                return 431;
            want = *head_len;
            /* the body length, if any */
            for (cl = buf; (cl = strstr(cl, "\r\n")) != NULL && cl < end; cl += 2) {
                if (strncasecmp(cl + 2, "Content-Length:", 15) == 0) {
                    char *p = cl + 17;
                    long n;

                    while (*p == ' ' || *p == '\t')
                        p++;
                    if (*p < '0' || *p > '9')
                        return 400;
                    n = strtol(p, NULL, 10);
                    if (n > MaxBodySize)
                        return 413;
                    want = *head_len + (int) n;
                }
            }
        }
    }
    return 0;
}

/*
 * The value of header name (with colon) in the headers head, copied
 * into val; NULL if it is missing or too long.
 */

static char *
header_value(char *head, const char *name, char *val, int size)
{
    char *p = head;
    int n = strlen(name);

    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2;
        if (strncasecmp(p, name, n) == 0) {
            char *e = strstr(p, "\r\n");
            int k;

            p += n;
            while (*p == ' ' || *p == '\t')
                p++;
            k = e - p;
            while (k > 0 && (p[k - 1] == ' ' || p[k - 1] == '\t'))
                k--;
            if (k >= size)
                return NULL;
            memcpy(val, p, k);
            val[k] = '\0';
            return val;
        }
    }
    return NULL;
}

/* is host (a Host header) or origin (an Origin header) this server? */

static int
is_own_host(const char *host)
{
    char a[64], b[64];

    snprintf(a, sizeof(a), "127.0.0.1:%d", httpd_port);
    snprintf(b, sizeof(b), "localhost:%d", httpd_port);
    return strcmp(host, a) == 0 || strcmp(host, b) == 0;
}

static int
is_own_origin(const char *origin)
{
    return strncmp(origin, "http://", 7) == 0 && is_own_host(origin + 7);
}

/* the token, compared in time independent of where it differs */

static int
has_token(const char *path)
{
    int i, diff = 0;

    if (path[0] != '/' || strlen(path) < 2 * TokenBytes + 2)
        return 0;
    for (i = 0; i < 2 * TokenBytes; i++)
        diff |= path[i + 1] ^ access_token[i];
    return diff == 0 && path[2 * TokenBytes + 1] == '/';
}

/* decode %XX in place; -1 for a bad or a NUL escape */

static int
url_decode(char *s)
{
    char *d = s;

    for (; *s; s++) {
        if (*s == '%') {
            int v;

            if (!isxdigit((unsigned char) s[1]) || !isxdigit((unsigned char) s[2]))
                return -1;
            sscanf(s + 1, "%2x", &v);
            if (v == 0)
                return -1;
            *d++ = (char) v;
            s += 2;
        }
        else
            *d++ = *s;
    }
    *d = '\0';
    return 0;
}

static const char *
static_content_type(const char *name)
{
    const char *dot = strrchr(name, '.');

    if (dot == NULL)
        return "application/octet-stream";
    if (strcmp(dot, ".css") == 0)
        return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0)
        return "text/javascript; charset=utf-8";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".svg") == 0)
        return "image/svg+xml";
    return "application/octet-stream";
}

/* a file of $FRICAS/share/hypertex/www; plain names only */

static void
static_file(const char *name, HttpResponse *r)
{
    char path[1024];
    const char *p;
    char *fricas = getenv("FRICAS");
    FILE *f;
    int c;

    r->status = 404;
    if (fricas == NULL || name[0] == '\0' || name[0] == '.')
        return;
    for (p = name; *p; p++)
        if (!isalnum((unsigned char) *p) && *p != '.' && *p != '_' && *p != '-')
            return;
    if (snprintf(path, sizeof(path), "%s/share/hypertex/www/%s", fricas, name)
        >= (int) sizeof(path))
        return;
    if ((f = fopen(path, "rb")) == NULL)
        return;
    while ((c = getc(f)) != EOF) {
        char ch = (char) c;

        hb_addn(&r->body, &ch, 1);
    }
    fclose(f);
    r->status = 200;
    r->content_type = static_content_type(name);
}

/* answer one request: checks, then the route */

static void
handle_request(char *buf, int head_len, HttpResponse *r)
{
    char *method, *path, *version, *line_end, *query;
    char val[256];
    int is_post;

    /* end the headers after the "\r\n" of the last one */
    buf[head_len - 2] = '\0';
    line_end = strstr(buf, "\r\n");
    *line_end = '\0';
    method = buf;
    if ((path = strchr(method, ' ')) == NULL) {
        r->status = 400;
        return;
    }
    *path++ = '\0';
    if ((version = strchr(path, ' ')) == NULL) {
        r->status = 400;
        return;
    }
    *version++ = '\0';
    if (strncmp(version, "HTTP/1.", 7) != 0) {
        r->status = 400;
        return;
    }
    /* the header search needs the "\r\n" in front of the first header */
    line_end[0] = '\r';
    if (header_value(line_end, "Host:", val, sizeof(val)) == NULL || !is_own_host(val)) {
        r->status = 403;
        return;
    }
    is_post = strcmp(method, "POST") == 0;
    if (!is_post && strcmp(method, "GET") != 0) {
        r->status = 405;
        return;
    }
    if (is_post && header_value(line_end, "Origin:", val, sizeof(val)) != NULL
        && !is_own_origin(val)) {
        r->status = 403;
        return;
    }
    if (!has_token(path)) {
        r->status = 404;
        return;
    }
    path += 2 * TokenBytes + 2;
    if ((query = strchr(path, '?')) != NULL)
        *query++ = '\0';
    if (url_decode(path) == -1) {
        r->status = 400;
        return;
    }
    if (is_post) {
        /* actions: step 5; the body starts at buf + head_len */
        (void) head_len;
        r->status = 501;
    }
    else if (path[0] == '\0') {
        r->status = 302;
        r->location = "page/RootPage";
    }
    else if (strncmp(path, "page/", 5) == 0)
        html_page_response(path + 5, r);
    else if (strncmp(path, "static/", 7) == 0)
        static_file(path + 7, r);
    else
        r->status = 404;
}

/* accept one connection on the listening socket and answer it */

void
httpd_serve(void)
{
    static char buf[MaxHeaderSize + MaxBodySize + 1];
    HttpResponse r;
    int fd, len, head_len;

    fd = accept(httpd_socket, NULL, NULL);
    if (fd == -1)
        return;
    memset(&r, 0, sizeof(r));
    r.status = read_request(fd, buf, &len, &head_len);
    if (r.status == 0) {
        r.status = 200;
        handle_request(buf, head_len, &r);
    }
    send_response(fd, &r);
    hb_free(&r.body);
    close(fd);
}
