# Fazni razvojni plan

Ne preskakati dokaz u prethodnoj fazi koji je preduvjet sljedeće. Host rad na neovisnim
modulima može napredovati dok ploča nije dostupna. Procjene vremena nisu postavljene.

| Faza | Isporuka | Izlazni kriterij | Status 2026-11-09 |
| --- | --- | --- | --- |
| P00 | Source audit, shema, opseg, odluke i baseline manifest | Izvori i rizici navedeni; no-SD/no-WAV odluka ugrađena | GOTOVO |
| P01 | Pinned SDK, minimalni S31 projekt i ponovljivi alati | Host baseline i S31 build; fizički boot zasebno | SOFTWARE GOTOVO, boot NOT RUN |
| P02 | Board/audio adapter i prijenos synth/MIDI jezgre | Build, host regresije, ES8311 ACK, mute, clockovi i kontrolirana prva nota | SOFTWARE PASS, hardware NOT RUN |
| P03 | USB MIDI host i live synth | GX49 enumerate, NoteOn/Off/CC, unplug/reconnect, bez zapele note | SOFTWARE PASS, hardware NOT RUN |
| P04 | Native Wi-Fi, Web kontrole, četiri looper trake | Virtual keys i source ownership, retry, looper i UI bez SD/WAV-a | SOFTWARE PASS, hardware NOT RUN |
| P05 | Flash bank store, NVS preset, SF worker | Particije, prekidi upisa/recovery, hash, allocator cap, boot restore | SOFTWARE PASS, HARDWARE NOT RUN |
| P06 | SF render/S31 optimizacija i integracija | 32 sample glasa prema novom mjernom ugovoru ili dokumentiran neuspjeh | HOST / BUILD PASS, HARDWARE NOT RUN |
| P06B | Opcionalni Bluetooth izlaz za zvučnik | Odabir lokalnog ili BT izlaza, uparivanje, promjena izlaza, prekid veze, kašnjenje i suživot s Wi-Fi/USB | HOST / BUILD / BROWSER PASS, HARDWARE NOT RUN |
| P07 | OTA i opcionalne fizičke kontrole | Wrong-target reject, rollback, health; LCD samo ako je odabran | RAZVOJNI HOST / BUILD / BROWSER PASS, HARDWARE NOT RUN |
| P08 | Završna kvalifikacija i izdanje | Zamrznuti image, host/build/hardware dokazi, slušanje i release limits | NIJE POČELO |

## P02 — konkretni prvi port paket

1. U aktivne `components/` kopirati samo planirani MIDI/synth kod iz pinned reference,
   uz license/provenance; izdvojiti minimalni host test runner.
2. Uvesti neutralni board API i S31 profil s pinovima iz HARDWARE; bez SD/C6 struktura.
3. Prilagoditi audio HAL i pinati `esp_codec_dev` tek nakon S31 dependency resolutiona.
   Kandidati iz izvora su codec 1.6.2 i usb 1.5.0, nisu već potvrđeni za novi build.
4. Sačuvati PCM/volume/filter ponašanje. Recorder tap ukloniti; ne uključivati SF
   optimizacije ovisne o toolchainu prije analize novog ELF-a.
5. Build i host PASS; zatim, kad fizički rad bude dio zadatka, identificirati ploču,
   očuvati zatečeno stanje i potvrditi bootstrap boot/PSRAM prije audio pokušaja.
6. PA LOW, codec I2C adresa, I2S clockovi, tišina pa kontrolirana nota niske razine.
   Rezultat slušanja bilježi korisnik; build nije zamjena.

## Rad prije dolaska ploče

Ploča još nije kod korisnika. Redoslijed za rad na računalu je P03 USB MIDI,
P04 Web i looper, pa P05 SoundFont i pohrana. P01 i P02 fizičke provjere ostaju
prve kad ploča stigne. Svaki paket zasebno bilježi host testove i S31 build.

