typedef unsigned char u_char;
typedef unsigned int  u_int;
typedef unsigned short u_short;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>         // sockets duh
#include <netinet/in.h>      
#include <netinet/ip.h>       
#include <netinet/udp.h>      
#include <errno.h>           
#include <signal.h>             // interrupt
#include <sys/time.h>               
#include <pcap/pcap.h>          // gethostbyname,...
#include <arpa/inet.h>          
#include <net/ethernet.h>       
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <stdint.h>


#define MAX_TSIZE 10000000      // Max velikost prijimaneho souboru -> 10 MB

int max_buffer_size = 1024;
int max_data_size   = 512;
int sockfd;

time_t time(time_t *tloc);
int inet_aton(const char *cp, struct in_addr *inp);
int gethostname(char *name, size_t len);

// Zpracovani interruptu
void intHandler(int signum) {
    (void)signum;   // musi to byt takhle, jinak compiler nadava
    close(sockfd);
    exit(1);
}

// Upravi socket na zaklade option timeout
// Zavre stary, a otevre a nastavi novy socket -> nemelo by se nastavovat po aktivni komunikaci
int upravSocket(int time, struct sockaddr_in client){
    (void) client;

    // Nastaveni timeoutu - default 10s
    struct timeval timeout;
    timeout.tv_sec  = time;
    timeout.tv_usec = 0;

    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == -1) {
        fprintf(stderr, "Nastala chyba pri nastavovani socketu. %s\n", strerror(errno));
        close(sockfd);
        return 0;
    }

    return 1;
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

// Naplni request packet
void naplnRequestPacket(char rrq_packet[], const char filepath[], const char dest_filepath[], char mode[], int opcode, int opts[], int vals[]){
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

        for(int i = 0; i < (int) strlen(dest_filepath); i++){
            rrq_packet[last_id + i] = dest_filepath[i];
        }

        last_id += (int) strlen(dest_filepath);
    }

    rrq_packet[last_id++] = '\0';
    
    for(int i = 0; i < (int) strlen(mode); i++){        // Mode
        rrq_packet[last_id + i] = mode[i];
    }
    
    last_id += strlen(mode);
    rrq_packet[last_id] = '\0';
    

    // OPTOINS
    if(opts[0]){
        last_id++;
        strcpy(rrq_packet + last_id, "timeout");
        last_id += 8;
        rrq_packet[last_id] = '\0';
        
        char value[10];
        sprintf(value, "%d", vals[0]);
        strcpy(rrq_packet + last_id, value);
        last_id += strlen(value);
        rrq_packet[last_id] = '\0';

    }
    
    if(opts[1]){
        last_id++;
        strcpy(rrq_packet + last_id, "tsize");
        last_id += 6;
        rrq_packet[last_id] = '\0';
        
        char value[10];
        sprintf(value, "%d", vals[1]);
        strcpy(rrq_packet + last_id, value);
        last_id += strlen(value);
        rrq_packet[last_id] = '\0';
    }
    
    if(opts[2]){
        last_id++;
        strcpy(rrq_packet + last_id, "blksize");
        last_id += 8;
        rrq_packet[last_id] = '\0';
        
        char value[10];
        sprintf(value, "%d", vals[2]);
        strcpy(rrq_packet + last_id, value);
        last_id += strlen(value);
        rrq_packet[last_id] = '\0';
    }
}

