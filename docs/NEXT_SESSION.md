# Nastavak rada

Za aktivni image, host i build rezultate pogledati [stanje projekta](STATE_SUMMARY.md).
Adresa S31 repozitorija je u [razvojnim uputama](DEVELOPMENT.md).
Za odabir imagea i provjeru SHA-256 koristi [referentne buildove](BRINGUP_BASELINE.md).

## Kada se uključi fizički uređaj

Identificirati model/reviziju/port i zatečeno stanje, ispuniti
[BOARD_BRINGUP](templates/BOARD_BRINGUP.md) te dogovoreni oporavak. Potvrditi bootstrap
boot i PSRAM prije audio imagea. Slijedi [P02 audio bringup](P02_AUDIO_BRINGUP.md) s
provjerom ES8311 ACK-a, clockova, tišine i probne note. Nakon toga slijedi
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

- Fizička revizija i priključci, provjeriti kad stigne ploča.
- Želi li korisnik zadržati vanjski ST7789 i zasebnu tipku; zasad headless/Web.
- Obavezne SF2 banke/preseti, njihove veličine/licence; limit 3 MiB je prijedlog.
- OTA signing policy (P07).

Već je odlučeno: **bez microSD, bez WAV snimanja, četiri MIDI looper trake ostaju**.
Ne tražiti ponovno istu odluku i ne vraćati uklonjene funkcije kroz „privremeni” SD backend.

