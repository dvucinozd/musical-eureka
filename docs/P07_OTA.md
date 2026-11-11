# P07 — razvojni Web OTA

P07 koristi dvije postojeće aplikacijske particije od 3 MiB i `otadata`.
Razvojni profil uključuje IDF bootloader rollback i Web upload. Za prvi
fizički pilot potreban je P07 bootloader uz P07 početnu aplikaciju i
odgovarajuću tablicu particija. Početnu instalaciju planirati nakon
[identifikacije ploče i postupka oporavka](NEXT_SESSION.md).

## Build i paket

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
.\tools\build.ps1 -BuildDirectory build-p07 -SdkConfig build-p07/sdkconfig -SdkConfigDefaults @('sdkconfig.defaults','config/sdkconfig.p07.defaults')
.\tools\build.ps1 -BuildDirectory build-p07-local -SdkConfig build-p07-local/sdkconfig
.\tests\run_p07_tests.ps1
Get-FileHash -Algorithm SHA256 -LiteralPath build-p07/nektarina_s31.bin
```

Za upload koristi samo aplikacijski `nektarina_s31.bin` iz P07 profila i
njegov očekivani SHA-256. Profil u descriptoru nosi oznaku `NEKTAR-S31-P07`
uz S31 chip ID i naziv projekta. Lokalni `sdkconfig` ili ignorirani overlay
za instrument treba postaviti jedinstvenu Wi-Fi lozinku od 12–63 znaka.
Sa zadanom razvojnom lozinkom upload je blokiran i panel je skriven.
Lozinka ulazi u image koji se koristi u pilotu. OTA paket zadržava pristupne
postavke samo ako je i sam buildan s odgovarajućim postavkama.

Profil `build-p07-local/` ima isključen Web OTA. Razvojni OTA profil
provjerava integritet SHA-256. Potpisivanje
izdanja i ključ za S31 ostaju otvoreni. Secure Boot i eFuse anti-rollback
nisu dio ovog profila.
[ESP-IDF 6.1 OTA i rollback](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s31/api-reference/system/ota.html).

## Upload i potvrda boota

1. Otpusti tipke i zaustavi looper. U odjeljku **Development firmware update**
   odaberi datoteku, unesi očekivani hash i potvrdi **Upload and restart**.
2. HTTP primi kompletan body u PSRAM uz ograničenje veličine i vremena.
   Preflight provjerava target, aplikaciju, profil, strukturu segmenata,
   checksum, ugrađeni SHA-256 i SHA-256 cijele datoteke. IDF dodatno provjeri
   kompatibilnost revizije čipa i načina rada flasha.
3. Nakon audio maintenance ACK-a OTA provjerava da je aktivna aplikacija
   označena kao VALID, da je odabrana za boot i da postoji drugi app slot.
   Upisuje samo taj slot. IDF potvrđuje image kroz `esp_ota_end` prije
   promjene boot selekcije. Ako ne uspije, audio izlazi iz maintenancea.
4. Nakon promjene selekcije uređaj odgovara da je firmware spreman i restartira
   se s utišanim izlazom. Na prvom bootu u stanju PENDING_VERIFY provjerava
   Wi-Fi inicijalizaciju, uspješan start USB MIDI hosta, start HTTP servera
   s registriranim rutama, PSRAM i oko jedne sekunde rada lokalnog audio taska
   bez novih deadlinea, DMA prelijevanja i grešaka. Opcionalni Bluetooth
   profil provjerava i rezultat pokretanja Bluetootha. Tijekom provjere Web
   naredbe vraćaju HTTP 503, a USB MIDI paketi se odbacuju. Uspjeh zapisuje
   VALID pod audio maintenanceom i zatim otvara ulaze. Greška pri pokretanju
   servisa ili provjeri izaziva restart u stanju PENDING_VERIFY kako bi
   bootloader vratio prethodni slot. Za ovu provjeru USB klavijatura ne mora biti spojena.

## Fizički pilot

- Prvo potvrdi zdravi početni P07 boot i VALID stanje. Zabilježi hash obaju
  imagea i aktivni slot preko serijskog loga. Nakon uspješnog uploada potvrdi
  novi slot, oznaku VALID, Web, MIDI i lokalni zvuk.
- Pokušaj upload P4 ili starijeg S31 imagea, krivi SHA-256, prekid prijenosa
  te upload dok svira live nota ili looper. Provjeri da aktivni boot ostaje
  isti i da audio ostaje upotrebljiv. Prekid tijekom flash upisa ispituje
  se odvojeno od prekida mrežnog prijenosa.
- Za rollback pilot kontrolirano prekinuti prvi boot dok je kandidat
  PENDING_VERIFY, uz potvrđen serijski oporavak. Zabilježiti povratak na
  poznati slot i provjeriti podatke NVS/SoundFont. Fizički OTA, prekid
  napajanja, rollback i audio nakon restarta ostaju **HARDWARE NOT RUN**.

[Build, host i browser dokazi](evidence/2026-11-09-p07.json).
