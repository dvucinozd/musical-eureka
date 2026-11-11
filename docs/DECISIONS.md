# Odluke, pretpostavke i rizici — 2026-11-09

## Potvrđeno korisničkim zahtjevom

- D01: novi zaseban projekt u `D:\AI\nektarinaS31`, izvor `D:\AI\NEKTAR-P4`.
- D02: ciljna ploča **ESP32-S31-Function-CoreBoard-1**.
- D03: **nema microSD**.
- D04: **izbaciti WAV snimanje**. Ne znači izbaciti MIDI looper.
- D14: **zvučnik spojen na ploču je primarni izlaz.** Korisnik želi mogućnost
  odabira Bluetooth zvučnika za isti synth, SoundFont i looper miks.

## Razvojne odluke ovog početnog paketa

- D05: C/ESP-IDF port, zamrznuti P4 1.0.0 commit; bez Rust rewritea.
- D06: SDK 6.1.0 / `fff9895…`, S31 preview flag; početno službeni board clock/memory defaults.
- D07: odvojeni bootstrap; bez kopiranja P4 aplikacije u aktivni build prije adapcije.
- D08: fizički rad počeo je 2026-11-11 na korisnički prijavljenom UART COM7,
  PCB V1.0. P01 boot/PSRAM prošli su nakon provjerene tvorničke kopije.
  Identitet porta ponovno se provjerava pri svakoj sesiji. P02–P07 ostaju NOT RUN.
- D15: jedan verzionirani startup preset u NVS-u. Identitet SF2 izbora koristi
  SHA-256 banke i program. Bez startup preseta boot bira prvi program
  pripremljene banke, a nedostupan spremljeni zvuk pada na spremljeni synth.
- D16: P06B je opt-in profil. Lokalni izlaz je zadan, gubitak BT veze
  utišava odabrani izlaz, a SSP bond ključevi žive u RAM-u do restarta.
- D17: P07 OTA je opt-in razvojni profil s provjerom SHA-256 i privatnom
  AP lozinkom. Potpis i ključevi izdanja ostaju otvoreni.

## Projektni prijedlozi koji se još kvalificiraju

- D09: SF bank A/B raw flash, NVS preseti, najviše 3 MiB datoteka / 10 MiB SF peak uz
  4 MiB free rezervu. To su početne granice, ne dokazana podrška svakoj takvoj banci.
- D10: upload/load/NVS/OTA u koordiniranom stanju bez sviranja zbog flash/cache utjecaja.
- D11: 16 live + 4×4 looper glasa ostaju cilj, 48 kHz/128 frames i mono ES8311.
- D12: prvo headless/Web. ST7789 i vanjsko tipkalo čekaju izbor; pinovi nisu dodijeljeni.
- D13: nativni Wi-Fi AP za početnu kontrolu; konačni SSID/pristup i mogući STA način
  dokumentirati pri P04. Ne tvrditi da naslijeđena P4 mrežna politika već vrijedi za S31.

## Otvoreno i rok odluke

| Pitanje | Kada je potrebno | Trenutno ponašanje |
| --- | --- | --- |
| Oznaka modula/fotografija i ručni oporavak | Dopuniti pri sljedećem fizičkom radu | PCB V1.0, SoC v0.0 i UART COM7 zabilježeni, automatski reset provjeren |
| ST7789 ili bez zaslona, vanjsko tipkalo | Prije P07 / ranije ako korisnik odabere | Headless/Web |
| Koje SF2 banke/preseti su obavezni | Prije zatvaranja P05 RAM limita | Mali legalno dostupan fixture; bez bundled banke |
| Fizička provjera 16 MiB rasporeda i NVS startup preseta | P05 | Aktivni raspored u S31 buildu i format NSP1 |
| OTA signing policy i ključ za novi projekt | Prije OTA izdanja | Razvojni profil s integritetom SHA-256 |

## Rizici i odgovor

| Rizik | Odgovor / dokaz |
| --- | --- |
| 16 MB PSRAM umjesto 32 MB | Staging estimator + cap allocator + largest block; fail prije aktivacije |
| S31 preview i preliminarni datasheet | Pin SDK/revizije; novi boot/peripheral testovi |
| Manji takt i novi memory/radio contention | Stvarni S31 timing; ne preslikavati P4 throughput |
| Flash write ometa audio/PSRAM | Maintenance protokol, ingress gate i recovery testovi |
| Nesklad vendor komentara i sheme | Shema vodi pinove, mjerenje potvrđuje polaritet/clockove |
| Toolchain-specific IRAM/libc optimizacija | Novi archive/hash/ELF audit i PCM usporedba |
| Ostaci SD/WAV UI/backend koda | Removal checklist i ciljani API/UI testovi u P04/P05 |
| Stari P4 dokumenti nose različite statuse | Release commit/notes i aktualni CMake imaju prednost |