// Zjisti delku options
int zjistiOptionLength(int opts[], int vals[]){
    int length = 0;

    if(opts[0]){
        if(!(vals[0] >= 1 && vals[0] <= 255)){
            fprintf(stderr, "CHYBA: Hodnoty rozsireni mimo rozsah.\n");
            return -1;
        }
        
        char value[10];
        sprintf(value, "%d", vals[0]);
        length += strlen(value) + 9;
    }
    
    if(opts[1]){
        char value[10];
        sprintf(value, "%d", vals[1]);
        length += strlen(value) + 7;
    }
    
    if(opts[2]){
        if(!(vals[2] >= 8 && vals[2] <= 65464)){
            fprintf(stderr, "CHYBA: Hodnoty rozsireni mimo rozsah.\n");
            return -1;
        }
        
        char value[10];
        sprintf(value, "%d", vals[2]);
        length += strlen(value) + 9;
    }

    return length;
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

// Vypise zpravu ve tvaru ACK {SRC_IP}:{SRC_PORT} {BLOCK_ID}
void vypisOACK(struct sockaddr_in server, int opts[], int vals[]){

    char *srcIP  = inet_ntoa(server.sin_addr);
    int srcPort = ntohs(server.sin_port); 

    fprintf(stderr, "OACK %s:%d ", srcIP, srcPort);

    if(opts[0]){
        fprintf(stderr, "timeout = %d ", vals[0]);
    }

    if(opts[1]){
        fprintf(stderr, "tsize = %d ", vals[1]);
    }

    if(opts[2]){
        fprintf(stderr, "blksize = %d ", vals[2]);
    }

    fprintf(stderr, "\n");
}

// Zkontroluje options, a pripadne nastavi
// Vraci 0, pri chybe 
//       1, pro blksize
//       2, pro tsize
//       4, pro timeout
int zkontrolujANastavOption(char option[], char readValue[], int vals[], struct sockaddr_in client){

    // timeout -> vals[0]
    // tsize   -> vals[1]
    // blksize -> vals[2]

    int value = atoi(readValue);

    if(!value) return 0;     // Nepovedlo se prevedeni, a ve value nemuze byt 0
        
    // Zkontroluje ktery je to argument
    if(strcmp(option, "timeout") == 0){
        if(value != vals[0]){   // Je to hodnota co jsem posilal?
            return 0;
        } 
        else {
            if(!upravSocket(value, client)) return 0;
            return 4;
        }
    } 
    
    if(strcmp(option, "tsize") == 0){
        if(value > MAX_TSIZE) {         // Pokud je velikost vetsi nez moje povolena, vrati 0
            return 0;
        } else {
            return 2;
        }
            
    }   
    
    if(strcmp(option, "blksize") == 0){
        if(value != vals[2]){   // Je to hodnota co jsem posilal?
            return 0;
        } 
        else {
            max_data_size = value;
            max_buffer_size = max_data_size + 128;
            return 3;
        }
    } 
     
    // Pokud to doslo az sem, nezna to ten option
    return 0;
}

// Zkontroluje OACK
// Vraci 0, pri chybe
//       1, pokud v poradku
int zkontrolujOACK(char response[], int readBytes, int vals[], struct sockaddr_in client, struct sockaddr_in server){

    // postupne se zpracuji, zkontroluji, nastavi options
    int i = 2;
    int j = 0;
    int k = 0;
    int optRead = 0;
    int valRead = 0;
    int opts[3] = {0, 0, 0};
    char option[10];
    char value[30]; 
    while(i < readBytes){
        if(!optRead){       // Jeste se nenacetl option
            if(response[i] != '\0'){
                option[j] = response[i];
                j++;
            } else {
                option[j] = '\0';
                optRead = 1;
                i++;
                continue;
            }
        }
        
        if(optRead && !valRead){    // Nacetl se option, ale ne hodnota
            if(response[i] != '\0'){
                value[k] = response[i];
                k++;
            } else {
                value[k] = '\0';
                valRead = 1;
            }
        }
        
        if(optRead && valRead){     // Je nacten option, i hodnota
            int opt = zkontrolujANastavOption(option, value, vals, client);
            if(!opt){
                return 0;
            } else {
                if(opt == 4){
                    opts[0] = 1;
                } else if (opt == 2){
                    opts[1] = 1;
                } else {
                    opts[2] = 1;
                }
            }

            valRead = 0;
            k = 0;
            optRead = 0;
            j = 0;
        }
        
        i++;
    }

    vypisOACK(server, opts, vals);
    return 1;        
}

// Vypise zpravu ve tvaru DATA {SRC_IP}:{SRC_PORT}:{DST_PORT} {BLOCK_ID}
void vypisData(struct sockaddr_in client, struct sockaddr_in server, int blockID){
    
    char *srcIP  = inet_ntoa(server.sin_addr);
    int srcPort = ntohs(server.sin_port); 
    int dstPort = ntohs(client.sin_port); 

    fprintf(stderr, "DATA %s:%d:%d %d\n", srcIP, srcPort, dstPort, blockID);
}

// Posle error packet
int posliErrorPacket(int sockfd, struct sockaddr_in server, int errorCode, char message[]){
    int packetLength = 5 + (int) strlen(message);    // 2 opcode, 2 errcode, \0
    char errorPacket[packetLength];
    
    errorPacket[0] = 0;
    errorPacket[1] = 5;
    errorPacket[2] = errorCode >> 8;
    errorPacket[3] = errorCode;
    
    strcpy(errorPacket + 4, message);
    errorPacket[packetLength] = '\0';
    // Tady je packet ready

    // Posilani
    if(!posliPacket(sockfd, errorPacket, packetLength, server)){
        return 0;
    }

    return 1;
}

// Zpracuje odpovedi na READ request -> Nepridava pri mode netascii Carry (pridava to server), jen jinak otevira soubor pro zapis
int zpracujRead(int sockfd, struct sockaddr_in server, struct sockaddr_in client, const char destination[], char mode[], char prvniBuffer[], int delkaPrvniho){
    
    FILE* file;
    socklen_t serverAddressLength = sizeof(server);
    
    toLowerString(mode);
    int modeNum     = (strcmp(mode, "octet") == 0) ? 2 : 1;
    int blockNumber = 1;
    int finished    = 0;
    int readBytes;
    char data[max_data_size + 4];
    memset(data, 0, max_data_size + 4);

    file = (modeNum == 2) ? fopen(destination, "wb") : fopen(destination, "w");

    if(file == NULL){
        fprintf(stderr, "Nastala CHYBA pri vytvareni souboru. %s\n", strerror(errno));
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
        memset(data, 0, max_data_size + 4);
        readBytes = recvfrom(sockfd, data, max_data_size + 4, 0, (struct sockaddr*) &server, &serverAddressLength);

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
                if(!posliACK(sockfd, server, lastBlockID)) return 0;
                continue;
            }
        } 
        else {
            if(data[1] == 5) vypisError(data, client, server);
        }

        // Zpracovani data packetu
        if(readBytes < max_data_size + 4) finished = 1;             // Posledni DATA packet

        for(int i = 4; i < readBytes; i++){
            fputc(data[i], file);
        }

        if(!posliACK(sockfd, server, blockNumber)) return 0;
    }

    fclose(file);
    return 1;
}

