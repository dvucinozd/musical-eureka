# Stanje — 2026-11-09

**P03 software PASS. Fizički P01 boot, P02 audio i P03 USB MIDI čekaju ploču.**
Aktivni firmware je `0.0.3-usb-midi` u `build-p03/`. P01 i P02 imagei su u
`build/` i `build-p02/`.

## Potvrđeni opseg

ESP32-S31-Function-CoreBoard-1; port iz P4 1.0.0. **Bez microSD i WAV recordera.**
Četiri MIDI looper trake ostaju. SoundFont je planiran iz internog flasha, s novim RAM
ograničenjima. Zaslon/tipkalo i točan fizički primjerak još nisu određeni.
Lokalni zvučnik ostaje zadani izlaz. Bluetooth zvučnik je potvrđeni zahtjev za P06B.

## P03

- S31 HS USB host, ingress i synth su povezani. Audio task obrađuje do 16 događaja
  po bloku uz provjeru USB sesije i epohe synth ulaza.
- **HOST PASS.** 14 skupova. Dodane provjere USB deskriptora, lifecyclea i prolaza
  od USB paketa do synth jezgre, uključujući sustain i prekide prijenosa.
- **BUILD PASS.** ESP-IDF 6.1.0, `espressif/usb` 1.5.0. App ima 406208 B, uz
  642368 B slobodno u app particiji od 1 MiB. ELF sadrži host, ingress i render.
- Linker procjena DIRAM-a je 196188 B zauzeto i 307556 B preostalo. Stvarni heap
  i latencija ovise o runtimeu. Build ima pet upstream CMake upozorenja, a host
  USB lifecycle test šest upozorenja o neiskorištenim parametrima ili varijablama
  zbog stuba koji uklanja logiranje.
- **HARDWARE NOT RUN.** Fizički postupak je u [P03 USB MIDI](P03_USB_MIDI.md).

[Build](evidence/2026-11-09-p03-build.log), [host](evidence/2026-11-09-p03-host.log),
[memorija](evidence/2026-11-09-p03-size.log) i [hashovi](evidence/2026-11-09-p03.json).

## P02

- Preneseni su MIDI parser i red događaja, synth jezgra i četiri looper trake.
  P02 audio task renderira synth i looper te šalje PCM u ES8311.
- S31 profil koristi dokumentirane I2C/I2S pinove i PA GPIO57. UART0 ima `?` za
  status i `t` za jednu tihu probnu notu. [P02 audio bringup](P02_AUDIO_BRINGUP.md).
- **HOST PASS.** Svih 11 relevantnih skupova prolazi nad aktivnim kodom.
- **BUILD PASS.** Aplikacija za ESP32-S31 ima 336032 B i zauzima oko 32 % app
  particije od 1 MiB. Build sadrži ES8311, MIDI parser, synth i looper put.
- **Statička memorija.** Linker procjena DIRAM-a je 184006 B zauzeto od 503744 B.
  Preostalih 319738 B je linker procjena i uključuje prostor potreban za runtime
  alokacije, taskove i DMA. Stvarni heap čeka očitanje s ploče.
- **HARDWARE NOT RUN.** Otvoreni su identitet ploče, boot, PSRAM, ACK, clockovi,
  mute i slušanje.

[P02 build log](evidence/2026-11-09-p02-build.log),
[host log](evidence/2026-11-09-p02-host.log),
[memorija](evidence/2026-11-09-p02-size.log) i
[hashovi](evidence/2026-11-09-p02.json).

## P00 i P01

- Pregledan aktualni P4 izvor na `9c0cf30…`, release/build identitet i glavne port ovisnosti.
- Pregledani službeni vodič, datasheeti, shema i pinned board YAML; dokumentirani pinovi
  i neslaganja komentara. P4 repozitorij ostao je bez izmjena.
- Napisan projektni ugovor, audit, arhitektura, memorija/flash plan, roadmap, test plan,
  registar odluka, razvojne upute, reference i predlošci dokaza.
- U P00 inicijaliziran lokalni Git `main`. Odredište za objavu S31 koda je
  [musical-eureka](https://github.com/dvucinozd/musical-eureka).
- Napravljen ignorirani pinned export 450 izvornih datoteka i tracked SHA-256 manifest.
- Napravljen mali S31 diagnostic firmware, target/version guard, SDK preflight i build alat.
- Napravljen neaktivan nacrt 16 MiB flash particija za dual OTA i dva bank slota.

## P00 i P01 provjere

| Vrsta | Rezultat | Granica dokaza |
| --- | --- | --- |
| SOURCE HOST | **PASS**, 30 testnih skupova | P4 referentni kod; uključuje stare recorder testove, nije S31 engine kvalifikacija |
| S31 BUILD | **PASS**, compile/link i image | ESP-IDF 6.1.0, GCC15.2, target esp32s31; bootstrap aplikacija 202160 B |
| PARTITION DRAFT | **PASS**, IDF parser/validation | Nacrt stane u 16 MiB; nije aktiviran ni flashan |
| Documentation/reference | Alat `tools/verify_project.py` | Lokalne poveznice, source hashovi i geometrija particija |
| BOOT / AUDIO / USB / WIFI / SF32 | **NOT RUN** | Nije pristupljeno fizičkom uređaju |
| OTA / LISTENING / SOAK | **NOT RUN** | Nisu dio dovršene pripreme |

Build ima pet upstream IDF CMake upozorenja o privatnim include direktorijima
`esp_wifi`/`wpa_supplicant`; SDK nije mijenjan. Referentni USB host test stubs daju
šest unused upozorenja u testu koji izvorni runner namjerno izvodi bez `-Werror`.
To je zabilježeno, ne prikriveno kao build bez upozorenja.

Logovi i identitet:

- [Build log](evidence/2026-11-09-bootstrap-build.log)
- [Baseline host log](evidence/2026-11-09-source-host-tests.log)
- [Strojna evidencija i hashovi](evidence/2026-11-09-startup.json)

## Sljedeće

Na identificiranoj ploči redom provjeriti P01 boot, P02 audio i P03 USB MIDI.
Sljedeći software paket je P04 Web, Wi-Fi i četiri MIDI trake.
[Nastavak rada](NEXT_SESSION.md).

