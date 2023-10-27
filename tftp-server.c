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

int zkontrolujANastavArgumenty(int pocet, char* argv[], int* port, char* cesta[]){
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

        if(!((*port = atoi(argv[2])) && *port >= 0 && *port < 65354)){       // Je cislo, a v rozsahu 0 - 65353
            fprintf(stderr, "CHYBA: Zadany port neni cislo, nebo v rozsahu 0 - 65353\n");
            return 0;
        }
    } else {
        *cesta = argv[1];
    }


    return 1;
}

//===============================================================================================================================

int main(int argc, char* argv[]){

    int port = -1;
    char* cesta;

    // Kontrola argumentu
    if(!zkontrolujANastavArgumenty(argc, argv, &port, &cesta)){
        return 1;     // Chyba vypsana ve funkci
    }

    printf("port: %d\ncesta: %s\n", port, cesta);
    
    
    return 0;
}
