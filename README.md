# NEKTARINA S31

Port projekta **NEKTAR-P4 / NEKTARINA 1.0.0** na **Espressif ESP32-S31-Function-CoreBoard-1**.
Radna mapa: `D:\AI\nektarinaS31`. Datum početka: **2026-11-09**.
Remote za S31: [dvucinozd/musical-eureka](https://github.com/dvucinozd/musical-eureka.git).

**Opseg potvrđen korisnikom: bez microSD kartice i bez WAV snimanja.**
Četiri MIDI looper trake ostaju. SoundFont, USB MIDI, synth, efekti i Web UI ostaju cilj porta;
banke i postavke planiraju se u internom flashu. Zaslon i vanjsko tipkalo još nisu odabrani.
Početni rad može se nastaviti s Web sučeljem bez zaslona.

P04 povezuje USB MIDI, virtualnu tipkovnicu i četiri looper trake s lokalnim
ES8311 izlazom. Web sučelje radi na `http://192.168.8.1/` preko SoftAP-a
`NEKTARINA-S31`. P05b dodaje SF2 upload i sviranje, A/B oporavak banke
pri bootu te jedan startup preset u NVS-u.
P06B dodaje opcionalni razvojni Bluetooth izlaz uz zadani lokalni zvučnik.

## Dokumenti za početak

| Dokument | Sadržaj |
| --- | --- |
| [Projekt i opseg](docs/PROJECT.md) | Funkcije, ograničenja i definicija završetka |
| [Audit izvora](docs/SOURCE_AUDIT.md) | Zamrznuti P4 izvor, konkretne prepreke i što izbaciti |
| [Hardver i pinovi](docs/HARDWARE.md) | Službene specifikacije, shema i otvorene fizičke provjere |
| [P02 audio bringup](docs/P02_AUDIO_BRINGUP.md) | Mute, UART dijagnostika i provjera prve note |
| [P03 USB MIDI](docs/P03_USB_MIDI.md) | USB sesije, panic i postupak provjere GX49 |
| [P04 Web i looper](docs/P04_WEB.md) | AP pristup, virtualna tipkovnica i četiri MIDI trake |
| [P06 SoundFont](docs/P06_SOUNDFONT.md) | PCM usporedba, S31 telemetrija i pilot s 16/32 glasa |
| [P06B Bluetooth](docs/P06B_BLUETOOTH.md) | A2DP razvojni build, Web izbor i fizički pilot |
| [P07 Web OTA](docs/P07_OTA.md) | Razvojni OTA profil, boot potvrda i rollback pilot |
| [Referentni buildovi za bring-up](docs/BRINGUP_BASELINE.md) | Lokalna arhiva, SHA-256 i izbor profila za prvi dan |
| [Arhitektura](docs/ARCHITECTURE.md) | Granice modula, audio task i prijenos postojećeg koda |
| [Flash i memorija](docs/STORAGE_AND_MEMORY.md) | SoundFont bez SD-a, memorijski limiti i prijedlog particija |
| [Razvojno okruženje](docs/DEVELOPMENT.md) | Ponovljivi build i lokalna referentna kopija |
| [Fazni plan](docs/ROADMAP.md) | P00–P08 i izlazni kriteriji |
| [Provjere](docs/VALIDATION.md) | Host, build, fizički testovi i performance ugovor |
| [Odluke i rizici](docs/DECISIONS.md) | Potvrđeno, predloženo i otvoreno |
| [Stanje](docs/STATE_SUMMARY.md) / [nastavak](docs/NEXT_SESSION.md) | Što je učinjeno i sljedeći posao |
| [Izvori](docs/REFERENCES.md) | Linkovi, revizije i podrijetlo podataka |

## Normalni S31 build

```powershell
Set-Location D:\AI\nektarinaS31
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
.\tools\build.ps1 -BuildDirectory build-p07-local -SdkConfig build-p07-local/sdkconfig
```

Potreban je **ESP-IDF 6.1.0**, Git commit `fff9895c82d744c7237be8847347bdd1b07c6643`.
S31 je u tom izdanju **preview target**; skripta koristi `--preview`. Build ne pristupa ploči.
Od P04 koristi se tablica s 3 MiB app slotovima i dvjema SoundFont particijama.

`reference/nektar-p4/` je lokalna, ignorirana kopija 450 datoteka iz točnog commita.
Njezini P4 buildovi i flash naredbe **nisu** upute za S31. Rekonstrukcija:
`python tools/export_source.py` (odbija prepisati postojeću kopiju).
Hash manifest je [reference/source-files.json](reference/source-files.json).

