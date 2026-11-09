# Nastavak rada

Za aktivni image, host i build rezultate pogledati [stanje projekta](STATE_SUMMARY.md).
Adresa S31 repozitorija je u [razvojnim uputama](DEVELOPMENT.md).

## Kada se uključi fizički uređaj

Identificirati model/reviziju/port i zatečeno stanje, ispuniti
[BOARD_BRINGUP](templates/BOARD_BRINGUP.md) te dogovoreni oporavak. Potvrditi bootstrap
boot i PSRAM prije audio imagea. Slijedi [P02 audio bringup](P02_AUDIO_BRINGUP.md) s
provjerom ES8311 ACK-a, clockova, tišine i probne note. Nakon toga slijedi
[P03 provjera fizičke GX49 klavijature](P03_USB_MIDI.md).

## P04 — sljedeći software paket

Ploča još nije kod korisnika. Prenijeti Web sučelje i nativni Wi-Fi adapter,
uvesti zajedničko vlasništvo USB i Web nota te kontrole četiriju MIDI traka.
Testirati sučelje i logiku na računalu. Bluetooth zvučnik ima zaseban P06B paket
nakon uspostave lokalne audio putanje i Web kontrole.

## Otvoreni izbori

- Fizička revizija i priključci, provjeriti kad stigne ploča.
- Želi li korisnik zadržati vanjski ST7789 i zasebnu tipku; zasad headless/Web.
- Obavezne SF2 banke/preseti, njihove veličine/licence; limit 3 MiB je prijedlog.
- NVS preset kapacitet, A/B bank protokol i finalna tablica particija (P05).
- Pristup Wi-Fi-ju (P04) i OTA signing policy (P07).

Već je odlučeno: **bez microSD, bez WAV snimanja, četiri MIDI looper trake ostaju**.
Ne tražiti ponovno istu odluku i ne vraćati uklonjene funkcije kroz „privremeni” SD backend.

