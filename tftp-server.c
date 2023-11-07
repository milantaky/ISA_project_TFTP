#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>         // sockets duh
#include <netinet/in.h>      
#include <netinet/ip.h>       
#include <netinet/udp.h>      
#include <errno.h>           // ???
#include <signal.h>             // interrupt
#include <time.h>               // na timeout?
#include <pcap/pcap.h>
#include <arpa/inet.h>          // htons
#include <net/ethernet.h>       // ???
#include <unistd.h>

#define MAX_BUFFER_SIZE 1024

// int interrupt = 0;
// void intHandler() {
//     interrupt = 1;
// }


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

    //signal(SIGINT, intHandler);
    int port = 69;                  // Defaultne posloucha tady
    const char* cesta = NULL;
    char buffer[MAX_BUFFER_SIZE];              // !!!!!!!! Zatim 1024
    memset(buffer, 0 , MAX_BUFFER_SIZE);       // Vynuluje buffer

    // Kontrola argumentu
    if(!zkontrolujANastavArgumenty(argc, argv, &port, &cesta)){
        return 1;     // Chyba vypsana ve funkci
    }

    printf("port: %d\ncesta: %s\n", port, cesta);
    
    // handle bude poslouchat na en0/eth0 asi s filtrem kde je adresa serveru
    

    struct sockaddr_in server, client;
    server.sin_family        = AF_INET;
    server.sin_port          = htons(port);
    server.sin_addr.s_addr   = INADDR_ANY;

    // printf("PORTTTTTT: %d\n", (int) server.sin_port);

    // char ipStr[INET_ADDRSTRLEN];
    // if (inet_ntop(AF_INET, &(server.sin_addr), ipStr, sizeof(ipStr)) != NULL) {
    //     printf("IP Address: %s\n", ipStr);
    // } else {
    //     printf("inet_ntop");
    // }

    int readBytes;

    // fork() ??? pak upravit listen na delsi frontu nez 1

    // SOCKETY
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);    // AF_INET = IPv4, SOCK_DGRAM = UDP, 0 = IP protocol

    if(sockfd < 0){
        fprintf(stderr, "CHYBA pri otevirani socketu.\n");
        return 1;
    }

    // SOCKET BIND
    if(bind(sockfd, (struct sockaddr*) &server, sizeof(server)) < 0){
        fprintf(stderr, "Nastala CHYBA pri bindovani socketu.\n");
        close(sockfd);
        return 1;
    }

    // LISTEN
    // if(listen(sockfd, 3) < 0){
    //     fprintf(stderr, "Nastala CHYBA pri cekani na klienta. %s\n", strerror(errno));
    //     close(sockfd);
    //     return 1;
    // }

    // ACCEPT
    //int clientSock;
    // socklen_t serverAddressLength = sizeof(server);
    socklen_t clientAddressLength = sizeof(client);

    // if((clientSock = accept(sockfd, (struct sockaddr*) &server, &serverAddressLength)) < 0){
    //     fprintf(stderr, "Nastala CHYBA pri prijimani klienta. %s\n", strerror(errno));
    //     close(sockfd);
    //     return 1;
    // }

    // CTENI
    
    printf("Cekam na klienta...\n");

    while(1){
        // readBytes = recvfrom(clientSock, buffer, MAX_BUFFER_SIZE, 0, (struct sockaddr*) &client, &clientAddressLength);
        readBytes = recvfrom(sockfd, buffer, MAX_BUFFER_SIZE, 0, (struct sockaddr*) &client, &clientAddressLength);

        if(readBytes == -1){
            fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
            close(sockfd);
            return 1; 
        }

        printf("%x%x", buffer[0], buffer[1]);
        for(int i = 2; i < readBytes; i++){
            if(buffer[i] == '\n'){
                printf("X");
            } else {
                printf("%c", buffer[i]);
            }
        }
        printf("\n");
        break;
    }

    // // send(new_socket, hello, strlen(hello), 0);
    //close(clientSock);
    close(sockfd);
    return 0;
}
