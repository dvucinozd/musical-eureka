# Interni flash, SoundFont i memorijski budžet

P05b povezuje interni flash, Web upload, reprodukciju SoundFonta i startup preset
u NVS-u. Fizičke provjere slijede na S31 ploči.

## Zašto novi backend

P4 čita SF2 i presete s FAT32 kartice. Preimenovanje `/sdcard` u `/flash` ne rješava
kapacitet, trošenje ni utjecaj erase/write operacija na audio. Raw A/B bank slotovi
omogućuju da nepotpun upload ne prepiše prethodno potvrđenu banku, bez SD drivera i
bez velikog filesystema. Početni UI podržava jednu odabranu banku i rezervni slot za
zamjenu/oporavak; više trajnih banaka nije obećano.

NVS drži jedan korisnički startup preset pod `nektarina/startup`. Format `NSP1`
je 200 B s eksplicitnim little-endian poljima. Sprema synth konfiguraciju,
broj taktova i identitet zvuka. SF2 izbor koristi SHA-256 datoteke, bank i
program, pa ista banka može dobiti drugi A/B slot ili runtime generaciju.
Ako zvuk nedostaje, vraća se synth fallback uz spremljenu konfiguraciju.
MIDI sadržaj looper traka je u RAM-u. Spremanje nepromijenjenog preseta preskače upis.

## Particije za 16 MiB flash

[config/partitions.csv](../config/partitions.csv) je aktivan od P04 zbog veličine
nativnog Wi-Fi imagea. P01–P03 slike imaju raniji single-app raspored. Fizička
migracija particija provjerava se kad stigne ploča.

| Particija | Offset | Veličina | Namjena |
| --- | --- | --- | --- |
| nvs | 0x9000 | 64 KiB | Startup preset i IDF podaci |
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

1. Web primi ograničen upload u PSRAM, bez erase/write. Zatraži storage stanje.
   Zahtjev se odbija dok note, efekti ili looper sviraju.
2. Audio/DMA se koordinirano zaustavljaju s utišanim PA, ingress je zatvoren.
3. Validiraj SF2 i pripremi novu banku prije erasea. Odaberi slot koji nije
   zaštićena prethodna banka.
4. Izračunaj SHA-256 i upiši ograničene blokove u drugi slot.
5. Usporedi pročitane bajtove s validiranim payloadom iz kojeg je izračunat
   SHA-256. Upiši header s magic/schema/size/hash/generation/CRC.
6. Objavi novi slot završnim markerom. Stari slot ostaje valjan.
7. Worker preda novu banku audio tasku koji je aktivira dok je DMA zaustavljen.
   Worker oslobađa prethodnu banku nakon ACK-a. Na bootu pokušava pripremiti
   slotove redom od veće generacije prema manjoj. Ako oba pokušaja padnu,
   instrument ostaje u synth načinu.

P05a koristi generaciju zapisanu u slotu kao autoritet za redoslijed oporavka. Time
commit banke ne ovisi o drugom upisu u NVS. NVS će držati postavke i izbor zvuka.
Header je 64 B u prvom sektoru od 4 KiB, s little-endian verzijom i generacijom,
veličinom, SHA-256, CRC-32 i završnim četverobajtnim markerom. Payload počinje u
sljedećem sektoru. U modelu flasha provjereni su djelomični erase, svaki od tri
payload upisa testne banke, upis headera, torn marker i povratak na stariju banku
kad noviji sadržaj ne prođe provjeru.

S31 na bootu pokrene Wi-Fi i primira audio, zatim parkira audio za čitanje
prije otvaranja Web i USB ulaza. Bez startup preseta bira prvi program prve
banke koja prođe provjeru i pripremu. Sa startup presetom učita banku u RAM
i vrati spremljeni zvuk ako je dostupan. P05b za flash koristi interne bounce
buffere. HTTP task je jedini runtime vlasnik operacija pohrane.

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
može iscrpiti budžet. Web kontrola **Unload from RAM** oslobađa banku uz očuvan
flash zapis. **Load saved bank** ponovno je učita. SF2 trake zadržavaju vezu
s bankom i nakon zaustavljanja. Za zamjenu ili unload svaku treba prebaciti
na synth preset ili očistiti.
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

## Provjere P05b

- Host testovi u `tests/run_p05a_tests.ps1 -P05b` pokrivaju šest djelomičnih
  operacija NOR modela, audio/worker ACK, budžet događaja, PCM render,
  četiri SF2 trake, zamjenu i `NSP1` codec. Regresija pokriva i USB cutoff
  pri ponovnom pokretanju audio izlaza.
- Browser plugin nije dostupan. Chrome preko Playwrighta i lokalnog simuliranog
  API-ja provjerio je stranice kataloga, upload, save/restore, synth fallback
  kroz kontrolu i unload/reload na širinama 1280 i 390 px.
- Na identificiranoj ploči provjeriti ES8311/PA tišinu pri pauzi i povratku,
  odbijanje upisa uz aktivne note i looper, ponovni boot s NVS-om, SHA/SF2
  fallback i prekide napajanja tijekom bank upisa. Izmjeriti rezervu i najveći
  PSRAM blok, latenciju USB/Web nota i deadlineove pri stvarnom opterećenju.