// Zpracuje odpovedi na WRITE request -> Nezapisuje pri mode netascii Carry (pridava to server), jen jinak otevira soubor pro cteni
int zpracujWrite(int sockfd, struct sockaddr_in server, struct sockaddr_in client){
    
    socklen_t serverAddressLength = sizeof(server);
    
    char response[max_buffer_size];
    char buffer[max_data_size + 4];
    char backup[max_data_size + 4];

    // Tady uz prisel ACK na packet 0

    int finished    = 0;
    int resend      = 0;
    int bytes       = 0;
    int readBytes;
    int blockNumber = 1;
    while(!finished && !resend){
        if(!resend){
            // Naplnit buffer daty
            memset(buffer, 0, max_data_size + 4);    // Nejprve vynulovat
            buffer[0] = 0; 
            buffer[1] = 3; 
            buffer[2] = blockNumber >> 8; 
            buffer[3] = blockNumber; 

            char helpBuffer[max_data_size];
            memset(helpBuffer, 0, max_data_size);

            bytes = 0;
            char c = getc(stdin);
            while(c != EOF && bytes < max_data_size){               // Plneni - Trosku neusporny
                    helpBuffer[bytes] = c;
                    bytes++;
                    if(bytes != max_data_size) c = getc(stdin);
            }

            if(c == EOF) finished = 1;
            
            strncpy(buffer + 4, helpBuffer, max_data_size);
            strncpy(backup, buffer, max_data_size + 4);             // Zaloha
            memset(response, 0, max_buffer_size);                   // Nulovani prijimaciho bufferu

            // Odeslani dat
            if(!posliPacket(sockfd, buffer, bytes + 4, server)) return 0;
        }
        else {  // Znovu poslani posledniho packetu
            if(!posliPacket(sockfd, backup, bytes + 4, server)) return 0;
            resend = 0;
        }
        
        // Prijimani ACKu
        readBytes = recvfrom(sockfd, response, max_buffer_size, 0, (struct sockaddr*) &server, &serverAddressLength);

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

    // fclose(file);
    return 1;
}

//===============================================================================================================================
// read ./tftp-client -h 127.0.0.1 -f ./READ/test.txt -t ./CLIENT/chyceno.txt
// write ./tftp-client -h 127.0.0.1 -t /chyceno.txt < test.txt

int main(int argc, char* argv[]){
    
    signal(SIGINT, intHandler);
    int port                  = 69;                      // Pokud neni specifikovan, predpoklada se vychozi dle specifikace (69) -> kdyztak se ve funkci prepise
    const char* hostname      = NULL;
    const char* dest_filepath = NULL;                    // WRITE -> soubor u clienta
    const char* filepath      = NULL;                    // READ  -> soubor na serveru
    char mode[]               = "octet";
    int opcode                = 0;                      // 1 == READ, 2 == WRITE

    if(!zkontrolujANastavArgumenty(argc, argv, &port, &hostname, &filepath, &dest_filepath)) return 1;

//====================================================

    // Zjisteni delky packetu podle OPCODE a MODE + napleni
    int requestLength;
    if(!filepath){  
        opcode = 2;     // WRITE
        requestLength = 2 + (int) strlen(dest_filepath) + 1 + (int) strlen(mode) + 1; // stdin
        if(!access(dest_filepath, F_OK & R_OK)){
            fprintf(stderr, "CHYBA: Soubor %s jiz existuje, nebo nemate prava pro cteni.\n", dest_filepath);
            return 1;
        }
    } else {
        opcode = 1;     // READ
        requestLength = 2 + (int) strlen(filepath) + 1 + (int) strlen(mode) + 1;
        if(!access(dest_filepath, F_OK & W_OK)){
            fprintf(stderr, "CHYBA: Soubor %s jiz existuje, nebo nemate prava pro zapis.\n", dest_filepath);
            return 1;
        }
    }

    int opts[3] = {1, 1, 1};            // timeout, tsize, blksize
    int vals[3] = {5, 0, 1024};

    // Zjisteni delky souboru
    if(opts[1] && opcode == 2){

        // Dojdi na konec souboru
        if (fseek(stdin, 0, SEEK_END) != 0) {
            fprintf(stderr, "Nastala CHYBA pri zjistovani delky souboru.\n");
            return 1;
        }

        // Hodnota fd = pocet bytu souboru
        long size = ftell(stdin);
        if (size == -1) {
            fprintf(stderr, "Nastala CHYBA pri zjistovani delky souboru.\n");
            return 1;
        }

        vals[1] = (int) size;
        // Vrat pozici na zacatek
        rewind(stdin);
    }

    int optLength = zjistiOptionLength(opts, vals);
    if(optLength == -1) return 1;
    requestLength += optLength;

    char requestPacket[requestLength];
    naplnRequestPacket(requestPacket, filepath, dest_filepath, mode, opcode, opts, vals);

//===================================================

// ============= Ziskani IP adresy
    // SERVER - (funguje pro hostname, i pro adresu)
    struct hostent *serverHostname;
    char *serverIP = NULL;

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
    client.sin_addr.s_addr = INADDR_ANY;
    // inet_aton(clientIP, &client.sin_addr);

    struct sockaddr_in server;
    server.sin_family      = AF_INET;
    server.sin_port        = htons(port);
    inet_aton(serverIP, &server.sin_addr);

    // SOCKETY
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0){
        fprintf(stderr, "Nastala CHYBA pri otevirani socketu.\n");
        return 1;
    }

    // Nastaveni timeoutu - default 10s
    struct timeval timeout;
    timeout.tv_sec  = 10;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == -1) {
        fprintf(stderr, "Nastala chyba pri nastavovani socketu.\n");
        close(sockfd);
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
    char response[max_buffer_size];
    memset(response, 0 , max_buffer_size);  
    socklen_t serverAddressLength = sizeof(server);
    int readBytes;
    
    readBytes = recvfrom(sockfd, response, max_buffer_size, 0, (struct sockaddr*) &server, &serverAddressLength);

    if(readBytes == -1){
        fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
        close(sockfd);
        return 1; 
    }
    printf("chytil jsem\n");

    if(opcode == 1){        // Zpracuj cteni

        if(response[0] == 0 && response[1] == 6){       // OACK

            if(!zkontrolujOACK(response, readBytes, vals, client, server)){
                posliErrorPacket(sockfd, server, 8, "TFTP Option rejected");
                return 1;
            } 
            else {  // Poslal se ack 0, a ceka se na DATA
                if(!posliACK(sockfd, server, 0)) return 1;

                // !!!!Byly options, vytvori se radsi novy buffer (kvuli blksize)
                char responseNew[max_buffer_size];
                memset(responseNew, 0 , max_buffer_size); 
                
                readBytes = recvfrom(sockfd, responseNew, max_buffer_size, 0, (struct sockaddr*) &server, &serverAddressLength);

                if(readBytes == -1){
                    fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
                    close(sockfd);
                    return 1; 
                }

                if(responseNew[0] == 0 && responseNew[1] == 3){       // DATA
                    if(!zpracujRead(sockfd, server, client, dest_filepath, mode, responseNew, readBytes)){
                        fprintf(stderr, "Nastala CHYBA pri zpracovani requestu READ.\n");
                        return 1;
                    }
                } 

                if(responseNew[0] == 0 && responseNew[1] == 5){       // ERROR
                    vypisError(responseNew, client, server);
                    close(sockfd);
                    return 1;
                }    

            }
        } 
        else{       // Nebyly options, pokracuj normalne
            if(response[0] == 0 && response[1] == 3){       // DATA
                if(!zpracujRead(sockfd, server, client, dest_filepath, mode, response, readBytes)){
                    fprintf(stderr, "Nastala CHYBA pri zpracovani requestu READ.\n");
                    return 1;
                }
            } 
            if(response[0] == 0 && response[1] == 5){       // ERROR
                vypisError(response, client, server);
                close(sockfd);
                return 1;
            }    
        
        }
    } 
    else {                 // Zpracuj zapis

        if(response[0] == 0 && response[1] == 6){       // OACK

            if(!zkontrolujOACK(response, readBytes, vals, client, server)){
                posliErrorPacket(sockfd, server, 8, "TFTP Option rejected");
                return 1;
            } 
            else {  // OACK v pohode, posli DATA
                // !!!!Byly options, vytvori se radsi novy buffer (kvuli blksize)
                char responseNew[max_buffer_size];
                memset(responseNew, 0 , max_buffer_size); 
                
                readBytes = recvfrom(sockfd, responseNew, max_buffer_size, 0, (struct sockaddr*) &server, &serverAddressLength);

                if(readBytes == -1){
                    fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
                    close(sockfd);
                    return 1; 
                }

                if(responseNew[0] == 0 && responseNew[1] == 4){       // ACK
                    if(!zpracujWrite(sockfd, server, client)){
                        fprintf(stderr, "Nastala CHYBA pri zpracovani requestu READ.\n");
                        return 1;
                    }
                } 

                if(responseNew[0] == 0 && responseNew[1] == 5){       // ERROR
                    vypisError(responseNew, client, server);
                    close(sockfd);
                    return 1;
                }    

            }


        } 
        else {      // Nebyly options, pokracuj normalne
            if(response[0] == 0 && response[1] == 4){       // ACK
                if(!zpracujWrite(sockfd, server, client)){
                    fprintf(stderr, "Nastala CHYBA pri zpracovani requestu WRITE.\n");
                    return 1;
                }
            } 
            
            if(response[0] == 0 && response[1] == 5){       // ERROR
                vypisError(response, client, server);
                close(sockfd);
                return 1;
            }    
        }
    }

    close(sockfd);
    return 0;
}
