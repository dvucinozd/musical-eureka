# Razvojno okruženje i ponovljivost

## Zamrznuti SDK

- SDK: **ESP-IDF 6.1.0**, službeni tag `v6.1`.
- Commit: `fff9895c82d744c7237be8847347bdd1b07c6643`.
- Lokalno: `C:\esp\v6.1\esp-idf` (prilikom audita bez tracked promjena).
- Aktivacija: `C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1`.
- RISC-V GCC: 15.2.0; esptool u build logu 5.6.0.
- `esp32s31` je u lokalnom `tools/idf_py_actions/constants.py` u `PREVIEW_TARGETS`.
  Stabilan SDK release ne znači da je svaki target u njemu označen stabilnim.

```powershell
Set-Location D:\AI\nektarinaS31
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
.\tools\check_environment.ps1
.\tools\build.ps1 -BuildDirectory build-p05a -SdkConfig build-p05a/sdkconfig
```

`build.ps1` provjerava SDK commit/čistoću, potrebne alate i target, zatim pokreće:

```powershell
python "$env:IDF_PATH\tools\idf.py" --preview -B build-p05a -D IDF_TARGET=esp32s31 -D "SDKCONFIG=$PWD/build-p05a/sdkconfig" build
```

Ne koristiti P4 `idf.py` iz naslijeđenog PATH-a bez aktivacije. Novi target/config
testirati u zasebnom build direktoriju; `sdkconfig` je zajednički projektni generirani
file pa za neovisne konfiguracije treba zaseban `SDKCONFIG` put. Root CMake dodatno
odbija P4 target i SDK verziju različitu od 6.1.0.

Početne postavke: CPU320, OCT PSRAM200, flash16/QIO80 prema službenom board primjeru.
IDF generirani flash argument za bootloader je DIO i kada je QIO odabran u sdkconfigu;
koristiti generirane argumente, ne ručno usklađivati taj string. To nije fizička potvrda.

## Aktivni build i lokalni izvori

`main/app_main.c` ispisuje boot dijagnostiku, pokreće Wi-Fi i ES8311,
obnavlja SoundFont i startup preset te otvara USB MIDI i lokalni Web.
IDF startup prema defaults inicijalizira/testira PSRAM. `dependencies.lock` bilježi
S31 razrješenje `esp_codec_dev` 1.6.2 i `usb` 1.5.0. P04 build je u
`build-p04/`, P05a u `build-p05a/`, a P05b u `build-p05b/`. Zasebni sdkconfig sprječava ponovno
korištenje stare tvorničke particije od 1 MiB.

```powershell
python tools/export_source.py
```

Izvozi samo odabrane **Git tracked** putove na točnom commitu u
`reference/nektar-p4/`, nikad ignorirane vendor mape, privatne ključeve, buildove ili
SF2 banke iz starog working treea. Odbija prepisivanje postojeće kopije i nesigurne
archive putove. Referentni kod je već lokalno pripremljen; nije dio aktivnog builda.

Za baseline host testove koristiti zaseban PowerShell proces zbog izvornog `Set-Location`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\reference\nektar-p4\tests\run_tests.ps1
```

Runner za aktivne komponente koristi GCC iz `C:\msys64\ucrt64\bin` i zapisuje
izvršne datoteke u privremenu mapu.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_p05a_tests.ps1
```

## Particije i statičke provjere

```powershell
python tools/verify_project.py
python "$env:IDF_PATH\components\partition_table\gen_esp32part.py" --flash-size 16MB config/partitions.csv tmp/partitions.bin
```

Tablica je aktivna od P04. Prije fizičkog flasha zapisati identitet uređaja,
image, postojeće particije i način oporavka.

## Verzije, Git i dokazi

Adresa remote repozitorija za S31 koju je dao korisnik je
[https://github.com/dvucinozd/musical-eureka.git](https://github.com/dvucinozd/musical-eureka.git).
Lokalna grana `main` koristi `origin` za taj repozitorij. Vendor PDF-ovi, referentni
export, generirani sdkconfig i build izlazi su ignorirani. Manifesti,
`dependencies.lock` i provjereni tekstualni dokazi pripadaju projektu.

`docs/evidence/` sadrži stvarne logove i strojno čitljivu evidenciju. Build log može
sadržavati IDF-ove generirane upute „To flash”; njihovo prisustvo nije izvedeni flash.
Za svaku buduću fazu ažurirati STATE_SUMMARY/NEXT_SESSION i koristiti
[predložak faze](templates/PHASE_REPORT.md).

