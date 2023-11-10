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
#include <arpa/inet.h>          // htons
#include <net/ethernet.h>       // ???
#include <unistd.h>
#include <ctype.h>

#define MAX_BUFFER_SIZE 1024
#define MAX_DATA_SIZE 512

int interrupt = 0;

// Funkce pro zpracovani interrupt signalu
void intHandler(int signum) {
    (void)signum;       // Musi to byt takhle, jinak compiler nadava
    interrupt = 1;
}

// Zkontroluje a nastavi argumenty -> duh
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
}

// Odesle packet
int posliPacket(int sockfd, char buffer[], int size, struct sockaddr_in client){
    int bytesSent;
    bytesSent = sendto(sockfd, buffer, size, 0, (struct sockaddr*) &client, sizeof(client));

   if(bytesSent == -1){                                   
        fprintf(stderr, "Nastala CHYBA pri posilani packetu.\n");
        close(sockfd);
        return 0;
    } 

    printf("OOOOOOOOOODESLANO\n");
    return 1;
}

// Posle error packet
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
    if(!posliPacket(sockfd, errorPacket, packetLength, client)){
        return 0;
    }

    return 1;
}

// Posle ACK packet
int posliACK(int sockfd, struct sockaddr_in client, int blockNumber){
    char ack[4];

    ack[0] = 0;
    ack[1] = 4;
    ack[2] = blockNumber >> 8;
    ack[3] = blockNumber;

    if(!posliPacket(sockfd, ack, 4, client)){
        return 0;
    }

    return 1;
}

// Zpracuje READ -> Z pohledu serveru: Otevre soubor a posila data pakcety
// !!!!!!!!!!!!!!! je potreba dodelat timeout
int zpracujRead(int sockfd, struct sockaddr_in client, char location[], int mode){

    FILE *readFile;
    socklen_t clientAddressLength = sizeof(client);
    int blockNumber = 1;
    char buffer[MAX_DATA_SIZE + 4];                 // 512 + opcode + block number
    char helpBuffer[MAX_DATA_SIZE];                 // 512 + opcode + block number
    memset(buffer, 0 , MAX_DATA_SIZE + 4);          // Vynuluje buffer
    memset(helpBuffer, 0 , MAX_DATA_SIZE);          // Vynuluje buffer

    buffer[0] = 0;
    buffer[1] = 3;
    buffer[2] = blockNumber >> 8;
    buffer[3] = blockNumber;

    // Otevre se soubor z location podle mode
    if(mode == 2){                          // octet
        readFile = fopen(location, "rb");   // read binary

        if(readFile == NULL){
            fprintf(stderr, "Nastala CHYBA pri otevirani souboru pro cteni: %s\n", strerror(errno));
            close(sockfd);
            return 0;
        }

        // int lastACK = 0;
        int finished = 0;
        int resend   = 0;
        int bytes    = 0;
        int readBytes;
        char response[MAX_BUFFER_SIZE];
        char backup[MAX_DATA_SIZE + 4];
        memset(response, 0 , MAX_BUFFER_SIZE);              
        memset(backup, 0 , MAX_DATA_SIZE + 4);           

    //======================
        while(!finished && !resend){                            // Dokud to neni hotove a nemusi se nic odesilat znovu
            if(!resend){                                        // Naplnit novy
                printf("-------- Vyrabim packet\n");
                buffer[2] = blockNumber >> 8;
                buffer[3] = blockNumber;

                bytes = 0;
                memset(helpBuffer, 0 , MAX_DATA_SIZE);          // Vynuluje buffer
                char c = getc(readFile);
                while(c != EOF && bytes < MAX_DATA_SIZE){      // Plneni - Trosku neusporny
                    helpBuffer[bytes] = c;
                    bytes++;
                    if(bytes != MAX_DATA_SIZE){
                        c = getc(readFile);
                    }
                }

                printf("bytes: %d\n", bytes);

                if(c == EOF) finished = 1;

                strncpy(buffer + 4, helpBuffer, MAX_DATA_SIZE);         // Slozeni packetu
                strncpy(backup, buffer, MAX_DATA_SIZE + 4);     // Zaloha
                printf("-------- Odesilam packet\n");
                if(!posliPacket(sockfd, buffer, bytes + 4, client)){  
                    return 0;
                }
            } 
            else {    // Nastala chyba, je potreba odeslat znovu
                printf("-------- Znovu posilam packet\n");
                if(!posliPacket(sockfd, backup, bytes + 4, client)){  
                    return 0;
                }
                resend = 0;
            }

            // Prijimani ACKu
            readBytes = recvfrom(sockfd, response, MAX_BUFFER_SIZE, 0, (struct sockaddr*) &client, &clientAddressLength);

            if(readBytes == -1){
                fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
                close(sockfd);
                return 0; 
            } 
            
            printf("------------------------ OK\n");
            //blockNumber++;
            if(response[0] == 0 && response[1] == 4){
                int lastACK = ((int)buffer[2] << 8) + (int)buffer[3];

                if(lastACK != blockNumber){     // Posli znovu
                    printf("------------ Posilam znova\n");
                    resend = 1;
                } else {
                    blockNumber++;
                }
            }
            
        }
    //====================



        fclose(readFile);
    } else {                                // netascii
        readFile = fopen(location, "r");    // read

    }



    // naplni se buffer
    // odesle se 
    // zvysi se block #
    // ceka se na ACK
    // Opakuje se dokud je co cist

    return 1;

}

