# Referentni S31 buildovi za prvi dan s pločom

Arhiva `build-bringup-kit/` čuva lokalne kopije binarnih datoteka, ELF-a,
konfiguracija i generiranih flash argumenata za odgovarajući image.
Git čuva [SHA-256 manifest](evidence/2026-11-09-bringup-kit.json) i build
dokaze. Za provjeru lokalne arhive pokreni:

```powershell
python tools/verify_bringup_kit.py
```

| Profil u arhivi | Upotreba nakon fizičke identifikacije |
| --- | --- |
| `bootstrap` | Povijesni P01 image za PSRAM i boot dijagnostiku. Njegova particijska tablica pripada P01 rasporedu. |
| `local` | Prvi puni lokalni audio/USB/Web/SoundFont pilot. |
| `soundfont-telemetry` | Zasebni P06 image za 16/32 sample glasa. |
| `bluetooth` | Izolirani P06B pilot ako treba odvojiti Bluetooth od OTA-a. |
| `ota` | Izolirani P07 rollback profil. |
| `bluetooth-ota` | Zajednički build za pilot nakon osnovnog lokalnog bring-upa. |

Za povijesni P01 `sdkconfig` referenca je hash iz
[P01 dokaza](evidence/2026-11-09-startup.json). Arhiva P01 profila sadrži
izvorne binarne datoteke i generirane flash argumente.

Zajednički build koristi pinned ESP-IDF 6.1.0 i oba postojeća overlaya:

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
.\tools\build.ps1 -BuildDirectory build-p07-combined -SdkConfig build-p07-combined/sdkconfig -SdkConfigDefaults @('sdkconfig.defaults','config/sdkconfig.p06b.defaults','config/sdkconfig.p07.defaults')
.\tests\run_p07_tests.ps1 -Image build-p07-combined/nektarina_s31.bin
```

OTA kontrola je u arhiviranim buildovima zatvorena zadanom razvojnom
Wi-Fi lozinkom. Za stvarni OTA pilot pripremi zaseban P07 build s privatnom
lozinkom i zabilježi novi hash. [OTA postupak](P07_OTA.md)

Prije pristupa uređaju utvrdi reviziju, port, stanje particija i način
oporavka. Odaberi jedan profil i koristi pripadajuće argumente i artefakte.
Fizički boot, zvuk, Bluetooth i OTA rollback čekaju ploču.
[Redoslijed provjera](NEXT_SESSION.md) · [Predložak prvog boota](templates/BOARD_BRINGUP.md)
