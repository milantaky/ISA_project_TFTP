#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>         // sockets duh
// #include <netinet/in.h>      // ???
// #include <errno.h>           // ???
#include <signal.h>             // interrupt
#include <time.h>               // na timeout?
#include <pcap/pcap.h>
#include <arpa/inet.h>          // htons
#include <net/ethernet.h>       // ???

int main(int argc, char* argv[]){
    printf("%d %s\n", argc, argv[0]);
    printf("Hello world\n");
    return 0;
}
