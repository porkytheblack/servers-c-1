#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

int main(void) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in addr = { .sin_family = AF_INET,
                                .sin_addr.s_addr = INADDR_ANY,
                                .sin_port = htons(9001) };
    bind(s, (struct sockaddr*)&addr, sizeof addr);
    puts("UDP calc on 9001");

    while (1) {
        char buf[128]; struct sockaddr_in cli; socklen_t len = sizeof cli;
        int n = recvfrom(s, buf, sizeof buf-1, 0,
                         (struct sockaddr*)&cli, &len);
        if (n <= 0) continue;
        buf[n] = 0;

        /* expected packet: "op x y" e.g. "add 3 4" */
        char op[8]; long a,b; if (sscanf(buf,"%7s %ld %ld",op,&a,&b)!=3) continue;
        double res = !strcmp(op,"add")?a+b:
                     !strcmp(op,"sub")?a-b:
                     !strcmp(op,"mul")?a*b:
                     (!strcmp(op,"div") && b)?(double)a/b:0;

        char out[64];
        snprintf(out,sizeof out,"%.6f",res);
        sendto(s,out,strlen(out),0,(struct sockaddr*)&cli,len);
    }
}
