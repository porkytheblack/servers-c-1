#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#define HOST "127.0.0.1"
#define PORT "8080"
#define BUFSZ 8192

/* ───── helper: fire-and-forget GET ───────────────────────────── */
static int http_get(const char *path, char *body, size_t blen) {
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res;
    if (getaddrinfo(HOST, PORT, &hints, &res) != 0) return -1;
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0 || connect(s, res->ai_addr, res->ai_addrlen) < 0) { freeaddrinfo(res); return -1; }
    freeaddrinfo(res);

    dprintf(s, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, HOST);

    /* skip headers, copy body */
    char buf[BUFSZ]; size_t off = 0; int hdr = 1;
    ssize_t n;
    while ((n = read(s, buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (hdr && i < n - 3 && !strncmp(&buf[i], "\r\n\r\n", 4)) {
                hdr = 0; i += 3; continue;
            }
            if (!hdr && off < blen - 1) body[off++] = buf[i];
        }
    }
    body[off] = 0; close(s); return 0;
}

/* ───── main menu ─────────────────────────────────────────────── */
static void menu(void) {
    puts("\n1.Open 2.Deposit 3.Withdraw 4.Balance 5.Statement 6.Close 0.Quit");
}

int main(void) {
    char r[BUFSZ];
    while (1) {
        menu(); int c; if (scanf("%d", &c) != 1) break;

        unsigned acct, pin; double amt; char name[50], nat[20], type;
        char url[256];

        switch (c) {
        case 1:
            puts("Name NatID Type(S/C) InitialDeposit:");
            scanf("%49s %19s %c %lf", name, nat, &type, &amt);
            snprintf(url, sizeof url, "/open?name=%s&nat=%s&type=%c&dep=%.2f",
                     name, nat, type, amt);
            http_get(url, r, sizeof r); puts(r); break;
        case 2:
            puts("Acct PIN Amount:");
            scanf("%u %u %lf", &acct, &pin, &amt);
            snprintf(url, sizeof url, "/deposit?acct=%u&pin=%u&amt=%.2f", acct, pin, amt);
            http_get(url, r, sizeof r); puts(r); break;
        case 3:
            puts("Acct PIN Amount:");
            scanf("%u %u %lf", &acct, &pin, &amt);
            snprintf(url, sizeof url, "/withdraw?acct=%u&pin=%u&amt=%.2f", acct, pin, amt);
            http_get(url, r, sizeof r); puts(r); break;
        case 4:
            puts("Acct PIN:");
            scanf("%u %u", &acct, &pin);
            snprintf(url, sizeof url, "/balance?acct=%u&pin=%u", acct, pin);
            http_get(url, r, sizeof r); puts(r); break;
        case 5:
            puts("Acct PIN:");
            scanf("%u %u", &acct, &pin);
            snprintf(url, sizeof url, "/statement?acct=%u&pin=%u", acct, pin);
            http_get(url, r, sizeof r); puts(r); break;
        case 6:
            puts("Acct PIN:");
            scanf("%u %u", &acct, &pin);
            snprintf(url, sizeof url, "/close?acct=%u&pin=%u", acct, pin);
            http_get(url, r, sizeof r); puts(r); break;
        case 0:  return 0;
        default: puts("?");
        }
    }
    return 0;
}
