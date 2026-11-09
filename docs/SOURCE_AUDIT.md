# Audit NEKTAR-P4 izvora — 2026-11-09

## Identitet i autoritet

- Izvor: `D:\AI\NEKTAR-P4`, branch `main`, čist working tree tijekom audita.
- HEAD i dereferencirani `v1.0.0`: `9c0cf30b556501a6bb7bd4ffe348f6d7b83bc037`.
- Firmware build commit iz release bilješki: `430d27a2bd4edd64b843627ada6e363b302561db`.
- Git diff između ta dva commita je prazan za `main`, `components`, CMake, defaults,
  particije i lock. Kasniji dokumenti/testni alati ne mijenjaju taj firmware.
- Pročitani README, aktualni odjeljci STATE_SUMMARY/NEXT_SESSION i RELEASE_1.0.0,
  build/board/audio/USB/Wi-Fi/SD/SF/recorder integracija, test runner i licence.
  Ovo je ciljani port audit, nije potpuni redak-po-redak code review.
- README i stari dokumenti sadrže ranije otvorene R4 nalaze. Release bilješke
  2026-11-05 opisuju kasnije prihvaćanje; ne vraćati projekt na raniji 0.26.0 baseline.

## Mapa promjena

| Izvorni put | Postupak |
| --- | --- |
| `components/midi/` | Prenijeti parser/queue; ponovno host testirati |
| `components/synth/` | Prenijeti engine, četiri looper trake, sound refs i vlasništvo nota |
| `components/soundfont/` | Prenijeti validator/engine/lifecycle; parametrizirati memorijske limite |
| `components/usb_midi_host/` | Prenijeti lifecycle i descriptor parser; provjeriti S31 HS port/API |
| `components/board_jc_esp32p4_m3/` | Ne koristiti kao S31 BSP; izdvojiti neutralno board sučelje |
| `components/audio_hal/` | Prilagoditi board include, I2S/codec, clock i IRQ; zadržati timing ugovor |
| `components/saturation_iram/` | Ponovno provjeriti IDF 6.1 libc archive/layout i ELF; ne kopirati objekte |
| `main/app_wifi.c` | Ukloniti esp_hosted init/state; nativni esp_wifi uz retry/regresije |
| `main/app_soundfont.c`, `app_sf_live.c`, `app_sf_last.c` | Zamijeniti SD/file backend flash bank storeom, ID banke/hashom i novim limitima |
| `main/app_storage.*`, `app_preset_file.*`, `app_settings.*` | Razdvojiti preset/NVS i bank store; ukloniti recorder/SD lease grananje |
| `main/app_sdcard.*`, `main/app_recorder.*` | Ne unositi u aktivni S31 port |
| `main/app_main.c` | Novi redoslijed init-a bez SD testa i recorder init-a; ukloniti D/E/R SD/WAV konzolne funkcije |
| `main/app_web_gui.c`, `index.html`, `gui.css`, generirani `.inc` | Ukloniti recorder, Audio Takes i microSD UI/API/polling; napraviti flash bank kontrole |
| `main/app_display.c`, `app_button.c` | Display opcionalan; GPIO47 je hardkodiran u button.c, mora u novi profil |
| `main/app_security.c`, `app_ota_health.c` | Sačuvati korisno ponašanje, ali novi S31 image/particije/ključevi i health kriteriji |
| `main/idf_component.yml`, `dependencies.lock` | Novi resolution za S31; izbaciti esp_wifi_remote i esp_hosted transportni lanac |
| `sdkconfig.defaults`, root CMake | Novi target/revizija/PSRAM/mreža, ne zamjena jednog stringa |

## Konkretni nalazi

1. P4 koristi 360 MHz, 32 MB HEX PSRAM i 16 MB flash; S31 ploča prema vodiču
   ima 16 MB OCT PSRAM. Izvorni SF worker ima `PSRAM_CAP=22 MiB`, rezervu 4 MiB
   i `FILE_CAP=12 MiB`. Validator također ima tvrdi 22 MiB staging peak i 20 MiB
   decoded cap; HTTP zasebno ponavlja 12 MiB. Sva mjesta moraju dijeliti novu politiku.
2. SF2 PCM16 uzorci pretvaraju se u float: validator računa `decoded_bytes=sample_bytes*2`.
   Blob datoteke i dekodirani uzorci privremeno koegzistiraju, uz aktivnu staru banku.
3. `app_sdcard.c` koristi P4 on-chip LDO i SDMMC. Ukloniti ga, ne emulirati karticu u flashu.
4. `app_wifi.c` eksplicitno uključuje `esp_hosted.h`; main CMake zahtijeva
   `espressif__esp_hosted`. Samo uklanjanje manifest dependencyja nije dovoljno.
5. USB adapter hardkodira HS peripheral mask 1 s P4 komentarom. S31 IDF HAL ima
   jedan HS DWC; API/index provjeriti pri build integraciji, a enumeraciju fizički.
6. `saturation_iram/prepare.py` hardkodira picolibc multilib put. CMake danas uključuje
   SAT_IRAM/O3/audio O2/IRAM renderer i Q cache, iako stari README komponente kaže OFF.
   Autoritet je aktualni CMake/ELF. Novi toolchain zahtijeva novi dokaz, ne stari PASS.
7. WAV ring ima 262144 `int16_t` uzoraka (512 KiB PSRAM-a). Uklanjanje štedi taj ring
   i recorder task, ali ne rješava samo po sebi razliku 32 → 16 MB PSRAM-a.
8. Izvorna fizička izlazna putanja je mono; stereo FX naziv ne znači dva DAC kanala.

## Testovi i izostavljene kampanje

Zamrznuta referentna kopija prošla je 30 skupova `tests/run_tests.ps1` na hostu.
To uključuje i stare SD/recorder testove kao dokaz baselinea; oni nisu budući S31 kriteriji.
Nisu pokrenuti puni SoundFont sanitizer/fuzz/golden runneri ni P4 hardware testovi.
U S31 zadržati MIDI, USB cleanup, synth, looper, note ownership, SF lifecycle/parser,
NVS i relevantne UI testove. Zamijeniti SD/WAV testove flash integrity/recovery testovima.

`tools/soundfont/wav_campaign.py` i `tools/sdcard_fill_test.py` ne pripadaju S31 planu.
`upload_campaign.py`, R4 harness i lifecycle runneri trebaju novi API i kriterije bez SD/WAV-a.
Licencni pregled: [THIRD_PARTY](../THIRD_PARTY.md). Detaljni hashovi: [manifest](../reference/source-files.json).

