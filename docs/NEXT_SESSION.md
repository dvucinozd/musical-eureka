# Nastavak rada

Za aktivni image, host i build rezultate pogledati [stanje projekta](STATE_SUMMARY.md).
Adresa S31 repozitorija je u [razvojnim uputama](DEVELOPMENT.md).
Za odabir imagea i provjeru SHA-256 koristi [referentne buildove](BRINGUP_BASELINE.md).

## Handoff za 2026-11-12

Rad je zaustavljen na korisnikov zahtjev nakon potvrde čiste C6 synth note.
Sljedeći posao je fizički P03 USB MIDI pilot s GX49 na J4 Type-A.
Prvo ponovno identificirati UART COM7 i očitati `?`. Posljednja proba
završila je u `state=2`, `fault=ESP_OK`, uz nula deadlinea i DMA overflowa.

Aktivni image je `build-p02-diagnostic/nektarina_s31.bin`,
verzija `0.0.5-soundfont-dev`, SHA-256
`d7ed40a79bc9588322082472e6e64de99db57fef3c5d9047f5bb0e26f39da274`.
Bootloader potječe iz `build-review-local`, a aktualna tablica ima
dva 3 MiB app slota. Za početak P03 dovoljno je koristiti već instalirani image.

Početni synth gain je 2/50. UART probe ga privremeno postave na 25/50
i nakon probe vrate prethodni gain. Pri MIDI pilotu zabilježiti i podesiti
glasnoću kroz podržane MIDI/Web kontrole ako je potrebno.

P02 clock mjerenja, PA napon i uzrok nečujnog C4 sinusa ostaju otvoreni.
Korisnik ima multimetar. Zvučnik je na J9, model i impedancija nisu zabilježeni.
USB MIDI, Web interakcije, SoundFont, Bluetooth i OTA još čekaju fizičke pilote.

Lokalni tvornički backup je `build-device-20261111/factory-flash-16MiB.bin`.
Njegov hash i postupak oporavka su u [P01 zapisu](BOARD_BRINGUP_2026-11-11.md).
BIN/ELF i lokalne kopije ranijih dijagnostičkih imagea ostaju izvan Gita.
`build-review-local` i `build-review-combined` bilježe provjeru R1/R2 prije
kasnijih UART dijagnostika. Za budući aktualni Bluetooth/OTA pilot napraviti
novi build iz tadašnjeg izvora i zabilježiti njegove hashove.

## Sljedeće na fizičkoj ploči

Popravci iz [code reviewa](CODE_REVIEW_2026-11-11.md) su implementirani.
Na ploči je `build-p02-diagnostic/` s ES8311 readbackom i UART probama.
Aktivni app i hashovi su u [P02 zapisu](evidence/2026-11-11-p02-hardware.json).
Profil koristi aktualni raspored particija s dva OTA slota.
Za kasniji Bluetooth i OTA pilot koristi `build-review-combined/`.
[Veličine, SHA-256 i pripadajuće flash datoteke](evidence/2026-11-11-review-fixes.json).

P01 je prošao 2026-11-11 na PCB-u V1.0, UART COM7: 16 MiB flash/PSRAM,
320 MHz CPU i dva uspješna boota s PSRAM testom. Potom je instaliran
`build-review-local` s punim aktualnim particijskim rasporedom.
[Identitet, lokalni backup, hashovi i oporavak](BOARD_BRINGUP_2026-11-11.md).
Ponovno provjeriti identitet porta prije pristupa. Dopuniti oznaku modula/fotografiju.

Korisnik je spojio zvučnik na J9 i potvrdio čistu C6 synth notu te izravni
1 kHz ton u lijevom slotu. C4 sinus nije bio čujan. Oznake zvučnika i uzrok
razlike u odzivu još nisu poznati. Clockovi i PA napon nisu izmjereni.
Za novu provjeru na aktualnom imageu UART `h` daje C6, `t` C4,
`s/l/r` izravni 1 kHz u oba/lijevom/desnom slotu. Svaka proba traje
jednu sekundu i vraća audio u muted stanje. Synth proba koristi gain 25/50
i vraća prethodni gain. `?` čita peak i brojače, `d` codec/GPIO stanje.
Nastaviti provjeru USB MIDI-ja uz bilježenje preostalih P02 mjerenja.
Za slušanje prvo koristiti više note koje su se pokazale čujnima.
Slijedi
[P03 provjera fizičke GX49 klavijature](P03_USB_MIDI.md) i [P04 Web](P04_WEB.md).

## P05b — fizička kvalifikacija

P05b softverski povezuje PA mute i DMA ACK s Web uploadom i NVS-om. Audio task
aktivira banku tijekom pauze, zatim puni DMA tišinom i odbacuje prethodni
USB backlog. Postupak za stvarni flash, startup restore i RAM/latenciju je
u [P05b provjerama](STORAGE_AND_MEMORY.md#provjere-p05b).

## P06 — spreman pilot

[P06 postupak](P06_SOUNDFONT.md) daje buildove i 16/32-glasni pilot. Prije
promjene DSP-a ili rasporeda memorije snimiti cijelu obradu bloka na S31
nakon fizičkog P01–P05 bring-upa. Odvojeno provjeriti latenciju, slušanje
i normalni image kroz soak.

## P06B — spreman Bluetooth razvojni profil

[P06B postupak](P06B_BLUETOOTH.md) daje build, izbor izlaza i fizički
pilot. Nakon lokalnog audio bring-upa provjeriti SSP, format, latenciju,
prekid veze i rad Bluetootha uz USB MIDI i Wi-Fi.

## P07 — razvojni OTA pilot

[P07 postupak](P07_OTA.md) opisuje provjeru paketa, instalaciju razvojnog
rollback profila i fizički test. Nakon provjere identiteta uređaja i
osnovnog audio bring-upa potvrditi početni P07 slot, uspješan OTA i
oporavak nakon prekinutog prvog boota. Nakon tih provjera slijedi P08.

## Otvoreni izbori

- Dopuniti fotografiju i doslovnu oznaku modula, uvježbati ručni BOOT+RESET oporavak.
- Želi li korisnik zadržati vanjski ST7789 i zasebnu tipku; zasad headless/Web.
- Obavezne SF2 banke/preseti, njihove veličine/licence; limit 3 MiB je prijedlog.
- OTA signing policy (P07).

Već je odlučeno: **bez microSD, bez WAV snimanja, četiri MIDI looper trake ostaju**.
Ne tražiti ponovno istu odluku i ne vraćati uklonjene funkcije kroz „privremeni” SD backend.

