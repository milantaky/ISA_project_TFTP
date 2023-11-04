#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>         // sockets duh
#include <netinet/in.h>         // ???
// #include <errno.h>           // ???
#include <signal.h>             // interrupt
#include <time.h>               // na timeout?
#include <pcap/pcap.h>
#include <arpa/inet.h>          // htons
#include <net/ethernet.h>       // ???

enum{
    RRQ = 1,
    WRQ,
    DATA,
    ACK,
    ERROR
} tftp_opcode;

enum{
    NOT_DEFINED,
    FILE_NOT_FOUND,
    ACCESS_VIOLATION,
    DISK_FULL_OR_ALLOCATION_EXCEEDED,
    ILLEGAL_TFTP_OPERATION,
    UNKNOWN_TRANSFER_ID,
    FILE_ALREADY_EXISTS,
    NO_SUCH_USER
} tftp_error_code;

int zkontrolujANastavArgumenty(int pocet, char* argv[], int* port, const char* cesta[]){
    /*  
    [] = volitelny
    tftp-server [-p port] root_dirpath

        -p místní port, na kterém bude server očekávat příchozí spojení
        cesta k adresáři, pod kterým se budou ukládat příchozí soubory      */
    
    if(pocet == 2 && strcmp(argv[1], "--help") == 0){
        printf("tftp-server [-p port] root_dirpath\n\n -p místní port, na kterém bude server očekávat příchozí spojení\ncesta k adresáři, pod kterým se budou ukládat příchozí soubory\n");
        return 0;
    }
    
    if(!(pocet == 2 || pocet == 4)){
        fprintf(stderr, "CHYBA: Spatny pocet argumentu argumentu.\n       Zadejte prosim prikaz ve tvaru: tftp-server [-p port] root_dirpath\n");
        return 0;
    } 


    if(pocet == 4){ // I s portem - Zkontroluje se a nastavi argument portu
        *cesta = argv[3];

        if(strcmp(argv[1], "-p") != 0){
            fprintf(stderr, "CHYBA: Spatne zadany parametr. Je zde ocekavano -p namisto %s.\n", argv[1]);
            return 0;
        }

        if(!((*port = atoi(argv[2])) && *port >= 0 && *port < 65536)){       // Je cislo, a v rozsahu 0 - 65535
            fprintf(stderr, "CHYBA: Zadany port neni cislo, nebo v rozsahu 0 - 65535\n");
            return 0;
        }
    } else {
        *cesta = argv[1];
    }


    return 1;
}


//===============================================================================================================================

int main(int argc, char* argv[]){

    int port = 69;                  // Defaultne posloucha tady
    const char* cesta = NULL;

    // Kontrola argumentu
    if(!zkontrolujANastavArgumenty(argc, argv, &port, &cesta)){
        return 1;     // Chyba vypsana ve funkci
    }

    printf("port: %d\ncesta: %s\n", port, cesta);
    
    // sehnat adresu serveru
    // handle bude poslouchat na en0/eth0 asi s filtrem kde je adresa serveru
    // pak se to zpracuje

    // zatim asi chytat ve while !interrupt (C^c) ve dvou terminalech
    // SOCKETY

    // RANDOM TID asi pada, kdyz chteji port ne????????


    // int sockfd = socket(AF_INET, SOCK_DGRAM, 0);    // AF_INET = IPv4, SOCK_DGRAM = UDP, 0 = IP protocol
    
    // if(sockfd < 0){
    //     fprintf(stderr, "CHYBA pri otevirani socketu.\n");
    //     return 1;
    // }


    // // SOCK BIND
    // // LISTEN
    // // new socket = accept
    // // send(new_socket, hello, strlen(hello), 0);

    // close(sockfd);
    return 0;
}
