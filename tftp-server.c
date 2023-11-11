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
// - Alokuje dest
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

// Vypise chybovou zpravu ve tvaru ERROR {SRC_IP}:{SRC_PORT}:{DST_PORT} {CODE} "{MESSAGE}"
void vypisError(char buffer[], struct sockaddr_in client, struct sockaddr_in server){

    // Dalo by se to i vlozit do fprintf, ale takhle je to prehlednejsi
    char *srcIP  = inet_ntoa(client.sin_addr);
    int srcPort = ntohs(client.sin_port); 
    int dstPort = ntohs(server.sin_port); 
    int code = (int) buffer[3];
    char message[256];
    strcpy(message, buffer + 4);

    fprintf(stderr, "ERROR %s:%d:%d %d \"%s\"\n", srcIP, srcPort, dstPort, code, message);
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

// Vypise zpravu ve tvaru ACK {SRC_IP}:{SRC_PORT} {BLOCK_ID}
void vypisACK(struct sockaddr_in client, int blockID){

    char *srcIP  = inet_ntoa(client.sin_addr);
    int srcPort = ntohs(client.sin_port); 

    fprintf(stderr, "ACK %s:%d %d\n", srcIP, srcPort, blockID);
}

// Vypise zpravu ve tvaru DATA {SRC_IP}:{SRC_PORT}:{DST_PORT} {BLOCK_ID}
void vypisData(struct sockaddr_in client, struct sockaddr_in server, int blockID){
    
    char *srcIP  = inet_ntoa(client.sin_addr);
    int srcPort = ntohs(client.sin_port); 
    int dstPort = ntohs(server.sin_port); 

    fprintf(stderr, "DATA %s:%d:%d %d\n", srcIP, srcPort, dstPort, blockID);
}

// Vypise zpravu ve tvaru RRQ {SRC_IP}:{SRC_PORT} "{FILEPATH}" {MODE} {$OPTS}
//                   nebo WRQ {SRC_IP}:{SRC_PORT} "{FILEPATH}" {MODE} {$OPTS}
// !!!!!!!!!!!!!!!!!!!!DODELAT options
void vypisRequest(struct sockaddr_in client, int mode, char filepath[], int request){
    
    char *srcIP  = inet_ntoa(client.sin_addr);
    int srcPort = ntohs(client.sin_port); 
    char modeC[10];

    if(mode == 2){
        strcpy(modeC, "octet");
    } else {
        strcpy(modeC, "netascii");
    }

    if(request == 1){
        fprintf(stderr, "RRQ %s:%d \"%s\" %s\n", srcIP, srcPort, filepath, modeC);
    } 
    else {
        fprintf(stderr, "WRQ %s:%d \"%s\" %s\n", srcIP, srcPort, filepath, modeC);
    }
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

    // Kontola souboru pred otevrenim
    if(access(location, F_OK)){     // Existence
        posliErrorPacket(sockfd, client, 1, "File not found.");
        return 0;
    }

    if(access(location, R_OK)){     // Prava
        posliErrorPacket(sockfd, client, 2, "Access violation.");
        return 0;
    }

    readFile = (mode == 2) ? fopen(location, "rb") : fopen(location, "r");

    if(readFile == NULL){
        fprintf(stderr, "Nastala CHYBA pri otevirani souboru pro cteni: %s\n", strerror(errno));
        close(sockfd);
        return 0;
    }

    int finished = 0;
    int resend   = 0;
    int bytes    = 0;
    int readBytes;
    char response[MAX_BUFFER_SIZE];
    char backup[MAX_DATA_SIZE + 4];
    memset(response, 0 , MAX_BUFFER_SIZE);              
    memset(backup, 0 , MAX_DATA_SIZE + 4);           

    while(!finished && !resend){                                    // Dokud to neni hotove a nemusi se nic odesilat znovu
        if(!resend){                                                // Naplnit novy
            buffer[2] = blockNumber >> 8;
            buffer[3] = blockNumber;

            bytes = 0;
            memset(helpBuffer, 0 , MAX_DATA_SIZE);                  // Vynuluje buffer
            char c = getc(readFile);
            while(c != EOF && bytes < MAX_DATA_SIZE){               // Plneni - Trosku neusporny
                if(c == '\n' && bytes < MAX_DATA_SIZE - 1){
                    helpBuffer[bytes] = '\r';
                    bytes++;
                }
                helpBuffer[bytes] = c;
                bytes++;
                if(bytes != MAX_DATA_SIZE) c = getc(readFile);
            }

            if(c == EOF) finished = 1;

            strncpy(buffer + 4, helpBuffer, MAX_DATA_SIZE);         // Slozeni packetu
            strncpy(backup, buffer, MAX_DATA_SIZE + 4);             // Zaloha
            if(!posliPacket(sockfd, buffer, bytes + 4, client)) return 0;
        } 
        else {    // Nastala chyba, je potreba odeslat znovu
            if(!posliPacket(sockfd, backup, bytes + 4, client)) return 0;
            resend = 0;
        }

        // Prijimani ACKu
        readBytes = recvfrom(sockfd, response, MAX_BUFFER_SIZE, 0, (struct sockaddr*) &client, &clientAddressLength);

        if(readBytes == -1){
            fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
            close(sockfd);
            return 0; 
        } 
        
        if(response[0] == 0 && response[1] == 4){
            int lastACK = ((int)buffer[2] << 8) + (int)buffer[3];
            vypisACK(client, blockNumber);

            if(lastACK != blockNumber)     // Posli znovu -> ACKnut minuly blok
                resend = 1;
            else
                blockNumber++; 
        }
        else {
            fprintf(stderr, "CHYBA: Neocekavany packet ocekavan ACK.\n");
            close(sockfd);
            return 0; 
        }
    }

    fclose(readFile);
    return 1;

}

// Zpracuje WRITE -> Z pohledu serveru: Otevre soubor a posila data pakcety
//                -> Pri mode netascii zpracovava \r a \n
// !!!!!!!!!!!!!!! je potreba dodelat timeout
// int zpracujWrite(int sockfd, struct sockaddr_in server, struct sockaddr_in client, char location[], int mode, char prvniBuffer[], int delkaPrvniho){
int zpracujWrite(int sockfd, struct sockaddr_in server, struct sockaddr_in client, char location[], int mode){

    FILE* file;
    socklen_t clientAddressLength = sizeof(client);
    int blockNumber = 0;
    int finished    = 0;
    int readBytes;

    char data[MAX_DATA_SIZE + 4];
    memset(data, 0, MAX_DATA_SIZE + 4);

    // Kontola souboru pred otevrenim
    if(!access(location, F_OK)){     // Existence
        posliErrorPacket(sockfd, client, 6, "File already exists.");
        return 0;
    }

    // 2 == octet 1 == netascii 
    file = (mode == 2) ? fopen(location, "wb") : fopen(location, "w");

    if(file == NULL){
        fprintf(stderr, "Nastala CHYBA pri vytvareni souboru.\n");
        close(sockfd);  // Client se timeoutne
        return 0;
    }

    if(!posliACK(sockfd, client, blockNumber)) return 0;

    // Chytani DATA packetu
    while(!finished){
        memset(data, 0, MAX_DATA_SIZE + 4);
        readBytes = recvfrom(sockfd, data, MAX_DATA_SIZE + 4, 0, (struct sockaddr*) &client, &clientAddressLength);

        if(readBytes == -1){
            fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
            close(sockfd);
            return 0; 
        }

        blockNumber++;
        vypisData(client, server, blockNumber);

        // Kontrola prijatych dat
        if(data[1] == 3){
            int lastBlockID = ((int)data[2] << 8) + (int)data[3];
            if (lastBlockID == blockNumber - 1){                // Prisla stejna data -> ztratil se ACK
                if(!posliACK(sockfd, client, lastBlockID)) return 0;

                continue;
            }
        } 
        else {
            if(data[1] == 5) vypisError(data, client, server);
        }

        // Zpracovani data packetu
        if(readBytes < MAX_DATA_SIZE + 4) finished = 1;             // Posledni DATA packet

        for(int i = 4; i < readBytes; i++){
            if(mode == 1 && data[i] == '\n' && data[i - 1] != '\r'){
                if(fputc('\r', file) == EOF){                              // Zacinam na indexu 4, muzu si to dovolit
                    posliErrorPacket(sockfd, client, 3, "Disk full or allocation exceeded.");
                    return 0;
                } 
            }

            if(fputc(data[i], file) == EOF){                              // Zacinam na indexu 4, muzu si to dovolit
                posliErrorPacket(sockfd, client, 3, "Disk full or allocation exceeded.");
                return 0;
            } 
        }

        if(!posliACK(sockfd, client, blockNumber)) return 0;
    }

    fclose(file);
    return 1;
}

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

    printf("CESTA: %s\n", cesta);

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
    char *location = NULL;                          // cesta kam se budou ukladat soubory / odkud se budou cist -> zalezi na mode (root_dirpath)

    int readBytes;
    readBytes = recvfrom(sockfd, buffer, MAX_BUFFER_SIZE, 0, (struct sockaddr*) &client, &clientAddressLength);

    if(readBytes == -1){
        fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
        close(sockfd);
        return 1; 
    } 
    
    // ZPRACOVANI REQUESTU
    if(buffer[0] != 0){                
        fprintf(stderr, "CHYBA: Chybny TFTP packet.\n");
        posliErrorPacket(sockfd, client, 4, "Illegal TFTP operation.");
        return 1;
    } 

    nactiLokaci(buffer, &location);      
    offset = 3 + (int) strlen(location);            // 2 kvuli opcode a 1 je \0 za lokaci

    if(!(mode = zkontrolujMode(buffer, offset))){          // 2 == octet 1 == netascii            
        fprintf(stderr, "CHYBA: Spatny mode requestu. Zvolte pouze netascii/octet.\n");
        posliErrorPacket(sockfd, client, 4, "Illegal TFTP operation.");
        free(location);
        return 1;
    }

    if(buffer[1] == 1){                 // READ
        printf("dostal jsem packet pro cteni\n");
        vypisRequest(client, mode, location, 1);
        if(!zpracujRead(sockfd, client, location, mode)){
            fprintf(stderr, "Nastala CHYBA pri zpracovani requestu.\n");
            free(location);
            return 1;
        }
    } 
    else if(buffer[1] == 2){            // WRITE
        printf("dostal jsem packet pro zapis\n");
        
        // Uprava cesty
        char cestaNew[(int) strlen(cesta) + 1];
        char cestaWrite[(int)(strlen(location) + strlen(cesta)) + 1];
    
        for(int i = 0; i < (int) strlen(cesta); i++){
            cestaNew[i] = cesta[i];
        }
        
        cestaNew[(int) strlen(cesta)] = '\0';
    
        strcpy(cestaWrite, cestaNew);
        if(location[0] == '.'){
            strcpy(cestaWrite + (int)(strlen(cesta)), location + 1);
        } else {
            strcpy(cestaWrite + (int)(strlen(cesta)), location);
        }

        vypisRequest(client, mode, location, 2);

        // if(!zpracujWrite(sockfd, server, client, cestaWrite, mode, buffer, readBytes)){
        if(!zpracujWrite(sockfd, server, client, cestaWrite, mode)){
            fprintf(stderr, "Nastala CHYBA pri zpracovani requestu WRITE.\n");
            free(location);
            return 1;
        }

        
    } 
    else {
        fprintf(stderr, "CHYBA: Nejprve je potreba zaslat request packet.\n");
        posliErrorPacket(sockfd, client, 0, "Nejprve je potreba zaslat request packet.");
        free(location);
        return 1;
    }
    

    // while(!interrupt){  // !!!!!!! loopuje to tady pri na interruptu
    // }

    // Accept request -> write = ACK a block number = 0
    //                ->  read = prvni blok dat


    free(location);
    close(sockfd);
    return 0;
}
