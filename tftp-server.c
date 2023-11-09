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
#include <ctype.h>

#define MAX_BUFFER_SIZE 1024

int interrupt = 0;


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

// Funkce pro zpracovani interrupt signalu
void intHandler(int signum) {
    (void)signum;       // Musi to byt takhle, jinak compiler nadava
    interrupt = 1;
}

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

// Prevede cely string na lowercase 
char toLowerString(char string[]){
    for(int i = 0; i < (int) strlen(string); i++){
        string[i] = tolower(string[i]);
    }
    
    return *string;
}

// Zkontroluje mode -> 2 = octet, 1 = netascii, 0 = chyba
int zkontrolujMode(char buffer[], int offset){
    char mode[10];
    int i = 0;
    int j = offset;

    char c = buffer[j];

    while(c != '\0'){
        if(i > 9){      // Delsi nez netascii
            return 0;
        }
        mode[i] = c;
        i++;
        j++;
        c = buffer[j];
    }
    
    mode[i] = '\0';
    toLowerString(mode);

    if(strcmp(mode, "octet") == 0){
        return 2;
    } else {
        if(strcmp(mode, "netascii") == 0){
            return 1;
        }
    }

    return 0;
}

// Nacte lokaci (filename) z bufferu do dest
void nactiLokaci(char buffer[], char *dest[]){
    char lokace[300] = {0};
    int i = 2;
    int j = 0;
    char c = buffer[i];
    
    while(c != '\0'){
        lokace[j] = c;
        i++;
        j++;
        c = buffer[i];
    }
    
    lokace[j] = '\0';
    
    *dest = strdup(lokace);
    // *dest = lokace;
}

int posliErrorPacket(int sockfd, struct sockaddr_in client, int errorCode, char message[]){
    int packetLength = 5 + (int) strlen(message);    // 2 opcode, 2 errcode, \0
    char errorPacket[packetLength];
    
    errorPacket[0] = 0;
    errorPacket[1] = 5;
    errorPacket[2] = 0;         // Tady si to muzu dovolit -> errorCode je 0-7
    errorPacket[3] = errorCode;
    
    strcpy(errorPacket + 4, message);
    errorPacket[packetLength] = '\0';
    // Tady je packet ready

    // Posilani

    int bytesSent;
    bytesSent = sendto(sockfd, errorPacket, packetLength, 0, (struct sockaddr*) &client, sizeof(client));
    
    if(bytesSent == -1){
        fprintf(stderr, "Nastala CHYBA pri posilani packetu.\n");
        return 0;
    }

    printf("OOOOOOOOOODESLANO\n");
    return 1;
}

//===============================================================================================================================

int main(int argc, char* argv[]){

    signal(SIGINT, intHandler);
    int port = 69;                              // Defaultne posloucha tady
    const char* cesta = NULL;

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

    // CEKANI NA REQUEST A CTENI
    printf("Cekam na klienta...\n");

    socklen_t clientAddressLength = sizeof(client);
    char buffer[MAX_BUFFER_SIZE];                   // !!!!!!!! Zatim 1024
    char sendBuffer[MAX_BUFFER_SIZE];       
    memset(buffer, 0 , MAX_BUFFER_SIZE);            // Vynuluje buffer
    memset(sendBuffer, 0 , MAX_BUFFER_SIZE);        // Vynuluje buffer
    //int blockNumber = 0;
    int mode = 0;                                   // 1 = netascii, 2 = octet
    int offset;
    char *location = NULL;                          // cesta kam se budou ukladat soubory / odkud se budou cist -> zalezi na mode

    readBytes = recvfrom(sockfd, buffer, MAX_BUFFER_SIZE, 0, (struct sockaddr*) &client, &clientAddressLength);

    if(readBytes == -1){
        fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
        close(sockfd);
        return 1; 
    } 
    else {                                  // ZPRACOVANI REQUESTU
        if(buffer[0] != 0){                 // Spatny packet
            fprintf(stderr, "CHYBA: Obdrzeny packet neni TFTP packet.\n");
            posliErrorPacket(sockfd, client, 0, "Obdrzeny packet neni TFTP packet.");
            return 1;
        } 
        else {
            nactiLokaci(buffer, &location);      
            printf("location: %s\n", location);

            offset = 3 + (int) strlen(location);            // 2 kvuli opcode a 1 je \0 za lokaci
            free(location);

            if(!(mode = zkontrolujMode(buffer, offset))){     
                printf("%d\n", mode);                               
                fprintf(stderr, "CHYBA: Spatny mode requestu. Zvolte pouze netascii/octet.\n");
                posliErrorPacket(sockfd, client, 0, "Spatny request mode");
                return 1;
            }

            if(buffer[1] == 1){             // READ
                printf("dostal jsem packet pro cteni\n");

            } 
            else if(buffer[1] == 2){         // WRITE
                printf("dostal jsem packet pro zapis\n");
                


            } else {
                fprintf(stderr, "CHYBA: Nejprve je potreba zaslat request packet.\n");
                posliErrorPacket(sockfd, client, 0, "Nejprve je potreba zaslat request packet.");
                return 1;
            }
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

    }



    // while(!interrupt){  // !!!!!!! loopuje to tady pri na interruptu
    // }

    // Accept request -> write = ACK a block number = 0
    //                ->  read = ACK a prvni blok dat


    close(sockfd);
    return 0;
}
