/*  bank_udp_server.c  – tiny-bank over UDP
    build:  cc -std=c17 -Wall -Wextra -o bank_udp_server bank_udp_server.c -lm
    run  :  ./bank_udp_server 8081   # port optional (default 8081)
*/
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <time.h>

/* ────── banking core (identical to earlier version) ────── */
#define ACCT_FILE "accounts.dat"
#define TXN_DIR "txns"
#define MIN_BALANCE 1000.0
#define MIN_IO 500.0

typedef struct
{
    unsigned number;
    char name[50];
    char nat_id[20];
    char type;
    unsigned pin;
    double balance;
    int active;
} Account;

static void ensure_dir(void)
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

static int find(unsigned n, Account *out, long *off)
{
    FILE *f = store("rb+");
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
static void save(const Account *a, long off)
{
    FILE *f = store("rb+");
    fseek(f, off, SEEK_SET);
    fwrite(a, sizeof *a, 1, f);
    fclose(f);
}
static void log_txn(unsigned acct, const char *k, double amt, double bal)
{
    char p[64];
    snprintf(p, sizeof p, TXN_DIR "/%u.log", acct);
    FILE *f = fopen(p, "a");
    if (!f)
        return;
    fprintf(f, "%ld|%s|%.2f|%.2f\n", time(NULL), k, amt, bal);
    fclose(f);
}

static unsigned open_account(const char *n, const char *id, char type, double dep, unsigned *pin)
{
    if (dep < MIN_BALANCE)
        return 0;
    ensure_dir();
    FILE *f = store("ab+");
    Account a = {0};
    fseek(f, 0, SEEK_END);
    a.number = (ftell(f) / sizeof(Account)) + 100001;
    strncpy(a.name, n, 49);
    strncpy(a.nat_id, id, 19);
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
    if (find(n, &a, &off) || a.pin != pin || !a.active)
        return -1;
    if (a.balance > MIN_BALANCE)
        return -2;
    a.active = 0;
    save(&a, off);
    log_txn(n, "CLOSE", 0, a.balance);
    return 0;
}
static int deposit(unsigned n, unsigned pin, double amt)
{
    if (amt < MIN_IO)
        return -3;
    Account a;
    long off;
    if (find(n, &a, &off) || a.pin != pin || !a.active)
        return -1;
    a.balance += amt;
    save(&a, off);
    log_txn(n, "CREDIT", amt, a.balance);
    return 0;
}
static int withdraw(unsigned n, unsigned pin, double amt)
{
    if (amt < MIN_IO || fmod(amt, MIN_IO) != 0)
        return -3;
    Account a;
    long off;
    if (find(n, &a, &off) || a.pin != pin || !a.active)
        return -1;
    if (a.balance - amt < MIN_BALANCE)
        return -4;
    a.balance -= amt;
    save(&a, off);
    log_txn(n, "DEBIT", amt, a.balance);
    return 0;
}
static double balance(unsigned n, unsigned pin)
{
    Account a;
    if (find(n, &a, 0) || a.pin != pin || !a.active)
        return -1;
    return a.balance;
}

/* send last 5 tx lines compressed */
static void statement(unsigned n, char *out, size_t cap)
{
    char p[64];
    snprintf(p, sizeof p, TXN_DIR "/%u.log", n);
    FILE *f = fopen(p, "r");
    if (!f)
    {
        strcpy(out, "[]");
        return;
    }
    char *lines[1024];
    size_t c = 0;
    char buf[128];
    while (fgets(buf, sizeof buf, f) && c < 1024)
        lines[c++] = strdup(buf);
    fclose(f);
    size_t i0 = c > 5 ? c - 5 : 0;
    size_t off = 0;
    off += snprintf(out + off, cap - off, "[");
    for (size_t i = i0; i < c; i++)
    {
        char *cpy = strdup(lines[i]);
        char *t = strtok(cpy, "|");
        char *k = strtok(NULL, "|");
        char *a = strtok(NULL, "|");
        char *b = strtok(NULL, "\n");
        off += snprintf(out + off, cap - off, "{%ld,%s,%s,%s}%s", atol(t), k, a, b, (i == c - 1) ? "]" : ",");
        free(cpy);
        free(lines[i]);
    }
}

/* ────── tiny dispatcher ────── */
#define BUF 1024
static void process(char *in, char *out)
{
    char cmd[16];
    if (sscanf(in, "%15s", cmd) != 1)
    {
        strcpy(out, "ERR 99 bad_cmd");
        return;
    }
    if (!strcmp(cmd, "open"))
    {
        char n[50], id[20], t;
        double dep;
        if (sscanf(in + 5, "%49s %19s %c %lf", n, id, &t, &dep) != 4)
        {
            strcpy(out, "ERR 98 bad_args");
            return;
        }
        unsigned pin, acct = open_account(n, id, t, dep, &pin);
        if (acct)
            sprintf(out, "OK %u %u", acct, pin);
        else
            strcpy(out, "ERR 1 open_fail");
    }
    else if (!strcmp(cmd, "deposit"))
    {
        unsigned a, p;
        double amt;
        if (sscanf(in + 8, "%u %u %lf", &a, &p, &amt) != 3)
        {
            strcpy(out, "ERR 98");
            return;
        }
        int r = deposit(a, p, amt);
        if (r)
            sprintf(out, "ERR %d", r);
        else
            strcpy(out, "OK");
    }
    else if (!strcmp(cmd, "withdraw"))
    {
        unsigned a, p;
        double amt;
        if (sscanf(in + 9, "%u %u %lf", &a, &p, &amt) != 3)
        {
            strcpy(out, "ERR 98");
            return;
        }
        int r = withdraw(a, p, amt);
        if (r)
            sprintf(out, "ERR %d", r);
        else
            strcpy(out, "OK");
    }
    else if (!strcmp(cmd, "balance"))
    {
        unsigned a, p;
        if (sscanf(in + 8, "%u %u", &a, &p) != 2)
        {
            strcpy(out, "ERR 98");
            return;
        }
        double b = balance(a, p);
        if (b < 0)
            strcpy(out, "ERR 2");
        else
            sprintf(out, "OK %.2f", b);
    }
    else if (!strcmp(cmd, "statement"))
    {
        unsigned a, p;
        if (sscanf(in + 10, "%u %u", &a, &p) != 2)
        {
            strcpy(out, "ERR 98");
            return;
        }
        if (balance(a, p) < 0)
        {
            strcpy(out, "ERR 2");
            return;
        }
        statement(a, out, BUF);
    }
    else if (!strcmp(cmd, "close"))
    {
        unsigned a, p;
        if (sscanf(in + 5, "%u %u", &a, &p) != 2)
        {
            strcpy(out, "ERR 98");
            return;
        }
        int r = close_account(a, p);
        if (r)
            sprintf(out, "ERR %d", r);
        else
            strcpy(out, "OK");
    }
    else
        strcpy(out, "ERR 99");
}

/* ────── main ────── */
int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 8081;
    srand(time(NULL));
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in srv = {.sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(port)};
    bind(s, (struct sockaddr *)&srv, sizeof srv);
    printf("UDP tiny-bank on %d\n", port);

    char in[BUF], out[BUF];
    while (1)
    {
        struct sockaddr_in cli;
        socklen_t clen = sizeof cli;
        ssize_t n = recvfrom(s, in, sizeof in - 1, 0, (struct sockaddr *)&cli, &clen);
        if (n <= 0)
            continue;
        in[n] = 0;
        process(in, out);
        sendto(s, out, strlen(out), 0, (struct sockaddr *)&cli, clen);
    }
}
