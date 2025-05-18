/* bank_udp_client.c – menu-driven UDP CLI
   build:  cc -std=c17 -Wall -Wextra -o bank_udp_cli bank_udp_client.c
   run  :  ./bank_udp_cli 127.0.0.1 8081
*/
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#define BUF 1024

static int send_recv(const char *host, const char *port, const char *msg, char *resp)
{
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};
    struct addrinfo *res;
    if (getaddrinfo(host, port, &hints, &res))
        return -1;
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0)
        return -1;
    sendto(s, msg, strlen(msg), 0, res->ai_addr, res->ai_addrlen);
    struct timeval tv = {2, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ssize_t n = recv(s, resp, BUF - 1, 0);
    if (n < 0)
    {
        close(s);
        return -1;
    }
    resp[n] = 0;
    close(s);
    return 0;
}

static void menu(void) { puts("\n1.Open 2.Deposit 3.Withdraw 4.Balance 5.Statement 6.Close 0.Quit"); }

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    const char *port = argc > 2 ? argv[2] : "8081";
    char line[BUF], resp[BUF];
    while (1)
    {
        menu();
        int c;
        if (scanf("%d", &c) != 1)
            break;
        unsigned acct, pin;
        double amt;
        char name[50], nat[20], type;
        switch (c)
        {
        case 1:
            puts("Name NatID Type(S/C) InitialDeposit:");
            scanf("%49s %19s %c %lf", name, nat, &type, &amt);
            snprintf(line, sizeof line, "open %s %s %c %.2f", name, nat, type, amt);
            break;
        case 2:
            puts("Acct PIN Amount:");
            scanf("%u %u %lf", &acct, &pin, &amt);
            snprintf(line, sizeof line, "deposit %u %u %.2f", acct, pin, amt);
            break;
        case 3:
            puts("Acct PIN Amount:");
            scanf("%u %u %lf", &acct, &pin, &amt);
            snprintf(line, sizeof line, "withdraw %u %u %.2f", acct, pin, amt);
            break;
        case 4:
            puts("Acct PIN:");
            scanf("%u %u", &acct, &pin);
            snprintf(line, sizeof line, "balance %u %u", acct, pin);
            break;
        case 5:
            puts("Acct PIN:");
            scanf("%u %u", &acct, &pin);
            snprintf(line, sizeof line, "statement %u %u", acct, pin);
            break;
        case 6:
            puts("Acct PIN:");
            scanf("%u %u", &acct, &pin);
            snprintf(line, sizeof line, "close %u %u", acct, pin);
            break;
        case 0:
            return 0;
        default:
            continue;
        }
        if (send_recv(host, port, line, resp))
            puts("no reply (packet lost?)");
        else
            puts(resp);
    }
}
