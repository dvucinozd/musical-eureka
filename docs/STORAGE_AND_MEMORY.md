# Interni flash, SoundFont i memorijski budžet

**Potvrđeno:** nema microSD ni WAV snimanja. **Predloženo:** bank store s dva raw flash
slota i NVS za male postavke/presete. Nije implementirano; konačni layout zaključati u P05.

## Zašto novi backend

P4 čita SF2 i presete s FAT32 kartice. Preimenovanje `/sdcard` u `/flash` ne rješava
kapacitet, trošenje ni utjecaj erase/write operacija na audio. Raw A/B bank slotovi
omogućuju da nepotpun upload ne prepiše prethodno potvrđenu banku, bez SD drivera i
bez velikog filesystema. Početni UI podržava jednu odabranu banku i rezervni slot za
zamjenu/oporavak; više trajnih banaka nije obećano.

Preset save/restore planira se kao ograničen, verzioniran NVS blob ili kolekcija s
ograničenim brojem zapisa. Web import/export postojećeg preset formata može ostati,
ali nije pregled datoteka s kartice. Točan broj/veličina preseta zaključavaju se nakon
pregleda serijaliziranih struktura. Ne zapisivati velike SF2 banke u NVS.

## Nacrt particija za 16 MiB flash

[config/partitions.proposed.csv](../config/partitions.proposed.csv) je **neaktivan nacrt**.
Aktivni bootstrap koristi IDF single-app particije. Nacrt nije uputa za postojeću ploču.

| Particija | Offset | Veličina | Namjena |
| --- | --- | --- | --- |
| nvs | 0x9000 | 64 KiB | Postavke/preseti, aktivna banka i format version |
| otadata | 0x19000 | 8 KiB | IDF OTA metadata |
| phy_init | 0x1B000 | 4 KiB | PHY data |
| ota_0 | 0x20000 | 3 MiB | Prvi S31 app slot |
| ota_1 | 0x320000 | 3 MiB | Drugi S31 app slot |
| bank_a | 0x620000 | 4 MiB | SF2 payload + verzionirani header |
| bank_b | 0xA20000 | 4 MiB | Nova banka / rollback |
| Nedodijeljeno | 0xE20000 | 1920 KiB | Rezerva, ne filesystem po automatizmu |

Bootloader offset/veličinu određuje generirani IDF build; ne prepisivati P4 flash adrese.
Provjeriti application size margin, potpis i oba OTA slota prije aktivacije layouta.
Nova tablica nije kompatibilna s proizvoljnim zatečenim imageom bez migracijskog plana.

## Transakcija banke i oporavak

1. Zatraži ekskluzivno storage stanje; odbij dok live note ili looper sviraju.
2. Audio/DMA se koordinirano zaustavljaju s utišanim PA, ingress je zatvoren.
3. Odaberi neaktivni slot. Provjeri veličinu prije erasea; nikad ne briši aktivni slot.
4. Upisuj bounded chunkove u neaktivni slot; vodi SHA-256 i byte count.
5. Pročitaj payload nazad, potvrdi SHA i SF2 preflight. Header s magic/schema/size/hash/
   generation/CRC i commit oznakom dovrši tek nakon potpune validacije.
6. Promijeni aktivni izbor u NVS-u tek kad novi slot prolazi. Stari slot ostaje valjan.
7. Nakon restarta validiraj manifest i odabrani slot; pri grešci koristi posljednji
   valjan ili synth fallback. Nepotpun payload nije banka. Hash nije kriptografska
   autentikacija sadržaja; OTA potpis ima zasebnu svrhu.

Točan commit-marker layout i redoslijed oporavka mora dobiti testove prekida napajanja
na svakoj granici. NVS manifest i slot header moraju se slagati; ne oslanjati se na
pretpostavljenu atomarnost više flash zapisa. Upload ne mora odmah učitati banku.
Load ostaje odvojena operacija s istim pravilima generacije i looper ownershipa kao izvor.

## Početni prijedlog RAM limita

Kapacitet ploče 16 MiB nije raspoloživ heap. Prije SF load-a izmjeriti ukupni/free/minimum/
largest za INTERNAL, DMA i SPIRAM, nakon audio + USB + Wi-Fi init-a.

| Stavka | Projektna početna granica, nije izmjeren rezultat |
| --- | --- |
| Upload SF2 payload | najviše **3 MiB**, zajednički limit HTTP/storage/validator/worker |
| Ukupni SF staging/active allocator cap | **10 MiB**, uključujući izvorni blob i sve žive SF alokacije |
| Slobodna PSRAM rezerva | najmanje **4 MiB** pri alokaciji, uz provjeru largest blocka |
| PCM16 → float | približno 2× veličina sample podataka uz dodatni izvorni blob |
| Postojeći WAV ring | uklonjen iz ciljnog dizajna: 512 KiB + recorder task |
| DMA/internal reserve | broj definirati iz ELF-a i mjerenja; nema izmišljenog PASS budžeta |

Za sample-heavy banku od 3 MiB vrh može biti oko 3 + 6 MiB prije metadata/voice objekata.
Zato 3 MiB file cap **nije jamstvo da banka stane**. Validator izračunava stvarni peak;
allocator dodatno provjerava slobodnu i najveću kontinuiranu regiju. Aktivna stara banka
može uzrokovati `NEEDS_UNLOAD`; ne obećavati neprimjetnu zamjenu dviju velikih banaka.
Granice su konzervativni prijedlog, mijenjaju se samo uz dokaz i isti API/validator ugovor.

## Flash i audio

IDF opisuje da zadane SPI0/1 operacije mogu isključiti cache i zaustaviti taskove,
uključujući pristup PSRAM-u. XIP/auto-suspend su opcije s dodatnim uvjetima, nisu
zadano rješenje za ovaj port. Zato početni plan zaustavlja audio pri flash upisu i
čitanju banke tijekom load-a. Sviranje koristi unaprijed pripremljene PSRAM uzorke.
[S31 flash concurrency dokumentacija](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s31/api-reference/peripherals/spi_flash/spi_flash_concurrency.html).

NVS save, promjena aktivne banke i OTA također su flash operacije. Spremanje tijekom
sviranja odbiti ili odgoditi uz jasan status. Flash wear smanjiti explicit saveom,
debounce/coalescingom i pisanjem samo promijenjenih podataka. Nema audio snimanja,
periodičnog zapisivanja audio blokova ni recorder direktorija.

