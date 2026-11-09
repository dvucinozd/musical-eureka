# NEKTARINA S31

Port projekta **NEKTAR-P4 / NEKTARINA 1.0.0** na **Espressif ESP32-S31-Function-CoreBoard-1**.
Radna mapa: `D:\AI\nektarinaS31`. Datum početka: **2026-11-09**.
Remote za S31: [dvucinozd/musical-eureka](https://github.com/dvucinozd/musical-eureka.git).

**Opseg potvrđen korisnikom: bez microSD kartice i bez WAV snimanja.**
Četiri MIDI looper trake ostaju. SoundFont, USB MIDI, synth, efekti i Web UI ostaju cilj porta;
banke i postavke planiraju se u internom flashu. Zaslon i vanjsko tipkalo još nisu odabrani.
Početni rad može se nastaviti s Web sučeljem bez zaslona.

Aktivni firmware `0.0.3-usb-midi` spaja USB MIDI na synth i lokalni ES8311 izlaz.
Pri pokretanju je pojačalo utišano. UART0 naredba `?` ispisuje stanje, a `t`
pokreće kratku probnu notu prije prve USB MIDI sesije. Planirani P06B dodaje
Bluetooth zvučnik kao opciju uz zadani lokalni izlaz.

## Dokumenti za početak

| Dokument | Sadržaj |
| --- | --- |
| [Projekt i opseg](docs/PROJECT.md) | Funkcije, ograničenja i definicija završetka |
| [Audit izvora](docs/SOURCE_AUDIT.md) | Zamrznuti P4 izvor, konkretne prepreke i što izbaciti |
| [Hardver i pinovi](docs/HARDWARE.md) | Službene specifikacije, shema i otvorene fizičke provjere |
| [P02 audio bringup](docs/P02_AUDIO_BRINGUP.md) | Mute, UART dijagnostika i provjera prve note |
| [P03 USB MIDI](docs/P03_USB_MIDI.md) | USB sesije, panic i postupak provjere GX49 |
| [Arhitektura](docs/ARCHITECTURE.md) | Granice modula, audio task i prijenos postojećeg koda |
| [Flash i memorija](docs/STORAGE_AND_MEMORY.md) | SoundFont bez SD-a, memorijski limiti i prijedlog particija |
| [Razvojno okruženje](docs/DEVELOPMENT.md) | Ponovljivi build i lokalna referentna kopija |
| [Fazni plan](docs/ROADMAP.md) | P00–P08 i izlazni kriteriji |
| [Provjere](docs/VALIDATION.md) | Host, build, fizički testovi i performance ugovor |
| [Odluke i rizici](docs/DECISIONS.md) | Potvrđeno, predloženo i otvoreno |
| [Stanje](docs/STATE_SUMMARY.md) / [nastavak](docs/NEXT_SESSION.md) | Što je učinjeno i sljedeći posao |
| [Izvori](docs/REFERENCES.md) | Linkovi, revizije i podrijetlo podataka |

## Build početnog projekta

```powershell
Set-Location D:\AI\nektarinaS31
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
.\tools\build.ps1 -BuildDirectory build-p03
```

Potreban je **ESP-IDF 6.1.0**, Git commit `fff9895c82d744c7237be8847347bdd1b07c6643`.
S31 je u tom izdanju **preview target**; skripta koristi `--preview`. Build ne pristupa ploči.
Početni image ima IDF single-app tablicu particija; nacrt za završni port nije aktiviran.

`reference/nektar-p4/` je lokalna, ignorirana kopija 450 datoteka iz točnog commita.
Njezini P4 buildovi i flash naredbe **nisu** upute za S31. Rekonstrukcija:
`python tools/export_source.py` (odbija prepisati postojeću kopiju).
Hash manifest je [reference/source-files.json](reference/source-files.json).