// // Zpracuje WRITE -> Z pohledu serveru: Vytvori soubor a naplni ho
// int zpracujWrite(){

// }

//===============================================================================================================================
// ./tftp-server /WRITE

int main(int argc, char* argv[]){

    signal(SIGINT, intHandler);
    int port = 69;                              // Defaultne posloucha tady
    const char* cesta = NULL;

    // Kontrola argumentu
    if(!zkontrolujANastavArgumenty(argc, argv, &port, &cesta)){
        return 1;    
    }

    struct sockaddr_in server, client;
    server.sin_family        = AF_INET;
    server.sin_port          = htons(port);
    server.sin_addr.s_addr   = INADDR_ANY;

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
    // int blockNumber = 0;
    int mode = 0;                                   // 1 = netascii, 2 = octet
    int offset;
    char *location = NULL;                          // cesta kam se budou ukladat soubory / odkud se budou cist -> zalezi na mode

    int readBytes;
    readBytes = recvfrom(sockfd, buffer, MAX_BUFFER_SIZE, 0, (struct sockaddr*) &client, &clientAddressLength);

    if(readBytes == -1){
        fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
        close(sockfd);
        return 1; 
    } 
    else {                                  // ZPRACOVANI REQUESTU
        if(buffer[0] != 0){                
            fprintf(stderr, "CHYBA: Chybny TFTP packet.\n");
            posliErrorPacket(sockfd, client, 0, "Chybny TFTP packet.");
            close(sockfd);
            return 1;
        } 
        else {
            nactiLokaci(buffer, &location);      
            printf("location: %s\n", location);

            offset = 3 + (int) strlen(location);            // 2 kvuli opcode a 1 je \0 za lokaci

            if(!(mode = zkontrolujMode(buffer, offset))){                    
                fprintf(stderr, "CHYBA: Spatny mode requestu. Zvolte pouze netascii/octet.\n");
                posliErrorPacket(sockfd, client, 0, "Spatny request mode.");
                close(sockfd);
                return 1;
            }

            if(buffer[1] == 1){                 // READ
                printf("dostal jsem packet pro cteni\n");
                //posliACK(sockfd, client, blockNumber);
                zpracujRead(sockfd, client, location, mode);
                free(location);
            } 
            else if(buffer[1] == 2){            // WRITE
                printf("dostal jsem packet pro zapis\n");
                
            } 
            else {
                fprintf(stderr, "CHYBA: Nejprve je potreba zaslat request packet.\n");
                posliErrorPacket(sockfd, client, 0, "Nejprve je potreba zaslat request packet.");
                close(sockfd);
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
    //                ->  read = prvni blok dat


    close(sockfd);
    return 0;
}
