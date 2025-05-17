#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#define BUFSZ 1024

/* ───── tiny query-string helper ───────────────────────────── */
typedef struct { char k[8]; char v[32]; } kv;
static int parse_qs(char *qs, kv arr[], int cap) {
    int n = 0; char *tok = strtok(qs, "&");
    while (tok && n < cap) {
        char *eq = strchr(tok, '=');
        if (eq) { *eq = 0; strncpy(arr[n].k, tok, 7); strncpy(arr[n].v, eq+1, 31); n++; }
        tok = strtok(NULL, "&");
    }
    return n;
}
static const char *getv(kv *a, int n, const char *k) {
    for (int i = 0; i < n; i++) if (!strcmp(a[i].k, k)) return a[i].v;
    return NULL;
}

/* ───── HTTP dispatcher ───────────────────────────────────── */
static void handle(int fd) {
    char buf[BUFSZ] = {0};
    read(fd, buf, BUFSZ-1);

    char method[8], path[128];
    if (sscanf(buf, "%7s %127s", method, path) != 2) { close(fd); return; }

    char *qs = strchr(path, '?');
    if (qs) *qs++ = 0;

    kv params[8]; int pc = qs ? parse_qs(qs, params, 8) : 0;
    const char *sx = getv(params, pc, "x");
    const char *sy = getv(params, pc, "y");

    long x = sx ? strtol(sx, NULL, 10) : 0;
    long y = sy ? strtol(sy, NULL, 10) : 0;
    int bad = (!sx || !sy);

    double res = 0;

    if (!strcmp(path, "/add"))       res = x + y;
    else if (!strcmp(path, "/sub"))  res = x - y;
    else if (!strcmp(path, "/mul"))  res = x * y;
    else if (!strcmp(path, "/div")) {
        if (y == 0) bad = 2; else res = (double)x / (double)y;
    }
    else bad = 3;

    dprintf(fd, "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n");
    if (bad == 0)
        dprintf(fd, "{\"result\":%.6f}\n", res);
    else if (bad == 2)
        dprintf(fd, "{\"error\":\"divide_by_zero\"}\n");
    else
        dprintf(fd, "{\"error\":\"bad_request\"}\n");

    close(fd);
}

/* ───── listen/accept loop ────────────────────────────────── */
static int make_srv(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY,
                             .sin_port = htons(port) };
    if (bind(s, (struct sockaddr*)&a, sizeof a) || listen(s, 10)) {
        perror("bind/listen"); exit(1);
    }
    return s;
}
int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 9000;
    int srv = make_srv(port);
    printf("calc-server on %d\n", port);

    while (1) {
        int c = accept(srv, NULL, NULL);
        if (c < 0) continue;
        if (!fork()) { close(srv); handle(c); exit(0); }
        close(c);
    }
}
