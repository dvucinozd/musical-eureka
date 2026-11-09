# P06B Bluetooth zvučnik

Opcionalni razvojni profil šalje završni miks syntha, SoundFonta i četiri
MIDI looper trake na jedan Classic Bluetooth A2DP zvučnik. Lokalni ES8311
izlaz je zadan nakon pokretanja. Normalni profil ima isključen Bluetooth.

## Build i provjera bez ploče

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
.\tools\build.ps1 -BuildDirectory build-p06b -SdkConfig build-p06b/sdkconfig -SdkConfigDefaults @('sdkconfig.defaults','config/sdkconfig.p06b.defaults')
.\tools\build.ps1 -BuildDirectory build-p06b-local -SdkConfig build-p06b-local/sdkconfig
.\tests\run_p06b_tests.ps1
```

Primjer A2DP Source iz pinned SDK-a kompajliran je iz kopije u
`build-p06b-example/`. Aplikacijski profil koristi interne SBC mogućnosti
Bluedroida. Prihvaća pregovoreni PCM format 44,1 ili 48 kHz, mono ili stereo,
uz 16-bitne uzorke. Stereo dobiva isti mono signal u oba kanala.
[ESP-IDF 6.1 A2DP API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s31/api-reference/bluetooth/esp_a2dp.html).

## Web postupak

1. Zvučnik stavi u način uparivanja, otvori instrumentov Web i odaberi
   **Scan for speakers** u odjeljku **Audio output**.
2. Odaberi pronađeni zvučnik pa **Connect**. Nakon što se medijski kanal
   pokrene, **Use Bluetooth speaker** prebacuje izlaz.
3. **Use local speaker** vraća lokalni izlaz. **Disconnect** prekida vezu.
   Ako Bluetooth veza padne dok je izabrana, instrument ostaje utišan.
   Lokalni izlaz vraća se Web odabirom, a ponovno spajanje na zvučnik
   može nastaviti Bluetooth izlaz.

Profil podržava SSP za zvučnike u načinu uparivanja. Odabir zvučnika
autorizira SSP potvrdu samo za tu adresu dok je spajanje u tijeku. Uređaji
koji traže stari PIN zahtijevaju zaseban rad. Bond ključevi ostaju u RAM-u
do restarta ploče. Nakon restarta zvučnik se ponovno odabire u Webu.

## Granice audio puta

- I2S DMA i dalje daje ritam od 48 kHz / 128 frameova. Dok je Bluetooth
  izlaz aktivan, lokalni I2S nosi tišinu, a PA je isključen.
- Audio task upisuje do 128 mono frameova u jedan fiksni red kapaciteta
  4096 frameova (8192 B PCM). Bluedroid callback čita i radi linearnu
  konverziju 48 → 44,1 kHz prema pregovorenom formatu. Prije prvog signala
  i nakon praznog reda čeka se 1024 framea. Na nedostatak podataka šalje
  tišinu, a pun red odbacuje novi blok. Ti brojači, uključujući tišinu pri
  punjenju reda, izloženi su na `GET /api/output`.
- Prebacivanje prema Bluetoothu spušta lokalni signal kroz jedan blok,
  zatim propušta šest tihih DMA blokova prije utišavanja PA i predaje
  signala Bluetoothu. Povratak na lokalni izlaz zatvara PCM red i primjenjuje
  ulaznu rampu. Zvučnik može dovršiti vlastiti ranije primljeni audio.
- U pinned SDK-u `btc_storage_add_bonded_device` odmah poziva
  `btc_config_flush`. P06B build preusmjerava samo Bluedroidove OSI pozive
  `config_new` i `config_save` kako bi konfiguracija ostala u RAM-u.
  Postojeći NVS startup preset i A/B pohrana ostaju pod audio maintenanceom.
  Svaka promjena IDF commita traži ponovnu provjeru ovih poziva.

## Dokazi i fizički pilot

| S31 build | Firmware | Zauzeti DIRAM prema linkeru |
| --- | ---: | ---: |
| A2DP Source primjer | 1 044 720 B | 130 172 B |
| Lokalni profil | 1 227 072 B | 254 786 B |
| Bluetooth profil | 2 014 016 B | 317 138 B |

Bluetooth profil dodaje 786 944 B firmwarea i 62 352 B linkanog DIRAM-a.
Oba aplikacijska profila koriste isti raspored s app slotovima od 3 MiB.
Runtime heap i vršna memorija mjere se na ploči.

- Host provjere pokrivaju ograničenje PCM reda, ponovno spajanje i
  odbacivanje starog PCM-a, wrap brojača, resampling, mono/stereo te logiku
  izbora izlaza. P06 PCM i prethodna host regresija pokrenuti su ponovno.
- Chrome preko Playwrighta na 1280 × 900 i 390 × 844 prošao je simulirani
  slijed scan → connect → Bluetooth → virtualna tipka → gubitak veze →
  lokalni izlaz. Browser plugin nije bio dostupan. U lokalnom profilu
  panel se skriva. [Buildovi, logovi i hashovi](evidence/2026-11-09-p06b.json).
- Na ploči nakon [bring-upa](NEXT_SESSION.md) provjeriti boot i raspoloživi
  heap s uključenim BT-om. Snimiti stvarno uparivanje i pregovoreni format.
  Poslušati prijelaz u oba smjera, provjeriti PA, prekid i ponovno
  spajanje, restart te rad uz USB MIDI i aktivan Wi-Fi. Izmjeriti
  note-to-audio latenciju i pratiti PCM drop/silence, DMA i heap. Usporediti
  s [P06 pilotom](P06_SOUNDFONT.md). **HARDWARE NOT RUN**.
