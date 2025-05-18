/********************************************************************
 *  bank_service_server.c  – concurrent, connection-oriented server *
 *  resolved “through services” (getservbyname).                    *
 *******************************************************************/
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/stat.h>

/* ---------- banking core (exactly the same as earlier) ---------- */
#define ACCT_FILE "accounts.dat"
#define TXN_DIR "txns"
#define MIN_BALANCE 1000.0
#define MIN_IO 500.0

typedef struct
{
    unsigned num;
    char name[50], id[20], type;
    unsigned pin;
    double bal;
    int active;
} Acct;
static void need_dir(void)
{
    if (access(TXN_DIR, F_OK))
        mkdir(TXN_DIR, 0700);
}
static FILE *store(const char *m)
{
    FILE *f = fopen(ACCT_FILE, m);
    if (!f && m[0] != 'w')
        f = fopen(ACCT_FILE, "wb+");
    return f;
}

static int find(unsigned n, Acct *out, long *off)
{
    FILE *f = store("rb+");
    Acct a;
    long pos = 0;
    while (fread(&a, sizeof a, 1, f) == 1)
    {
        if (a.num == n)
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
static void save(const Acct *a, long off)
{
    FILE *f = store("rb+");
    fseek(f, off, SEEK_SET);
    fwrite(a, sizeof *a, 1, f);
    fclose(f);
}
static void logt(unsigned acc, const char *k, double amt, double bal)
{
    char p[64];
    snprintf(p, sizeof p, TXN_DIR "/%u.log", acc);
    FILE *f = fopen(p, "a");
    if (!f)
        return;
    fprintf(f, "%ld|%s|%.2f|%.2f\n", time(NULL), k, amt, bal);
    fclose(f);
}

static unsigned open_acc(const char *n, const char *id, char t, double dep, unsigned *pin)
{
    if (dep < MIN_BALANCE)
        return 0;
    need_dir();
    FILE *f = store("ab+");
    Acct a = {0};
    fseek(f, 0, SEEK_END);
    a.num = (ftell(f) / sizeof a) + 100001;
    strncpy(a.name, n, 49);
    strncpy(a.id, id, 19);
    a.type = (t == 'C') ? 'C' : 'S';
    a.pin = (rand() % 9000) + 1000;
    a.bal = dep;
    a.active = 1;
    fwrite(&a, sizeof a, 1, f);
    fclose(f);
    logt(a.num, "OPEN", dep, a.bal);
    if (pin)
        *pin = a.pin;
    return a.num;
}
static int close_acc(unsigned n, unsigned pin)
{
    Acct a;
    long off;
    if (find(n, &a, &off) || a.pin != pin || !a.active)
        return -1;
    if (a.bal > MIN_BALANCE)
        return -2;
    a.active = 0;
    save(&a, off);
    logt(n, "CLOSE", 0, a.bal);
    return 0;
}
static int deposit(unsigned n, unsigned pin, double amt)
{
    if (amt < MIN_IO)
        return -3;
    Acct a;
    long off;
    if (find(n, &a, &off) || a.pin != pin || !a.active)
        return -1;
    a.bal += amt;
    save(&a, off);
    logt(n, "CREDIT", amt, a.bal);
    return 0;
}
static int withdraw(unsigned n, unsigned pin, double amt)
{
    if (amt < MIN_IO || fmod(amt, MIN_IO) != 0)
        return -3;
    Acct a;
    long off;
    if (find(n, &a, &off) || a.pin != pin || !a.active)
        return -1;
    if (a.bal - amt < MIN_BALANCE)
        return -4;
    a.bal -= amt;
    save(&a, off);
    logt(n, "DEBIT", amt, a.bal);
    return 0;
}
static double get_bal(unsigned n, unsigned pin)
{
    Acct a;
    if (find(n, &a, 0) || a.pin != pin || !a.active)
        return -1;
    return a.bal;
}

/* compact 5-txn summary */
static void stmt(unsigned n, FILE *out)
{
    char p[64];
    snprintf(p, sizeof p, TXN_DIR "/%u.log", n);
    FILE *fp = fopen(p, "r");
    if (!fp)
    {
        fprintf(out, "[]\n");
        return;
    }
    char *L[1024];
    size_t c = 0;
    char buf[128];
    while (fgets(buf, 128, fp) && c < 1024)
        L[c++] = strdup(buf);
    fclose(fp);
    size_t i0 = c > 5 ? c - 5 : 0;
    fputc('[', out);
    for (size_t i = i0; i < c; i++)
    {
        char *cpy = strdup(L[i]);
        char *t = strtok(cpy, "|");
        char *k = strtok(NULL, "|");
        char *a = strtok(NULL, "|");
        char *b = strtok(NULL, "\n");
        fprintf(out, "{%ld,%s,%s,%s}%s", atol(t), k, a, b, (i == c - 1) ? "]\n" : ",");
        free(cpy);
        free(L[i]);
    }
}

/* ---------- child: interact line-by-line with client ---------- */
static void session(int sock)
{
    FILE *fp = fdopen(sock, "r+");
    if (!fp)
    {
        close(sock);
        exit(1);
    }
    char cmd[16];
    while (fscanf(fp, "%15s", cmd) == 1)
    {
        if (!strcmp(cmd, "open"))
        {
            char n[50], id[20], t;
            double d;
            if (fscanf(fp, "%49s %19s %c %lf", n, id, &t, &d) != 4)
            {
                fprintf(fp, "ERR bad\n");
                break;
            }
            unsigned pin, ac = open_acc(n, id, t, d, &pin);
            if (ac)
                fprintf(fp, "OK %u %u\n", ac, pin);
            else
                fprintf(fp, "ERR open\n");
        }
        else if (!strcmp(cmd, "deposit"))
        {
            unsigned a, p;
            double amt;
            if (fscanf(fp, "%u %u %lf", &a, &p, &amt) != 3)
            {
                break;
            }
            int r = deposit(a, p, amt);
            fprintf(fp, r ? "ERR %d\n" : "OK\n", r);
        }
        else if (!strcmp(cmd, "withdraw"))
        {
            unsigned a, p;
            double amt;
            if (fscanf(fp, "%u %u %lf", &a, &p, &amt) != 3)
            {
                break;
            }
            int r = withdraw(a, p, amt);
            fprintf(fp, r ? "ERR %d\n" : "OK\n", r);
        }
        else if (!strcmp(cmd, "balance"))
        {
            unsigned a, p;
            if (fscanf(fp, "%u %u", &a, &p) != 2)
            {
                break;
            }
            double b = get_bal(a, p);
            if (b < 0)
                fprintf(fp, "ERR\n");
            else
                fprintf(fp, "OK %.2f\n", b);
        }
        else if (!strcmp(cmd, "statement"))
        {
            unsigned a, p;
            if (fscanf(fp, "%u %u", &a, &p) != 2)
            {
                break;
            }
            if (get_bal(a, p) < 0)
            {
                fprintf(fp, "ERR\n");
                continue;
            }
            fprintf(fp, "OK ");
            stmt(a, fp);
        }
        else if (!strcmp(cmd, "close"))
        {
            unsigned a, p;
            if (fscanf(fp, "%u %u", &a, &p) != 2)
            {
                break;
            }
            int r = close_acc(a, p);
            fprintf(fp, r ? "ERR %d\n" : "OK\n", r);
        }
        else
        {
            fprintf(fp, "ERR badcmd\n");
        }
        fflush(fp);
    }
    fclose(fp);
}

/* ---------- main: create listening socket on service “bankcs/tcp” -------- */
int main(void)
{
    signal(SIGCHLD, SIG_IGN); /* reap zombies */
    struct servent *se = getservbyname("bankcs", "tcp");
    int port = se ? ntohs(se->s_port) : 9090; /* fallback */
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM, .ai_flags = AI_PASSIVE};
    struct addrinfo *res;
    char pstr[8];
    snprintf(pstr, sizeof pstr, "%d", port);
    getaddrinfo(NULL, pstr, &hints, &res);
    int s = socket(res->ai_family, res->ai_socktype, 0);
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    bind(s, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    listen(s, 16);
    printf("bankcs listening on port %d\n", port);
    srand(time(NULL));

    while (1)
    {
        int cli = accept(s, NULL, NULL);
        if (cli < 0)
        {
            perror("accept");
            continue;
        }
        if (!fork())
        {
            close(s);
            session(cli);
            exit(0);
        }
        close(cli);
    }
}
