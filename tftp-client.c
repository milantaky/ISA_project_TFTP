#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>         // sockets duh
#include <netinet/in.h>      
#include <netinet/ip.h>       
#include <netinet/udp.h>      
// #include <errno.h>           // ???
#include <signal.h>             // interrupt
#include <time.h>               // na timeout?
#include <pcap/pcap.h>
#include <arpa/inet.h>          // htons
#include <net/ethernet.h>       // ???
#include <unistd.h>

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

int zkontrolujANastavArgumenty(int pocet, char* argv[], int* port, const char* hostname[], const char* filepath[], const char* dest_filepath[]){
    /*  
    [] = volitelny
    ttftp-client -h hostname [-p port] [-f filepath] -t dest_filepath

        -h IP adresa/doménový název vzdáleného serveru
        -p port vzdáleného serveru
        pokud není specifikován předpokládá se výchozí dle specifikace
        -f cesta ke stahovanému souboru na serveru (download)
        pokud není specifikován používá se obsah stdin (upload)
        -t cesta, pod kterou bude soubor na vzdáleném serveru/lokálně uložen      */

    if(pocet == 2 && strcmp(argv[1], "--help") == 0){
        printf("ttftp-client -h hostname [-p port] [-f filepath] -t dest_filepath\n\n-h IP adresa/doménový název vzdáleného serveru\n-p port vzdáleného serveru\npokud není specifikován předpokládá se výchozí dle specifikace\n-f cesta ke stahovanému souboru na serveru (download)\npokud není specifikován používá se obsah stdin (upload)\n-t cesta, pod kterou bude soubor na vzdáleném serveru/lokálně uložen.\n");
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

// Naplni RRQ/WRQ packet
void naplnRequestPacket(char rrq_packet[], const char filepath[], char mode[], int opcode){
    rrq_packet[0] = '0';
    int last_id = 2;
    
    if(opcode == 1){
        rrq_packet[1] = '1';

        for(int i = 0; i < (int) strlen(filepath); i++){
            rrq_packet[last_id + i] = filepath[i];
        }

        last_id += (int) strlen(filepath);
    } else {
        rrq_packet[1] = '2';
        char x[] = "stdin";

        for(int i = 0; i < (int) strlen(x); i++){
            rrq_packet[last_id + i] = x[i];
        }

        last_id = 7;
    }

    rrq_packet[last_id++] = '\0';
    
    for(int i = 0; i < (int) strlen(mode); i++){
        rrq_packet[last_id + i] = mode[i];
    }
    
    last_id += strlen(mode);
    rrq_packet[last_id] = '\0';
}

//===============================================================================================================================

// struct tftpPacket {
//     struct ip      ip_header;
//     struct udphdr  udp_header;
//     //struct tftphdr tftp_header;
//     char* data;
// };

int main(int argc, char* argv[]){

    int port = 69;                      // Pokud neni specifikovan, predpoklada se vychozi dle specifikace (69) -> kdyztak se ve funkci prepise
    const char* hostname      = NULL;
    const char* dest_filepath = NULL;
    const char* filepath      = NULL;
    char mode[] = "octet";
    uint16_t opcode = 0;

    if(!zkontrolujANastavArgumenty(argc, argv, &port, &hostname, &filepath, &dest_filepath)) return 1;

    // Pokud neni nastaven filepath, pouziva se obsah z stdin (upload - 2 (WRQ)), jinak download - 1 (RRQ)
    int rrq_length;
    if(!filepath){  
        opcode = 2;
        rrq_length = 2 + 5 + 1 + (int) strlen(mode) + 1; // stdin
    } else {
        opcode = 1;
        rrq_length = 2 + (int) strlen(filepath) + 1 + (int) strlen(mode) + 1;
    }

    char rrq_packet[rrq_length];
    naplnRequestPacket(rrq_packet, filepath, mode, opcode);

    // vypise obsah packetu
    // for(int i = 0; i < rrq_length; i++){
    //     if(rrq_packet[i] == '\0'){
    //         printf("X");
    //     } else {
    //         printf("%c", rrq_packet[i]);
    //     }
    // }
    // printf("\n");


    printf("PORT: %d\n", port);

    if(opcode == 1){
        printf("READ\n");
    } else {
        printf("WRITE\n");
    }

// ============= Ziskani IP adres
    // CLIENT
    char clientHostname[1024];
    struct hostent *client;
    char *clientIP;

    if (gethostname(clientHostname, sizeof(clientHostname)) == 0) { // Ziskej info o hostovi
        client = gethostbyname(clientHostname);

        if (client != NULL) { // Preved na IP
            clientIP = inet_ntoa(*((struct in_addr*) client->h_addr_list[0]));
            printf("Host name: %s\n", clientHostname);
            printf("Moje IP: %s\n", clientIP);
        }
    } else {
        fprintf(stderr, "CHYBA pri ziskavani IP adresy klienta\n");
        return 1;
    }


    // SERVER - (funguje pro hostname, i pro adresu)
    struct hostent *server;
    char *serverIP;

    server = gethostbyname(hostname);

    if(server != NULL){
        serverIP = inet_ntoa(*((struct in_addr*) server->h_addr_list[0]));
        printf("Server IP: %s\n", serverIP);
    }


    /*
    POSTUP:
        1. zjistit IP klienta -- DONE
        2. Zjistit IP serveru -- DONE
        3. Sestavit packet
        4. otevrit handle
        5. odeslat packet
        6. cekat na packet zpatky
        7. zkontrolovat ho
        8. udelat dalsi a poslat
    */

    // Vygeneruje si TID (rozsah 0 - 65535) , to zadat jako source a destination 69 (108 octal) v requestu 
    srand(time(NULL));
    int clientTID = rand() % 65536;

    // // UDP Header
    // struct udphdr udpHeader;
    // udpHeader.uh_sport = clientTID;       // Source port
    // udpHeader.uh_dport = port;            // Destination port
    // udpHeader.uh_ulen;          // Lenght -> Pocet bytu v UDP packetu + 8 (UDP header)
    // udpHeader.uh_sum = 0;       // 0 if unused

    // // IPv4 Header
    // ========= znamena dodelat
    // struct ip ipHeader;
    // ipHeader.ip_hl = 5;     // Header length
    // ipHeader.ip_v = 4;      // Version (IPv4)
    // ipHeader.ip_p = IPPROTO_UDP;    // Protokol
    // ipHeader.ip_sum = 0;    // Checksum - zatim 0   ==========
    // ipHeader.ip_tos;        // Type of Service - NO FUCKING IDEA    =============
    // ipHeader.ip_ttl = 64;   // Time To Live - 64 bylo ve wiresharku, idk
    // ipHeader.ip_len;        // Komplet delka TFTP (+ 20 IP, + 8 UDP) (udpHeader.uh_ulen + 20)   ===============
    // ipHeader.ip_id;         // NO CLUE  ==========
    // ipHeader.ip_off = 0;    // Fragment offset
    // ipHeader.ip_src.s_addr = clientIP; // ================
    // ipHeader.ip_dst.s_addr = serverIP; // ================

    // // TFTP Header




    // // SOCKETY
    // int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    // if(sockfd < 0){
    //     fprintf(stderr, "CHYBA pri otevirani socketu.\n");
    //     return 1;
    // }
    
    // // mac - interface en0

    // //send(new_socket, hello, strlen(hello), 0);

    // close(sockfd);
    return 0;
}
