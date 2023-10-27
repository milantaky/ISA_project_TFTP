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

// TODOOOOOOOOOOOOOOOOOO

int zkontrolujANastavArgumenty(int pocet, char* argv[], int* port, char* hostname[], char* cestaSoubor[], char* cestaServer[]){
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
        *cestaServer = argv[4];
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
        *cestaServer = argv[6];

        // Port nebo filepath??
        if(strcmp(argv[3], "-p") == 0){
            if(!((*port = atoi(argv[4])) && *port >= 0 && *port < 65354)){       // Je cislo, a v rozsahu 0 - 65353
                fprintf(stderr, "CHYBA: Zadany port neni cislo, nebo v rozsahu 0 - 65353\n");
                return 0;
            }
            return 1;
        }

        if(strcmp(argv[3], "-f") != 0){      
            fprintf(stderr, "CHYBA: Spatne zadane parametry\n");
            return 0;
        } 
        *cestaSoubor = argv[4];
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
            if(!((*port = atoi(argv[4])) && *port >= 0 && *port < 65354)){       // Je cislo, a v rozsahu 0 - 65353
                fprintf(stderr, "CHYBA: Zadany port neni cislo, nebo v rozsahu 0 - 65353\n");
                return 0;
            }
        }

        // filepath
        if(strcmp(argv[5], "-f") != 0){      
            fprintf(stderr, "CHYBA: Spatne zadane parametry\n");
            return 0;
        } 
        *cestaSoubor = argv[6];
       
        // Dest_filepath
        if(strcmp(argv[7], "-t") != 0){      
            fprintf(stderr, "CHYBA: Spatne zadane parametry\n");
            return 0;
        }
        *cestaServer = argv[8];
        return 1;
    } 

    return 1;
}

//===============================================================================================================================

int main(int argc, char* argv[]){

    int port = -1;
    char* hostname;
    char* cestaServer;
    char* cestaSoubor;

    // Kontrola argumentu
    if(!zkontrolujANastavArgumenty(argc, argv, &port, &hostname, &cestaSoubor, &cestaServer)){
        return 1;     // Chyba vypsana ve funkci
    }

    //printf("hostname: %s\nport: %d\nfilepath: %s\ncesta: %s\n", hostname, port, cestaSoubor, cestaServer);
    // printf("hostname: %s\nfilepath: %s\ncesta: %s\n", hostname, cestaSoubor, cestaServer);
    // printf("hostname: %s\nport: %d\ncesta: %s\n", hostname, port, cestaServer);
    printf("hostname: %s\ncesta: %s\n", hostname, cestaServer);
    
    
    return 0;
}
