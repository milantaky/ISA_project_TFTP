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
#define MAX_DATA_SIZE 512

int interrupt = 0;

// Zpracovani interruptu
void intHandler(int signum) {
    (void)signum;   // musi to byt takhle, jinak compiler nadava
    interrupt = 1;
}

// Zkontroluje a nastavi argumenty -> duh
int zkontrolujANastavArgumenty(int pocet, char* argv[], int* port, const char* hostname[], const char* filepath[], const char* dest_filepath[]){
    /*  
    [] = volitelny
    tftp-client -h hostname [-p port] [-f filepath] -t dest_filepath

        -h IP adresa/doménový název vzdáleného serveru
        -p port vzdáleného serveru
        pokud není specifikován předpokládá se výchozí dle specifikace
        -f cesta ke stahovanému souboru na serveru (download)
        pokud není specifikován používá se obsah stdin (upload)
        -t cesta, pod kterou bude soubor na vzdáleném serveru/lokálně uložen      */

    if(pocet == 2 && strcmp(argv[1], "--help") == 0){
        printf("tftp-client -h hostname [-p port] [-f filepath] -t dest_filepath\n\n-h IP adresa/doménový název vzdáleného serveru\n-p port vzdáleného serveru\npokud není specifikován předpokládá se výchozí dle specifikace\n-f cesta ke stahovanému souboru na serveru (download)\npokud není specifikován používá se obsah stdin (upload)\n-t cesta, pod kterou bude soubor na vzdáleném serveru/lokálně uložen.\n");
        return 0;
    }
    
    if(!(pocet == 5 || pocet == 7 || pocet == 9)){
        fprintf(stderr, "CHYBA: Spatny pocet argumentu argumentu.\n       Zadejte prosim prikaz ve tvaru: tftp-client -h hostname [-p port] [-f filepath] -t dest_filepath\n");
        return 0;
    } 

    if(pocet == 5){ // Bez volitelnych

        // Hostname
        if(strcmp(argv[1], "-h") != 0){      
            fprintf(stderr, "CHYBA: Spatne zadane parametry\n");
            return 0;
        }
        *hostname = argv[2];

        // Dest_filepath
        if(strcmp(argv[3], "-t") != 0){      
            fprintf(stderr, "CHYBA: Spatne zadane parametry\n");
            return 0;
        }
        *dest_filepath = argv[4];
        return 1;

    } else if(pocet == 7) { // S portem, nebo filepath

        // Hostname
        if(strcmp(argv[1], "-h") != 0){      
            fprintf(stderr, "CHYBA: Spatne zadane parametry\n");
            return 0;
        }
        *hostname = argv[2];

        // Dest_filepath
        if(strcmp(argv[5], "-t") != 0){      
            fprintf(stderr, "CHYBA: Spatne zadane parametry\n");
            return 0;
        }
        *dest_filepath = argv[6];

        // Port nebo filepath??
        if(strcmp(argv[3], "-p") == 0){
            if(!((*port = atoi(argv[4])) && *port >= 0 && *port < 65536)){       // Je cislo, a v rozsahu 0 - 65535
                fprintf(stderr, "CHYBA: Zadany port neni cislo, nebo v rozsahu 0 - 65535\n");
                return 0;
            }
            return 1;
        }

        if(strcmp(argv[3], "-f") != 0){      
            fprintf(stderr, "CHYBA: Spatne zadane parametry\n");
            return 0;
        } 
        *filepath = argv[4];
        return 1;
        
    } else {    // 9

        // Hostname
        if(strcmp(argv[1], "-h") != 0){      
            fprintf(stderr, "CHYBA: Spatne zadane parametry\n");
            return 0;
        }
        *hostname = argv[2];

        // Port
        if(strcmp(argv[3], "-p") == 0){
            if(!((*port = atoi(argv[4])) && *port >= 0 && *port < 65536)){       // Je cislo, a v rozsahu 0 - 65353
                fprintf(stderr, "CHYBA: Zadany port neni cislo, nebo v rozsahu 0 - 65535\n");
                return 0;
            }
        }

        // filepath
        if(strcmp(argv[5], "-f") != 0){      
            fprintf(stderr, "CHYBA: Spatne zadane parametry\n");
            return 0;
        } 
        *filepath = argv[6];
       
        // Dest_filepath
        if(strcmp(argv[7], "-t") != 0){      
            fprintf(stderr, "CHYBA: Spatne zadane parametry\n");
            return 0;
        }
        *dest_filepath = argv[8];
        return 1;
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

// Naplni RRQ/WRQ packet
void naplnRequestPacket(char rrq_packet[], const char filepath[], char mode[], int opcode){
    rrq_packet[0] = 0;
    int last_id = 2;
    
    if(opcode == 1){                                    // RRQ
        rrq_packet[1] = 1;

        for(int i = 0; i < (int) strlen(filepath); i++){
            rrq_packet[last_id + i] = filepath[i];
        }

        last_id += (int) strlen(filepath);
    } else {                                            //WRQ
        rrq_packet[1] = 2;
        char x[] = "stdin";

        for(int i = 0; i < (int) strlen(x); i++){
            rrq_packet[last_id + i] = x[i];
        }

        last_id = 7;
    }

    rrq_packet[last_id++] = '\0';
    
    for(int i = 0; i < (int) strlen(mode); i++){        // Mode
        rrq_packet[last_id + i] = mode[i];
    }
    
    last_id += strlen(mode);
    rrq_packet[last_id] = '\0';
}

// Vypise obsah packetu
void vypisPacket(char packet[], int length){
    for(int i = 0; i < length; i++){
        if(packet[i] == '\0'){
            printf("X");
        } else {
            printf("%c", packet[i]);
        }
    }
    printf("\n");
}

// Vypise chybovou zpravu ve tvaru ERROR {SRC_IP}:{SRC_PORT}:{DST_PORT} {CODE} "{MESSAGE}"
void vypisError(char buffer[], struct sockaddr_in client, struct sockaddr_in server){

    // Dalo by se to i vlozit do fprintf, ale takhle je to prehlednejsi
    char *srcIP  = inet_ntoa(server.sin_addr);
    int srcPort = ntohs(server.sin_port); 
    int dstPort = ntohs(client.sin_port); 
    int code = (int) buffer[3];
    char message[256];
    strcpy(message, buffer + 4);

    fprintf(stderr, "ERROR %s:%d:%d %d \"%s\"\n", srcIP, srcPort, dstPort, code, message);
}

// Odesle packet
int posliPacket(int sockfd, char buffer[], int size, struct sockaddr_in server){
    int bytesSent;
    bytesSent = sendto(sockfd, buffer, size, 0, (struct sockaddr*) &server, sizeof(server));

   if(bytesSent == -1){                                   
        fprintf(stderr, "Nastala CHYBA pri posilani packetu.\n");
        close(sockfd);
        return 0;
    } 

    return 1;
}

// Posle ACK packet
int posliACK(int sockfd, struct sockaddr_in server, int blockNumber){
    char ack[4];

    ack[0] = 0;
    ack[1] = 4;
    ack[2] = blockNumber >> 8;
    ack[3] = blockNumber;

    if(!posliPacket(sockfd, ack, 4, server)){
        return 0;
    }

    return 1;
}

// Vypise zpravu ve tvaru ACK {SRC_IP}:{SRC_PORT} {BLOCK_ID}
void vypisACK(struct sockaddr_in server, int blockID){

    char *srcIP  = inet_ntoa(server.sin_addr);
    int srcPort = ntohs(server.sin_port); 

    fprintf(stderr, "ACK %s:%d %d\n", srcIP, srcPort, blockID);
}

// Vypise zpravu ve tvaru DATA {SRC_IP}:{SRC_PORT}:{DST_PORT} {BLOCK_ID}
void vypisData(struct sockaddr_in client, struct sockaddr_in server, int blockID){
    
    char *srcIP  = inet_ntoa(server.sin_addr);
    int srcPort = ntohs(server.sin_port); 
    int dstPort = ntohs(client.sin_port); 

    fprintf(stderr, "DATA %s:%d:%d %d\n", srcIP, srcPort, dstPort, blockID);
}

// Zpracuje odpovedi na READ request -> Nepridava pri mode netascii Carry (pridava to server), jen jinak otevira soubor pro zapis
// !!!!!!!!!!!!!!! je potreba dodelat timeout
int zpracujRead(int sockfd, struct sockaddr_in server, struct sockaddr_in client, const char destination[], char mode[], char prvniBuffer[], int delkaPrvniho){
    
    FILE* file;
    socklen_t serverAddressLength = sizeof(server);
    
    toLowerString(mode);
    int modeNum     = (strcmp(mode, "octet") == 0) ? 1 : 2;
    int blockNumber = 1;
    int finished    = 0;
    int readBytes;
    char data[MAX_DATA_SIZE + 4];
    memset(data, 0, MAX_DATA_SIZE + 4);
  
    // Soubor
    if(modeNum == 1){
        file = fopen(destination, "wb");     // Octet -> Vytvori soubor pro zapis (kdyz uz soubor existuje, prepise ho)
    } else {
        file = fopen(destination, "w");      // Netascii -> same
    }

    if(file == NULL){
        fprintf(stderr, "Nastala CHYBA pri vytvareni souboru.\n");
        close(sockfd);  // Server se timeoutne
        return 0;
    }
    
    vypisData(client, server, blockNumber);

    // Zpracovani prvniho bufferu
    for(int i = 4; i < delkaPrvniho; i++){
        fputc(prvniBuffer[i], file);
    }
    
    if(!posliACK(sockfd, server, blockNumber)) return 0;

    // Chytani zbylych DATA packetu
    while(!finished){
        memset(data, 0, MAX_DATA_SIZE + 4);
        readBytes = recvfrom(sockfd, data, MAX_DATA_SIZE + 4, 0, (struct sockaddr*) &server, &serverAddressLength);

        if(readBytes == -1){
            fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
            close(sockfd);
            return 0; 
        }

        blockNumber++;
        vypisData(client, server, blockNumber);

        if(data[1] == 3){
            int lastBlockID = ((int)data[2] << 8) + (int)data[3];
            if (lastBlockID == blockNumber - 1){                // Prisla stejna data -> ztratil se ACK
                if(!posliACK(sockfd, server, lastBlockID)) return 0;
                    
                continue;
            }
        } 
        else {
            if(data[1] == 5) vypisError(data, client, server);
        }

        // Zpracovani data packetu
        if(readBytes < MAX_DATA_SIZE) finished = 1;             // Posledni DATA packet

        for(int i = 4; i < readBytes; i++){
            fputc(data[i], file);
        }

        if(!posliACK(sockfd, server, blockNumber)) return 0;
    }

    fclose(file);
    return 1;
}

//===============================================================================================================================
// ./tftp-client -h 127.0.0.1 -f ./READ/test.txt -t ./CLIENT/chyceno.txt

int main(int argc, char* argv[]){
    
    signal(SIGINT, intHandler);
    int port                  = 69;                      // Pokud neni specifikovan, predpoklada se vychozi dle specifikace (69) -> kdyztak se ve funkci prepise
    const char* hostname      = NULL;
    const char* dest_filepath = NULL;
    const char* filepath      = NULL;
    char mode[]               = "netascii";
    int opcode                = 0;

    if(!zkontrolujANastavArgumenty(argc, argv, &port, &hostname, &filepath, &dest_filepath)) return 1;

    // Zjisteni delky packetu podle OPCODE a MODE + napleni
    int requestLength;
    if(!filepath){  
        opcode = 2;     // WRITE
        requestLength = 2 + 5 + 1 + (int) strlen(mode) + 1; // stdin
    } else {
        opcode = 1;     // READ
        requestLength = 2 + (int) strlen(filepath) + 1 + (int) strlen(mode) + 1;
    }

    char requestPacket[requestLength];
    naplnRequestPacket(requestPacket, filepath, mode, opcode);

    // vypisPacket(requestPacket, requestLength);  

// ============= Ziskani IP adres
    // CLIENT
    char clientHostname[100];
    struct hostent *clientHost;
    char *clientIP;

    if (gethostname(clientHostname, sizeof(clientHostname)) == 0) { // Ziskej info o hostovi
        clientHost = gethostbyname(clientHostname);

        if (clientHost != NULL) { // Preved na IP
            clientIP = inet_ntoa(*((struct in_addr*) clientHost->h_addr_list[0]));
            printf("Moje IP: %s\n", clientIP);
        }
    } else {
        fprintf(stderr, "CHYBA pri ziskavani IP adresy klienta\n");
        return 1;
    }

    // SERVER - (funguje pro hostname, i pro adresu)
    struct hostent *serverHostname;
    char *serverIP;

    serverHostname = gethostbyname(hostname);

    if(serverHostname != NULL){
        serverIP = inet_ntoa(*((struct in_addr*) serverHostname->h_addr_list[0]));
        printf("Server IP: %s\n", serverIP);
    }

    // Vygeneruje si TID (rozsah 0 - 65535) , to zadat jako source a destination 69 (108 octal) v requestu 
    srand(time(NULL));
    int clientTID = rand() % 65536;
    printf("TID = %d\n", clientTID);

    struct sockaddr_in client;
    client.sin_family      = AF_INET;
    client.sin_port        = htons(clientTID);
    inet_aton(clientIP, &client.sin_addr);

    struct sockaddr_in server;
    server.sin_family      = AF_INET;
    server.sin_port        = htons(port);
    inet_aton(serverIP, &server.sin_addr);

    // SOCKETY
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0){
        fprintf(stderr, "Nastala CHYBA pri otevirani socketu.\n");
        return 1;
    }

    // BIND - kvuli source portu
    if (bind(sockfd, (struct sockaddr*)&client, sizeof(client)) < 0) {
        fprintf(stderr, "Nastala CHYBA pri bindovani socketu.\n");
        close(sockfd);
    return 1;
    }

    // OODESLANI REQUESTU
    int bytesSent;
    bytesSent = sendto(sockfd, requestPacket, requestLength, 0, (struct sockaddr*) &server, sizeof(server));
    
    if(bytesSent == -1){
        fprintf(stderr, "Nastala CHYBA pri posilani packetu.\n");
        close(sockfd);
        return 1;
    }

//==================================================================================================================================================

    // PRIJMUTI
    char response[MAX_BUFFER_SIZE];
    memset(response, 0 , MAX_BUFFER_SIZE);        
    socklen_t serverAddressLength = sizeof(server);
    int readBytes;
   
    readBytes = recvfrom(sockfd, response, MAX_BUFFER_SIZE, 0, (struct sockaddr*) &server, &serverAddressLength);

    if(readBytes == -1){
        fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
        close(sockfd);
        return 1; 
    }
    
    printf("chytil jsem\n");

    if(opcode == 1){        // Zpracuj cteni

        if(response[0] == 0 && response[1] == 3){       // DATA
            if(!zpracujRead(sockfd, server, client, dest_filepath, mode, response, readBytes)){
                fprintf(stderr, "Nastala CHYBA pri zpracovani requestu.\n");
                return 1;
            }
        } 
        else {
            if(response[0] == 0 && response[1] == 5){   // ERROR
                vypisError(response, client, server);
                close(sockfd);
                return 1;
            }    




        }
    } 
    else {                 // Zpracuj zapis

    }


    // while(!interrupt){

    // }
    close(sockfd);
    return 0;
}
