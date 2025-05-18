/* udp_calc_cli.c  usage: ./udp_calc_cli add 3 4 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

int main(int c,char**v){
    if(c!=4){fprintf(stderr,"usage: %s op x y\n",v[0]);return 1;}
    int s=socket(AF_INET,SOCK_DGRAM,0);
    struct sockaddr_in srv={.sin_family=AF_INET,.sin_port=htons(9001)};
    inet_pton(AF_INET,"127.0.0.1",&srv.sin_addr);

    char pkt[64]; snprintf(pkt,sizeof pkt,"%s %s %s",v[1],v[2],v[3]);
    sendto(s,pkt,strlen(pkt),0,(struct sockaddr*)&srv,sizeof srv);

    char buf[64]; int n=recv(s,buf,sizeof buf-1,0);
    buf[n]=0; puts(buf);
}
