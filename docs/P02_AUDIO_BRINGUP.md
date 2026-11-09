# P02 — synth jezgra i ES8311 izlaz

Firmware `0.0.2-audio` pokreće I2S TX sa 48 kHz, 128 frameova i dva 16-bitna
Philips slota. Lijevi slot nosi mono PCM, a desni šalje nule. MCLK je 256 × Fs.
Audio task radi na jezgri 1 i jedini mijenja stanje synth jezgre. P02 spaja četiri
MIDI looper trake u render. Upravljanje trakama pripada P04.

Probe ima jedan C4 s gainom 2/50 u synthu i −12 dB na kodeku. PA ostaje LOW kroz
inicijalizaciju, provjeru I2C adrese 0x18 i prvih šest predanih DMA blokova. Naredba
`t` iz tišine pokreće jednu probu s uvodnom tišinom, 200 ms držanja note, otpuštanjem
i završnom tišinom za pražnjenje DMA reda. Ponavljanje naredbe tijekom probe se odbija.
Timeout I2S predaje, nepotpuni upis ili nekonačni PCM zaključavaju grešku i mute.

## Fizička provjera — NOT RUN

Najprije evidentirati ploču i potvrditi P01 bootstrap boot prema
[predlošku za ploču](templates/BOARD_BRINGUP.md). P02 provjeriti zasebno s imageom
i hashovima iz P02 evidencije. Koristiti USB–UART bridge za UART0 naredbe (115200).

1. Očitati reviziju i memoriju. U boot logu tražiti `S31_AUDIO` ES8311 ACK i SDP
   potvrdu. Zabilježiti stvarne clockove ako je dostupan instrument. Očekivani su
   LRCK 48 kHz, BCLK 1,536 MHz i MCLK 12,288 MHz.
2. Poslati `?`. Očekuje se `state=2`, rast broja blokova i `fault=ESP_OK`.
   Potvrditi tišinu i PA LOW prije prve probe. Druga stanja su 0 off, 1 priming,
   3 probe i 4 fault.
3. Poslati `t` jednom pa `?` nakon otprilike sekunde. Očekuje se jedna kratka C4
   nota niske razine, povrat u `state=2` i `probes` veći za jedan. Zabilježiti klik,
   distorziju ili neočekivanu glasnoću i korisnikovu slušnu potvrdu.
4. Spremiti log, broj DMA overflowa i pripremnih deadlineova. Svaki porast traži
   analizu prije prolaza. Ciljani playback provjerava osnovni put, dok se
   kvalifikacija više glasova i opterećenja radi kasnije.

ES8311 je u DAC-only načinu, pa je mikrofon izvan P02 probe. UART naredbe služe
bringupu. USB MIDI i javne kontrole dobivaju ulaz u odgovarajućim fazama.
