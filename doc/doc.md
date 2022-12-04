Ondřej Mach (xmacho12)  

# Tunelování datových přenosů přes DNS dotazy

## Návrh

Největší otázkou návrhu bylo kódování souboru při přenosu.
Doménové jméno smí obsahovat pouze písmena (velká i malá) čísla a pomlčku.
Přenášený soubor může obsahovat jakékoli hodnoty bajtů.
Proto je nutné použít kódování.

Na první pohled se nabízí kódování `base64`, bohužel toto kódování má ve své sadě znaků i `+`, `/` a `=`.
Pro řešení bylo zvoleno kódování bajtů na hexadecimální zápis.
Toto značně sníží efektivitu, na druhou stranu je implementace velmi snadná, protože z jednoho vstupního bajtu se stanou přesně 2 výstupní.

Dalším problémem bylo, že nepřenášíme pouze obsah souboru, ale i jeho jméno. 
Toto bylo vyřešeno delimiterem, který oddeluje jméno a obsah souboru. 
Tento delimiter je odeslán i na konci obsahu, aby signalizoval konec přenosu. 
Jako delimiter byl zvolen znak `x`, bez konkrétního důvodu.

Pokud bychom tedy odesílali soubor `AAA`, jehož obsah by byl `BBB`,
výsledný kód by vypadal takto.

```
414141x424242x
```

Dále je třeba vyřešit, jakým způsobem rozdělit zakódovaný soubor do DNS paketů.
Je bráno v úvahu, že soubor může být libovolné velikosti a bude poslán ve více paketech.
DNS se běžně posílá přes UDP pakety.
UDP ale není spolehlivé, proto by bylo nutné implementovat vlastní mechnismy na ošetření. 
UDP také nezaručuje, že odeslané pakety přijdou ve stejném pořadí. 
Kvůli těmto problémům byla zvolena implementace přes TCP, která není tak častá, ale všechny servery by ji měly podporovat. 

Jeden DNS paket může nést mnoho záznamů ve 4 sekcích (dotazy, odpovědi, autoritativní sekce, dodatečná sekce).
Pro jednoduchost bude tato implementace odesílat v jednom paketu pouze jeden záznam, který bude v sekci s dotazy.
Odsud plyne omezení na množství přenesených dat v jednom dotazu.
Doména se skládá z více částí (anglicky labels) oddělených tečkou. 
Jedna tato část má maximální délku 63 znaků, celá doména může být dlouhá až 255 znaků.
V této implementaci obsahuje doména pouze jednu část (label) se zakódovanými znaky, kterou následuje `BASE_HOST` zadaný jako argument.

Ve výsledku tak jeden paket DNS přenese až 31 bajtů výsledného souboru.

\pagebreak

## Implementace komponentu sender

DNS sender akceptuje pouze parametry, které jsou v zadání, neimplementuje žádná rozšíření.
Při vynechání volitelného parametru `-u` je adresa výchozího DNS server načtena z `/etc/resolv.conf`.

Sender je navržen tak, že po jednom bytu načítá jméno souboru a poté obsah souboru.
Toto byte je kódován do hexadecimální reprezentace v ASCII a uložen do bufferu k odeslání.
Buffer s obsahem k odeslání je omezen na 63 bytů, což je maximální délka labelu.

Po naplnění bufferu, nebo skončení souboru je zavolána funkce `sendQuery`, která odešle obsah bufferu.

Funkce `sendQuery` vytvoří validní DNS paket a odešle ho do zadané schránky.

## Implementace komponentu receiver

Implementace receiveru je poněkud zajímavější. Parametry opět zůstávají stejné jako v zadání.

Receiver používá volání `fork` aby mohl paralelně obsluhovat více připojení.
Parent proces celou dobu čeká na nové připojení od klienta.
Jakmile se klient připojí, je zavolán `fork` pro vytvoření child procesu.
Child proces zavolá funkci `handleConnection` a parent proces opět čeká.
Z tohoto důvodu jsou výpisy na stderr číslované, každé nové připojení má své číslo.

Připojení probíhá do té doby, než je zakódovaný obsah souboru ukončen delimiterem (znak `'x'`).
V případě, že je vadné kódování server nahlásí chybu a ukončí spojení.
Pokud nebude konec domény odpovídat zadané doméně, bude tento dotaz ignorován, ale připojení zůstává aktivní.

