#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h> 
#include <math.h>

#define ACCT_FILE "accounts.dat"
#define TXN_DIR "txns"
#define MIN_BALANCE 1000.0
#define MIN_IO 500.0

/*──────────────────────────────── Account persistence ─────────────────────────*/
typedef struct
{
    unsigned int number;
    char name[50];
    char nat_id[20];
    char type;        /* 'S' or 'C' */
    unsigned int pin; /* 4-digit */
    double balance;
    int active;
} Account;

static void ensure_txn_dir(void)
{
    if (access(TXN_DIR, F_OK) != 0)
        mkdir(TXN_DIR, 0700);
}

static FILE *open_store(const char *mode)
{
    FILE *f = fopen(ACCT_FILE, mode);
    if (!f && (mode[0] == 'r' || mode[0] == 'a'))
        f = fopen(ACCT_FILE, "wb+");
    return f;
}

static int find_acct(unsigned int n, Account *out, long *off)
{
    FILE *f = open_store("rb+");
    if (!f)
        return -1;
    Account a;
    long pos = 0;
    while (fread(&a, sizeof a, 1, f) == 1)
    {
        if (a.number == n)
        {
            if (out)
                *out = a;
            if (off)
                *off = pos;
            fclose(f);
            return 0;
        }
        pos += sizeof a;
    }
    fclose(f);
    return -1;
}
static void save_acct(const Account *a, long off)
{
    FILE *f = open_store("rb+");
    fseek(f, off, SEEK_SET);
    fwrite(a, sizeof *a, 1, f);
    fflush(f);
    fclose(f);
}
static void log_txn(unsigned int acct, const char *kind, double amt, double bal)
{
    char p[64];
    snprintf(p, sizeof p, TXN_DIR "/%u.log", acct);
    FILE *f = fopen(p, "a");
    if (!f)
        return;
    fprintf(f, "%ld|%s|%.2f|%.2f\n", time(NULL), kind, amt, bal);
    fclose(f);
}
/*────────────────────────── business API (same as before) ─────────────────────*/
static unsigned int open_account(const char *n, const char *id, char type, double dep, unsigned *pin)
{
    if (dep < MIN_BALANCE)
        return 0;
    ensure_txn_dir();
    FILE *f = open_store("ab+");
    if (!f)
        return 0;
    Account a = {0};
    fseek(f, 0, SEEK_END);
    a.number = (ftell(f) / sizeof(Account)) + 100001;
    strncpy(a.name, n, sizeof a.name - 1);
    strncpy(a.nat_id, id, sizeof a.nat_id - 1);
    a.type = (type == 'C') ? 'C' : 'S';
    a.pin = (rand() % 9000) + 1000;
    a.balance = dep;
    a.active = 1;
    fwrite(&a, sizeof a, 1, f);
    fclose(f);
    log_txn(a.number, "OPEN", dep, a.balance);
    if (pin)
        *pin = a.pin;
    return a.number;
}
static int close_account(unsigned n, unsigned pin)
{
    Account a;
    long off;
    if (find_acct(n, &a, &off))
        return -1;
    if (!a.active || a.pin != pin)
        return -2;
    if (a.balance > MIN_BALANCE)
        return -3;
    a.active = 0;
    save_acct(&a, off);
    log_txn(n, "CLOSE", 0, a.balance);
    return 0;
}
static int withdraw_cash(unsigned n, unsigned pin, double amt)
{
    if (amt < MIN_IO || fmod(amt, MIN_IO) != 0)
        return -2;
    Account a;
    long off;
    if (find_acct(n, &a, &off))
        return -1;
    if (!a.active || a.pin != pin)
        return -1;
    if (a.balance - amt < MIN_BALANCE)
        return -3;
    a.balance -= amt;
    save_acct(&a, off);
    log_txn(n, "DEBIT", amt, a.balance);
    return 0;
}
static int deposit_cash(unsigned n, unsigned pin, double amt)
{
    if (amt < MIN_IO)
        return -2;
    Account a;
    long off;
    if (find_acct(n, &a, &off))
        return -1;
    if (!a.active || a.pin != pin)
        return -1;
    a.balance += amt;
    save_acct(&a, off);
    log_txn(n, "CREDIT", amt, a.balance);
    return 0;
}
static double get_balance(unsigned n, unsigned pin)
{
    Account a;
    if (find_acct(n, &a, NULL))
        return -1;
    if (!a.active || a.pin != pin)
        return -1;
    return a.balance;
}
/* last 5 txns streamed to client */
static void stream_statement(int fd, unsigned n)
{
    char p[64];
    snprintf(p, sizeof p, TXN_DIR "/%u.log", n);
    FILE *f = fopen(p, "r");
    if (!f)
    {
        dprintf(fd, "[]");
        return;
    }
    char *lines[1024];
    size_t cnt = 0;
    char buf[128];
    while (fgets(buf, sizeof buf, f) && cnt < 1024)
        lines[cnt++] = strdup(buf);
    fclose(f);
    dprintf(fd, "[");
    for (size_t i = (cnt > 5 ? cnt - 5 : 0); i < cnt; i++)
    {
        char *cpy = strdup(lines[i]);
        char *t = strtok(cpy, "|");
        char *kind = strtok(NULL, "|");
        char *amt = strtok(NULL, "|");
        char *bal = strtok(NULL, "\n");
        dprintf(fd, "{\"time\":%ld,\"kind\":\"%s\",\"amt\":%s,\"bal\":%s}%s",
                atol(t), kind, amt, bal, (i == cnt - 1) ? "" : ",");
        free(cpy);
        free(lines[i]);
    }
    dprintf(fd, "]");
}
/*──────────────────────────────── URL helpers ──────────────────────────*/
typedef struct
{
    char k[16];
    char v[64];
} kv;
static int parse_qs(char *qs, kv out[], int cap)
{
    int c = 0;
    char *tok = strtok(qs, "&");
    while (tok && c < cap)
    {
        char *eq = strchr(tok, '=');
        if (eq)
        {
            *eq = 0;
            strncpy(out[c].k, tok, 15);
            strncpy(out[c].v, eq + 1, 63);
            c++;
        }
        tok = strtok(NULL, "&");
    }
    return c;
}
static const char *val(kv *p, int n, const char *key)
{
    for (int i = 0; i < n; i++)
        if (strcmp(p[i].k, key) == 0)
            return p[i].v;
    return NULL;
}
/*──────────────────────────── request dispatcher ───────────────────────*/
static void handle_client(int fd)
{
    char req[1024] = {0};
    read(fd, req, sizeof req - 1);
    char method[8], url[256];
    sscanf(req, "%7s %255s", method, url);
    char *qs = strchr(url, '?');
    if (qs)
    {
        *qs++ = 0;
    }
    kv params[16];
    int pc = qs ? parse_qs(qs, params, 16) : 0;

    /* routing */
    if (strcmp(url, "/open") == 0)
    {
        const char *name = val(params, pc, "name");
        const char *nat = val(params, pc, "nat");
        const char *type = val(params, pc, "type");
        double dep = atof(val(params, pc, "dep") ?: "0");
        unsigned pin;
        unsigned acct = open_account(name ?: "", nat ?: "", type ? *type : 'S', dep, &pin);
        dprintf(fd, "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n");
        if (acct)
            dprintf(fd, "{\"acct\":%u,\"pin\":%u}\n", acct, pin);
        else
            dprintf(fd, "{\"error\":\"cannot open\"}\n");
    }
    else if (strcmp(url, "/close") == 0)
    {
        unsigned acct = atoi(val(params, pc, "acct") ?: "0");
        unsigned pin = atoi(val(params, pc, "pin") ?: "0");
        int r = close_account(acct, pin);
        dprintf(fd, "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n{\"result\":%d}\n", r);
    }
    else if (strcmp(url, "/withdraw") == 0)
    {
        unsigned acct = atoi(val(params, pc, "acct") ?: "0");
        unsigned pin = atoi(val(params, pc, "pin") ?: "0");
        double amt = atof(val(params, pc, "amt") ?: "0");
        int r = withdraw_cash(acct, pin, amt);
        dprintf(fd, "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n{\"result\":%d}\n", r);
    }
    else if (strcmp(url, "/deposit") == 0)
    {
        unsigned acct = atoi(val(params, pc, "acct") ?: "0");
        unsigned pin = atoi(val(params, pc, "pin") ?: "0");
        double amt = atof(val(params, pc, "amt") ?: "0");
        int r = deposit_cash(acct, pin, amt);
        dprintf(fd, "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n{\"result\":%d}\n", r);
    }
    else if (strcmp(url, "/balance") == 0)
    {
        unsigned acct = atoi(val(params, pc, "acct") ?: "0");
        unsigned pin = atoi(val(params, pc, "pin") ?: "0");
        double b = get_balance(acct, pin);
        dprintf(fd, "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n{\"balance\":%.2f}\n", b);
    }
    else if (strcmp(url, "/statement") == 0)
    {
        unsigned acct = atoi(val(params, pc, "acct") ?: "0");
        unsigned pin = atoi(val(params, pc, "pin") ?: "0");
        if (get_balance(acct, pin) < 0)
        { /* quick auth check */
            dprintf(fd, "HTTP/1.0 403\r\n\r\n");
        }
        else
        {
            dprintf(fd, "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n");
            stream_statement(fd, acct);
        }
    }
    else
    {
        dprintf(fd, "HTTP/1.0 404\r\n\r\n");
    }
    close(fd);
}
/*────────────────────────────── listener loop ──────────────────────────*/
static int mk_server(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(port)};
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 10) < 0)
    {
        perror("bind/listen");
        exit(1);
    }
    return s;
}
int main(int argc, char **argv)
{
    int port = (argc > 1) ? atoi(argv[1]) : 8080;
    srand(time(NULL));
    int srv = mk_server(port);
    printf("tiny-bank listening on %d\n", port);
    while (1)
    {
        int c = accept(srv, NULL, NULL);
        if (c < 0)
            continue;
        if (!fork())
        {
            close(srv);
            handle_client(c);
            exit(0);
        }
        close(c);
    }
}
