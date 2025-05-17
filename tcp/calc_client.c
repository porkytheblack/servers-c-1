#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#define HOST "127.0.0.1"
#define PORT "9000"
#define BUFSZ 4096

static int http_get(const char *path, char *out, size_t len) {
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res;
    if (getaddrinfo(HOST, PORT, &hints, &res)) return -1;
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0 || connect(s, res->ai_addr, res->ai_addrlen) < 0) { freeaddrinfo(res); return -1; }
    freeaddrinfo(res);
    dprintf(s, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, HOST);

    int hdr = 1; size_t off = 0; char buf[BUFSZ]; ssize_t n;
    while ((n = read(s, buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (hdr && i < n - 3 && !strncmp(&buf[i], "\r\n\r\n", 4)) { hdr = 0; i += 3; continue; }
            if (!hdr && off < len - 1) out[off++] = buf[i];
        }
    }
    out[off] = 0; close(s); return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s {add|sub|mul|div} intA intB\n", argv[0]);
        return 1;
    }
    const char *op = argv[1], *x = argv[2], *y = argv[3];
    char path[128];
    snprintf(path, sizeof path, "/%s?x=%s&y=%s", op, x, y);

    char body[BUFSZ];
    if (http_get(path, body, sizeof body)) { perror("net"); return 1; }

    /* crude: either {"result":N} or {"error":"msg"} */
    char *r = strstr(body, "\"result\":");
    if (r) {
        double v = atof(r + 9);
        printf("%g\n", v);
    } else {
        printf("%s\n", body);   /* show full error JSON */
        return 2;
    }
    return 0;
}
