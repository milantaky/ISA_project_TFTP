ISA projekt
TFTP Server + Klient
===
**Autor:** Milan Takáč - xtakac09
3BIT FIT VUT

O projektu:
---
Náplní tohoto projektu bylo naprogramovat server a klienta pro komunikaci
pomocí protokolu TFTP.

Projekt by měl korespondovat s RFC specifikací TFTP[1], rovněž ale musí
být v souladu s rozšířeními:

- TFTP Option Extension [2]
- TFTP Blocksize Option [3]
- TFTP Timeout Interval and Transfer Size Options [4]
  
Spouštění:
---
Po přeložení souborů **tftp-server.c**, a **tftp-client.c** příkazem `make` dostanete 2 spustitelné soubory.
Toto jsou příkazy ke spuštění s přepínači:

>Server: `./tftp-server [-p port] root_dirpath`

>Kde:
>*  `-p` je místní port, na kterém bude server očekávat příchozí spojení
>*  `root_dirpath` je cesta k adresáři, pod kterým se budou ukládat příchozí soubory


>Klient: `./tftp-client -h hostname [-p port] [-f filepath] -t dest_filepath`

>Kde:
>*  `-h` je IP adresa/doménový název vzdáleného serveru
>*  `-p` je port vzdáleného serveru
>pokud není specifikován předpokládá se výchozí dle specifikace
>*  `-f` je cesta ke stahovanému souboru na serveru (download),
>pokud není specifikován používá se obsah stdin (upload)
>*  `-t` je cesta, pod kterou bude soubor na vzdáleném serveru/lokálně uložen

Lze ale taktéž použít přepínač `--help`, který vypíše nápovědu pro spuštění

**Server**
===
- Defaultně poslouchá na portu 69 (nebyl-li použit přepínač -p)
- Umí zpracovat POUZE options:
`timeout` 
`tsize` 
`blksize`
- V základu má nastavený timeout na 10s - po uplynutí timeoutu se ukončí (v případě že nepřijde packet)

### Postup fungování
Nejprve se zpracují argumenty volání programu.
Poté se nastaví port, na kterém se poslouchat.
Otevře se socket, a čeká se na příchozí request. 
Když request dorazí, na základě hodnoty opcode se zkontroluje mode a případné options.
Následně probíhá výměna zasílání / přijímání packetů podle typu requestu:
- Při requestu READ server zasílá packety s daty klientovi, a čeká na ACK od klienta,  a zkontroluje číslo bloku který mu klient potvrdil, než pošle další data.
- Při requestu WRITE server přijímá data packety od klienta, a na každý odpovídá ACKem s číslem bloku který právě přijal.

Po přijetí/odeslání finálního packetu se spojení i server ukončí.

**Klient**
===
- Klient defaultně odesílá na port 69 (nebyl-li použit přepínač -p)
- Umí zpracovat POUZE options:
`timeout` 
`tsize` 
`blksize`
- V základu má nastavený timeout na 10s - po uplynutí timeoutu se ukončí (v případě že nepřijde packet)

## Postup fungování
Nejprve se zpracují argumenty volání programu.
Přeloží se hostname na adresu serveru, vygeneruje se TID a nastaví jako klientův port, na který se bude odesílat.
Otevře se socket, připraví se request packet a odešle se. 
- Při requestu READ klient přijímá od serveru packety s daty a odpovídá na ně ACKem s příslušným číslem přijatého bloku.
- Při requestu WRITE klient zasílá serveru packety s daty po přijetí ACKu s minulým číslem bloku dat.

Po přijetí/odeslání finálního packetu se spojení i klient ukončí.

## Server + klient
- Pokud nastane před/při přenosu nějaká chyba, strana na které nastala chyba odešle error packet s příslušným chybovým kódem, případně se vypíše chyba na standardní chybový výstup, a spojení se ukončí.
- Funguje i zachycování signálů přerušení (Ctrl + C), při jeho zpracování se druhé straně odešle chybový packet, a spojení se ukončí.

**Zdroje**
===
[1] RFC 1348 - (https://datatracker.ietf.org/doc/html/rfc1350)
[2] RFC 2347 - (https://datatracker.ietf.org/doc/html/rfc2347)
[3] RFC 2348 - (https://datatracker.ietf.org/doc/html/rfc2348)
[4] RFC 2349 - (https://datatracker.ietf.org/doc/html/rfc2349)