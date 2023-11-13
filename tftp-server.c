#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>         // sockets duh
#include <netinet/in.h>      
#include <netinet/ip.h>       
#include <netinet/udp.h>      
#include <errno.h>           // ???
#include <signal.h>             // interrupt
#include <sys/time.h>               // na timeout?
#include <arpa/inet.h>          // htons
#include <net/ethernet.h>       // ???
#include <unistd.h>
#include <ctype.h>

#define MAX_TSIZE       10000000    // Max velikost prijimaneho souboru -> 10 MB

int max_buffer_size = 1024;
int max_data_size   = 512;
int sockfd;

// Funkce pro zpracovani interrupt signalu
void intHandler(int signum) {
    (void)signum;       // Musi to byt takhle, jinak compiler nadava
    close(sockfd);
    exit(1);
}

// Upravi socket na zaklade option timeout
// Zavre stary, a otevre a nastavi novy socket -> nemelo by se nastavovat po aktivni komunikaci
int upravSocket(int time, struct sockaddr_in server){

    // Zavri stary socket
    close(sockfd);

    // Otevri novy s timeoutem
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);  

    if(sockfd < 0){
        fprintf(stderr, "CHYBA pri otevirani socketu.\n");
        return 0;
    }

    // Nastaveni timeoutu - default 10s
    struct timeval timeout;
    timeout.tv_sec  = time;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == -1) {
        fprintf(stderr, "Nastala chyba pri nastavovani socketu.\n");
        close(sockfd);
        return 0;
    }

    // SOCKET BIND
    if(bind(sockfd, (struct sockaddr*) &server, sizeof(server)) < 0){
        fprintf(stderr, "Nastala CHYBA pri bindovani socketu.\n");
        close(sockfd);
        return 0;
    }

    return 1;
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
int nactiLokaci(char buffer[], char *dest[]){
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
    
    *dest = (char *)malloc(strlen(lokace) + 1);

    if(dest == NULL){
        fprintf(stderr, "Nastala CHYBA pri alokovani pameti.\n");
        return 0;
    }

    strcpy(*dest, lokace);
    return 1;
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
    errorPacket[2] = errorCode >> 8;     
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

// Posle OACK
int posliOACK(int sockfd, struct sockaddr_in client, int optValues[], int oackLength){
    char oack[oackLength];

    oack[0] = 0;
    oack[1] = 6;

    int offset = 2;
    if(optValues[0]){   // timeout
        strcpy(oack + offset, "timeout");
        offset += 7;
        oack[offset++] = '\0';

        char value[20];
        sprintf(value, "%d", optValues[0]);

        strcpy(oack + offset, value);
        offset += strlen(value);
        oack[offset++] = '\0';
    }

    if(optValues[1]){   // tsize
        strcpy(oack + offset, "tsize");
        offset += 5;
        oack[offset++] = '\0';

        char value[20];
        sprintf(value, "%d", optValues[1]);

        strcpy(oack + offset, value);
        offset += strlen(value);
        oack[offset++] = '\0';
    }

    if(optValues[2]){   // blksize
        strcpy(oack + offset, "blksize");
        offset += 7;
        oack[offset++] = '\0';

        char value[20];
        sprintf(value, "%d", optValues[2]);

        strcpy(oack + offset, value);
        offset += strlen(value);
        oack[offset++] = '\0';
    }

    if(!posliPacket(sockfd, oack, oackLength, client)){
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
void vypisRequest(struct sockaddr_in client, int mode, char filepath[], int request, int optionArray[]){
    
    char *srcIP  = inet_ntoa(client.sin_addr);
    int srcPort = ntohs(client.sin_port); 
    char modeC[10];

    if(mode == 2){
        strcpy(modeC, "octet");
    } else {
        strcpy(modeC, "netascii");
    }

    if(request == 1){
        fprintf(stderr, "RRQ %s:%d \"%s\" %s ", srcIP, srcPort, filepath, modeC);
    } 
    else {
        fprintf(stderr, "WRQ %s:%d \"%s\" %s ", srcIP, srcPort, filepath, modeC);
    }

    if(optionArray[0]){
        fprintf(stderr, "timeout = %d ", optionArray[0]);
    }

    if(optionArray[1] != -1){
        fprintf(stderr, "tsize = %d ", optionArray[1]);
    }

    if(optionArray[2]){
        fprintf(stderr, "blksize = %d ", optionArray[2]);
    }

    fprintf(stderr, "\n");
}

// Zpracuje READ -> Z pohledu serveru: Otevre soubor a posila data pakcety
int zpracujRead(int sockfd, struct sockaddr_in client, char location[], int mode, int options){
    
    FILE *readFile;
    socklen_t clientAddressLength = sizeof(client);
    int readBytes;
    int blockNumber = 1;
    char buffer[max_data_size + 4];                 
    char helpBuffer[max_data_size];                 
    char response[max_buffer_size];
    char backup[max_data_size + 4];
    memset(buffer, 0 , max_data_size + 4);         
    memset(helpBuffer, 0 , max_data_size);        
    memset(backup, 0 , max_data_size + 4);           
    memset(response, 0 , max_buffer_size);            

    // Options > 0 -> byly optiony, cekej na ack 0
    if(options == 2){
        readBytes = recvfrom(sockfd, response, max_buffer_size, 0, (struct sockaddr*) &client, &clientAddressLength);

        if(readBytes == -1){
            fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
            close(sockfd);
            return 0; 
        } 

        if(response[1] == 4 && response[2] == 0 && response[3] == 0){   // neprisel ack 0
            vypisACK(client, 0);
        } 
        else {    
            posliErrorPacket(sockfd, client, 8, "TFTP option rejected");
            return 0;
        }
    }

    buffer[0] = 0;
    buffer[1] = 3;
    buffer[2] = blockNumber >> 8;
    buffer[3] = blockNumber;

    readFile = (mode == 2) ? fopen(location, "rb") : fopen(location, "r");

    if(readFile == NULL){
        fprintf(stderr, "Nastala CHYBA pri otevirani souboru pro cteni: %s\n", strerror(errno));
        close(sockfd);
        return 0;
    }

    int finished = 0;
    int resend   = 0;
    int bytes    = 0;

    while(!finished && !resend){                                    // Dokud to neni hotove a nemusi se nic odesilat znovu
        if(!resend){                                                // Naplnit novy
            buffer[2] = blockNumber >> 8;
            buffer[3] = blockNumber;

            bytes = 0;
            memset(helpBuffer, 0 , max_data_size);                  // Vynuluje buffer
            char c = getc(readFile);
            while(c != EOF && bytes < max_data_size){               // Plneni - Trosku neusporny
                if(c == '\n' && bytes < max_data_size - 1){
                    helpBuffer[bytes] = '\r';
                    bytes++;
                }
                helpBuffer[bytes] = c;
                bytes++;
                if(bytes != max_data_size) c = getc(readFile);
            }

            if(c == EOF) finished = 1;

            strncpy(buffer + 4, helpBuffer, max_data_size);         // Slozeni packetu
            strncpy(backup, buffer, max_data_size + 4);             // Zaloha
            if(!posliPacket(sockfd, buffer, bytes + 4, client)) return 0;
            printf("---------posilam data %d\n", blockNumber);
        } 
        else {    // Nastala chyba, je potreba odeslat znovu
            if(!posliPacket(sockfd, backup, bytes + 4, client)) return 0;
            printf("---------znovu posilam data\n");
            resend = 0;
        }

        // Prijimani ACKu
        readBytes = recvfrom(sockfd, response, max_buffer_size, 0, (struct sockaddr*) &client, &clientAddressLength);
        printf("--------prijmul jsem packet\n");

        if(readBytes == -1){
            fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
            close(sockfd);
            return 0; 
        } 
        
        if(response[0] == 0 && response[1] == 4){
            int lastACK = ((int)buffer[2] << 8) + (int)buffer[3];
            printf("--------- odeslan packet %d, dostal jsem ack %d\n", blockNumber, lastACK);
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
int zpracujWrite(int sockfd, struct sockaddr_in server, struct sockaddr_in client, char location[], int mode){

    FILE* file;
    socklen_t clientAddressLength = sizeof(client);
    int blockNumber = 0;
    int finished    = 0;
    int readBytes;

    char data[max_data_size + 4];
    memset(data, 0, max_data_size + 4);

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
        memset(data, 0, max_data_size + 4);
        readBytes = recvfrom(sockfd, data, max_data_size + 4, 0, (struct sockaddr*) &client, &clientAddressLength);

        if(readBytes == -1){
            fprintf(stderr, "Nastala CHYBA pri prijimani packetu.\n");
            fclose(file);
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
            if(data[1] == 5){
                vypisError(data, client, server);
                fclose(file);
                close(sockfd);
                return 0;
            } 
        }

        // Zpracovani data packetu
        if(readBytes < max_data_size + 4) finished = 1;             // Posledni DATA packet

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

// Zjisti delku souboru
long zjistiDelkuSouboru(char location[]){
    FILE* file = fopen(location, "rb");

    if(file == NULL) return 0;

    if (fseek(file, 0, SEEK_END) != 0) return 0;

    long size = ftell(file);

    if(size == -1) return 0;

    fclose(file);
    return size;
}

// Rozhodni a spln options + otestuj hodnoty jestli mohou byt!!!!!!!!!!!!!!
// Vraci 0 pri chybe
//       1 pokud se nic neprijalo
//       2 pokud se poslal OACK
int rozhodniANastavOptions(int magicNumber[], int optValues[], struct sockaddr_in server, struct sockaddr_in client, char location[]){

    // request -> 1 == read, 2 == write

    // timeout -> optValues[0]
    // tsize   -> optValues[1]
    // blksize -> optValues[2]

    int oackLength = 2;

    if(magicNumber[0]){   // timeout
        if(optValues[0] >= 1 && optValues[0] <= 255){
            if(!upravSocket(optValues[0], server)) return 0;
            
            char value[20];
            sprintf(value, "%d", optValues[0]);
            oackLength += 7 + strlen(value) + 2;

        } 
        else {
            return 0;
        }
    }

    if(magicNumber[1]){   // tsize
        if(optValues[1] == 0){  // read
            int length = (int) zjistiDelkuSouboru(location);
            if(!length) return 0; // Chyba souboru

            char value[30];
            sprintf(value, "%d", length);
            oackLength += 5 + strlen(value) + 2;
            optValues[1] = length;

        } else {
            if(optValues[1] > MAX_TSIZE){
                return 0;
            }

            char value[30];
            sprintf(value, "%d", optValues[1]);
            oackLength += 5 + strlen(value) + 2;
            optValues[1] = optValues[1];
        }
    }

    if(magicNumber[2]){   // blksize
        if(optValues[2] >= 8 && optValues[2] <= 65464){
            max_data_size   = optValues[2];
            max_buffer_size = max_data_size + 128;
            
            char value[20];
            sprintf(value, "%d", optValues[2]);
            oackLength += 7 + strlen(value) + 2;

        } else {
            return 0;
        }
    }

    // Je nejaky option? -> pridalo/nepridalo se neco
    if(oackLength != 2){
        if(!posliOACK(sockfd, client, optValues, oackLength)) return 0;
        return 2;
    }

    return 1;

}

// Zpracuje options
// Vraci 0 pri chybe nebo cislo (i soucet) podle options (timeout (4), tsize (2), blksize (1))
// Pokud jsou nastaveny nejake optiony, posle to funkci rozhodniANastavOptions
// Pokud precte nejaky co nezna, ignoruje ho
int zpracujOptions(char buffer[], int readBytes, struct sockaddr_in client, struct sockaddr_in server, char location[], int optionArray[]){

    int nulls        = 0;
    int optionsExist = 0;
    int returnValue  = 0;
    int request = (int) buffer[1];  // rrq/wrq

    int i = 2;
    
    // Dojde se k \0 za mode, a pokracuje?
    while(i < readBytes){
        if(buffer[i] == '\0') nulls++;
        if(nulls == 2 && (i + 1) < readBytes){          
            optionsExist = 1;  
            break;
        }
        i++;
    }
    
    if(!optionsExist) return 1;
    
    i++;
    int optNumber[3] = {0, 0, 0};                  // timeout = 4, tsize = 2, blksize = 1  -> funguje to jako prava v linuxu
    int optValues[3] = {0, 0, 0};

    // Iteruje se pres options
    while(optionsExist){
        if(optNumber[0] && optNumber[1] && optNumber[2]) return 0;    // Vsechny se nasly, a jsou dalsi, nebo uz jsou nactene nektere duplicitne

        // Nacteni optionu
        char option[10];
        memset(option, '\0', 10);
        strcpy(option, buffer + i);
        
        // Je tam i value?
        if((i + (int)strlen(option) + 1) < readBytes){           
            i += strlen(option) + 1;    
        } else {
            return 0;   // Chybny option
        }        
        
        // Nacte se, a prevede value
        char value[20];
        memset(value, '\0', 20);
        strcpy(value, buffer + i);
        int valuee = atoi(value);
        i += strlen(value);
        
        // Zjisti se co to bylo za value
        if(strcmp(option, "timeout") == 0){
            optNumber[0] = 1;
            optValues[0] = valuee;
            optionArray[0] = valuee;
            returnValue += 4;
        }

        if(strcmp(option, "tsize") == 0){
            optNumber[1] = 1;  
            if((valuee <= 0 || valuee > MAX_TSIZE) && request == 2){        // Preteklo to, nebo je to vetsi nez povolena velikost
                // chce to delku souboru pri write -> chyba
                return 0;
            }

            if(valuee == 0 && request == 2){
                // chce to delku souboru pri write -> chyba
                return 0;
            }

            if(valuee != 0 && request == 1){
                // posila to velikost pri cteni
                return 0;
            }

            optValues[1] = valuee;
            optionArray[1] = valuee;
            returnValue += 2;
        }
        
        if(strcmp(option, "blksize") == 0){
            optNumber[2] = 1;  
            optValues[2] = valuee;
            optionArray[2] = valuee;
            returnValue += 1;
        } 

        // Jsou tam dalsi?
        if(!((i + 1) < readBytes)){                  
            break;
        } else {
            i++;
        }
    }

    if(optNumber[0] || optNumber[1] || optNumber[2]) rozhodniANastavOptions(optNumber, optValues, server, client, location);

    return returnValue;
}

// Zpravuje request
int zpracujRequest(int sockfd, struct sockaddr_in client, struct sockaddr_in server, char buffer[], int readBytes, int mode, const char cesta[], char location[]){
    
    // cesta    -> root_dirpath -> kam se to bude ukladat na serveru
    // location -> filename

    // Uprava cesty pro zapis
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

    // Kontrola souboru
    if(buffer[1] == 1){                 // READ
        if(access(location, F_OK)){     // Existence
            posliErrorPacket(sockfd, client, 1, "File not found.");
            return 1;
        }

        if(access(location, R_OK)){     // Prava
            posliErrorPacket(sockfd, client, 2, "Access violation.");
            return 1;
        }
    } else {
        if(buffer[1] == 2){             // WRITE
            if(!access(cestaWrite, F_OK)){     // Existence
                posliErrorPacket(sockfd, client, 6, "File already exists.");
                return 1;
            }

            FILE* file = (mode == 2) ? fopen(cestaWrite, "wb") : fopen(cestaWrite, "w");

            if(file == NULL){
                posliErrorPacket(sockfd, client, 0, "Invalid filepath.");
                return 1;
            }
        }
    }

    int optionArray[3] = {0, -1, 0};
    int options = zpracujOptions(buffer, readBytes, client, server, location, optionArray);

    if(!options){
        posliErrorPacket(sockfd, client, 8, "TFTP option rejected.");
        return 1;
    }

    if(buffer[1] == 1){                 // READ
        printf("dostal jsem packet pro cteni\n");
        vypisRequest(client, mode, location, 1, optionArray);

        if(!zpracujRead(sockfd, client, location, mode, options)){
            fprintf(stderr, "Nastala CHYBA pri zpracovani requestu.\n");
            free(location);
            return 1;
        }
    } 
    else if(buffer[1] == 2){            // WRITE
        printf("dostal jsem packet pro zapis\n");
        
        // Uprava cesty
        // char cestaNew[(int) strlen(cesta) + 1];
        // char cestaWrite[(int)(strlen(location) + strlen(cesta)) + 1];
    
        // for(int i = 0; i < (int) strlen(cesta); i++){
        //     cestaNew[i] = cesta[i];
        // }
        
        // cestaNew[(int) strlen(cesta)] = '\0';
    
        // strcpy(cestaWrite, cestaNew);
        // if(location[0] == '.'){
        //     strcpy(cestaWrite + (int)(strlen(cesta)), location + 1);
        // } else {
        //     strcpy(cestaWrite + (int)(strlen(cesta)), location);
        // }

        vypisRequest(client, mode, location, 2, optionArray);

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

    return 0;
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

    // SOCKETY
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);  

    if(sockfd < 0){
        fprintf(stderr, "CHYBA pri otevirani socketu.\n");
        return 1;
    }

    // Nastaveni timeoutu - default 10s
    struct timeval timeout;
    timeout.tv_sec  = 10;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == -1) {
        fprintf(stderr, "Nastala CHYBA pri nastavovani socketu.\n");
        close(sockfd);
        return 1;
    }

    // SOCKET BIND
    if(bind(sockfd, (struct sockaddr*) &server, sizeof(server)) < 0){
        fprintf(stderr, "Nastala CHYBA pri bindovani socketu. %s\n", strerror(errno));
        close(sockfd);
        return 1;
    }

    // CEKANI NA REQUEST A CTENI
    printf("Cekam na klienta...\n");

    socklen_t clientAddressLength = sizeof(client);
    char buffer[max_buffer_size];                   // !!!!!!!! Zatim 1024
    char sendBuffer[max_buffer_size];       
    memset(buffer, 0 , max_buffer_size);            // Vynuluje buffer
    memset(sendBuffer, 0 , max_buffer_size);        // Vynuluje buffer
    // int blockNumber = 0;
    int mode = 0;                                   // 1 = netascii, 2 = octet
    int offset;
    char *location = NULL;                          // cesta kam se budou ukladat soubory / odkud se budou cist -> zalezi na mode (root_dirpath)

    int readBytes;
    readBytes = recvfrom(sockfd, buffer, max_buffer_size, 0, (struct sockaddr*) &client, &clientAddressLength);

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

    if(!nactiLokaci(buffer, &location)){
        posliErrorPacket(sockfd, client, 3, "Disk full or allocation exceeded.");
        return 1;
    }      
    offset = 3 + (int) strlen(location);            // 2 kvuli opcode a 1 je \0 za lokaci

    if(!(mode = zkontrolujMode(buffer, offset))){          // 2 == octet 1 == netascii            
        fprintf(stderr, "CHYBA: Spatny mode requestu. Zvolte pouze netascii/octet.\n");
        posliErrorPacket(sockfd, client, 4, "Illegal TFTP operation.");
        free(location);
        return 1;
    }

    if(zpracujRequest(sockfd, client, server, buffer, readBytes, mode, cesta, location)) return 1;

    free(location);
    close(sockfd);
    return 0;
}