| Faza | Što možemo napraviti na računalu | Što provjeravamo na ploči |
| --- | --- | --- |
| P03 USB MIDI | Prenijeti host i MIDI ingress, provjeriti deskriptore, granice reda, session/epoch i panic. Stubovima simulirati odspajanje, zakašnjeli paket i ponovno spajanje. Kompajlirati S31 driver i povezati obradu s audio taskom. | Enumeracija GX49, stvarni Note On/Off i CC, odspajanje za vrijeme držanja note, latencija od tipke do zvuka. |
| P04 Web, Wi-Fi i looper | Prenijeti nativni Wi-Fi adapter i HTTP rute, uvesti zajedničko vlasništvo nota. Testirati Web sučelje s lokalnim simuliranim API-jem i četiri MIDI trake. | Rad pristupne točke, stvarni HTTP odziv i istodobno sviranje preko USB-a i Weba. |
| P05 SoundFont, flash i NVS | Prenijeti SF2 validator, engine i lifecycle. Testirati veličine, CRC/SHA i oporavak A/B banki na modelu flasha s prekidom između koraka upisa. Provjeriti particije i build S31 adaptera. | Stvarni upis i oporavak nakon prekida napajanja, očitanje slobodne PSRAM i DMA memorije te mute/stop uz flash operacije. |
| P06 Render | Provjeriti referentni PCM, polyphony/voice stealing, raspodjelu i vrh alokacija. Pripremiti instrumentaciju za mjerenje na ploči. | Odrediti stvarni kapacitet i performanse S31. Izmjeriti 32 sample glasa, rok bloka od oko 2,67 ms i ponašanje uz USB i Wi-Fi. |
| P06B Bluetooth | Kompajlirati A2DP Source primjer za S31 i testirati logiku izbora izlaza i ograničenog PCM reda. | Uparivanje sa zvučnikom, audio format, realno kašnjenje, prekid veze i istodoban rad Wi-Fi/USB. |
| P07–P08 OTA i kvalifikacija | Pripremiti provjeru ciljnog čipa, veličine i formata imagea te testirati logiku OTA stanja. Pripremiti pakiranje i kriterije izdanja. | Provjeriti flash, boot, rollback, duže sviranje, slušanje i opcionalne fizičke kontrole. |

**Provjereno 2026-11-09.** P4 host USB deskriptori, lifecycle i MIDI ingress imaju
testove u pinned referenci. Lokalni S31 SDK deklarira jedan USB OTG kontroler.
Registry za `espressif/usb` 1.5.0 navodi `esp32s31`. To je početni kandidat za
razrješenje i build. P4 manifest veže IDF na 6.0.2 pa ga treba prilagoditi S31
projektu koji koristi 6.1.0.
[Espressif USB Host 1.5.0](https://components.espressif.com/components/espressif/usb/versions/1.5.0/readme?language=en).

Iz pinned reference pokrenuti su i prošli SoundFont `test_contract.c` i
`test_lifecycle.c` s MSYS2 GCC-om i referentnim SF2 fixtureima. Time su provjereni
DSP ugovor i životni ciklus banke na računalu. S31 integracija, stvarni flash i
rokovi izvođenja ostaju zasebni kriteriji.

P03 je prošao host testove i S31 build. Dok nema ploče, isti kriterij softverskog
prolaza vrijedi za P04 i P05. Status ostaje `HARDWARE NOT RUN` do mjerenja na uređaju.

## P04–P05 — promjena opsega

Prije UI integracije izbaciti WAV i microSD kontrole/rute/status/polling. Ostaje looper
REC. Uvesti jasne „Banks”, upload, validate/load i storage-busy statuse. Preseti idu u
internu pohranu. Upload/OTA/NVS save tijekom sviranja imaju dokumentirano odbijanje ili
koordiniran prijelaz u mirno stanje; ne resetirati sadržaj looper traka bez razloga.

## P06–P08 — nema naslijeđenog performance PASS-a

Ne obećavati ubrzanje iz odnosa 320/360 MHz. Mjeriti novi compiler, memory layout,
cache, nativni Wi-Fi i event setup. P4 iznimka za jedan preparation deadline tijekom
uploada ne postaje S31 kriterij. Nema WAV ni SD soak kampanja; koriste se audio/MIDI/Web
soak i zasebne flash integrity/recovery kampanje. Detalji u [VALIDATION](VALIDATION.md).