Server běží na cílovém počítači jako root, proto je důležité dbát i na bezpečnostní opatření.
Bez kontroly jmen souborů, která přichází od klienta by mohlo dojít k tzv. path traversal.
To znamená, že pokud by klient poslal jméno souboru např. `../file.txt` soubor by se uložil mimo původní složku.
Toto je velmi nebezpečné, obzvlášť za roota, který může přepsat jakýkoli soubor v systému.
Implementace receiveru předchází této zranitelnosti tak, že nepovolí žádné jméno souboru, které začíná lomítkem nebo obsahuje dvě tečky za sebou.

## Omezení a rozšíření

Sender i receiver jsou implementovány streamově a nepoužívají dynamickou alokaci.
Díky tomu je velikost přeneseného souboru prakticky neomezená.
Je ale možné, že při určité velikosti nastane overflow některé proměnné.
Největší přenesený soubor měl 600 MB, větší nebyly testovány.

Jméno souboru smí být dlouhé maximálně 31B, protože musí být přeneseno v prvním paketu.
Toto omezení je na straně reeiveru.

Program přenáší soubory jakéhokoli typu, včetně binárních souborů.

Oproti zadání receiver implementuje rozšíření, že může obsluhovat více senderů najednou.
Tato vlastnost byla testována spuštěním více senderů.
Výpisky z příjmu jednotlivých souborů se potom prolínaly.


\pagebreak

## Testování

Projekt byl testován lokálně, ale sputění na více počítačích v síti by probíhalo analogicky.
Pokud není projekt zkompilovaný, je třeba spustit příkaz `make` v kořenovém adresáři projektu.

### Receiver

Pro přenos souboru je třeba nejprve spustit receiver.
Server musí být spuštěn jako root protože komunikuje na portu 53.
Tento port patří mezi tzv. well-known porty, které jsou definovány jako rozsah od 1 do 1023.

```
# ./dns_receiver example.com files
Listening for new connections...
```

`example.com` je koncem domén, ve kterých budou přenášena data. 
`files` je jméno složky, do které budou ukládány přijaté soubory.

Při spuštění může dojít k problému, že lokální DNS server již naslouchá na portu 53.
V tomto případě je potřeba zjistit, o který proces se jedná a ukončit jej.

```
# ./dns_receiver example.com files
Could not bind to the socket.
```

### Sender

Nyní je možné spustit sender. 
Ten už není potřeba spouštět jako root, protože používá dynamický port.

```
$ python3 -c 'print(100*"A")' | ./dns_sender -b example.com abcd
[INIT] 127.0.0.53
[ENCD] a         0 '61x414141414141414141414141414141414141414141414141414141414141.example.com'
[SENT] a         0 30B to 127.0.0.53
[ENCD] a         1 '41414141414141414141414141414141414141414141414141414141414141.example.com'
[SENT] a         1 31B to 127.0.0.53
[ENCD] a         2 '41414141414141414141414141414141414141414141414141414141414141.example.com'
[SENT] a         2 31B to 127.0.0.53
[ENCD] a         3 '41414141414141410ax.example.com'
[SENT] a         3 9B to 127.0.0.53
[CMPL] a of 101B
```

Po odeslání se v terminálu s běžícím servere objeví nové hlášky.

```
# ./dns_receiver example.com files
Listening for new connections...
[INIT] 127.0.0.1
[1]     Connection started.
[1]     Opening file `a`
[PARS] a         0 '61x414141414141414141414141414141414141414141414141414141414141.example.com'
[RECV] a         0 30B from 127.0.0.1
[PARS] a         1 '41414141414141414141414141414141414141414141414141414141414141.example.com'
[RECV] a         1 31B from 127.0.0.1
[PARS] a         2 '41414141414141414141414141414141414141414141414141414141414141.example.com'
[RECV] a         2 31B from 127.0.0.1
[PARS] a         3 '41414141414141410ax.example.com'
[RECV] a         3 9B from 127.0.0.1
[1]     Closing file.
[1]     Closing connection.
[CMPL] a of 101B
```
\newpage

### Výsledky

Jak je viditelné z logů, pakety i jejich obsah spolu korespondují.
Pokud se nyní podíváme do složky `files` zadané jako parametr receiveru,
najdeme soubor `abcd`. 
Tento test byl proveden na více souborech, aby byla ověřena funkčnost programu.
Největší testovaný soubor bylo video velikosti 600 MB, přenos trval v řádu minut.

### Wireshark

Pro sledování paketů je také vhodné zapnout nástroj Wireshark.
Pro tento případ bylo nastaveno rozhraní na `any` a filtr na `dns.qry.name matches "example.com$"`.

![DNS pakety v nástroji Wireshark](img/wireshark1.png)


