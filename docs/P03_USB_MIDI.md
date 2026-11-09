# P03 — USB MIDI na ESP32-S31

Firmware `0.0.3-usb-midi` koristi S31 HS kontroler 0 na priključku J4 Type-A.
USB host lifecycle i deskriptori potječu iz pinned NEKTAR-P4 izvora. P03 koristi
managed komponentu `espressif/usb` 1.5.0 s ESP-IDF-om 6.1.0 i zadržava izvornu
MIT licencu USB adaptera.

Na jezgri 0 USB klijent prima pakete, a ingress task ih pretvara u MIDI događaje.
Audio task na jezgri 1 preuzima do 16 događaja po bloku. Generacija USB sesije i
epoha synth ulaza filtriraju zastarjele događaje. Gubitak veze, greška IN prijenosa,
nepotpun paket i prelijevanje reda poništavaju zahvaćeni ulaz. Pri gubitku živog
ulaza audio task radi panic, uključujući oslobađanje sustaina. Postojeći panic
zaustavlja transport loopera i čuva sadržaj traka. P04 ima zaseban posao oko
vlasništva između USB i Web izvora.

## Provjera na ploči

Status je **HARDWARE NOT RUN**. Prvo potvrditi P01 boot i P02 audio prema
[postupku provjere ES8311](P02_AUDIO_BRINGUP.md). Za P03 koristiti image iz
`build-p03/` s njegovim hashovima.

1. Podignuti P03 bez priključene klavijature. Preko UART0 na 115200 poslati `?`.
   Očekuje se `state=2`, `usb_ready=0`, rast broja blokova i tišina.
   `t` daje jednu nisku C4 probu prije prvog live ulaza u tom bootu.
2. Spojiti GX49 na J4 Type-A i očitati VID/PID, USB brzinu, sučelje i sesiju.
   Očekuje se `usb_ready=1` i `state=5`. PA se aktivira kad je MIDI sesija spremna.
   Svirati prvo pojedinačne note na početnoj niskoj glasnoći, pa Note Off,
   akorde, sustain, pitch bend i druge dokumentirane CC-ove.
3. Držati notu ili sustain pa odspojiti klavijaturu. Potvrditi tišinu, rast
   broja panica i povratak u `state=2`. Ponovno spojiti klavijaturu, potvrditi
   novu sesiju i odsvirati novu notu. Ponoviti uz držanu tipku i brzi reconnect.
4. Spremiti USB i audio log, raw/semantic drop brojače, pripremne deadlineove i
   DMA overflowe. `rx_to_dispatch_max_us` mjeri od završetka USB IN prijenosa
   do obrade MIDI događaja. Kašnjenje od tipke do zvuka mjeri se zasebno.

Za P03 kvalifikaciju trebaju stvarni USB i audio rezultati. Host suite koristi
USB stubove i determinističko vrijeme.
