# Prvi fizički bring-up — 2026-11-11

**P01 BOOT / PSRAM PASS.** Na ploči ostaje arhivirani `0.0.1-bootstrap`.
Audio, USB MIDI, Web, SoundFont, Bluetooth i OTA još nisu fizički provjereni.
[Strojni zapis i hashovi](evidence/2026-11-11-p01-hardware.json).

## Identitet i zatečeno stanje

- ESP32-S31-Function-CoreBoard-1, PCB **V1.0**, UART USB-C prema korisniku.
- **COM7**, CP210x USB–UART, VID `10C4`, PID `EA60`. Drugim portovima nije pristupano.
- ROM `esp32s31-20251218`, SoC **v0.0**, eFuse block revision v0.2 iz boot loga.
- SPI flash ID `46:4018`, fizički kapacitet **16 777 216 B**.
- OCT PSRAM AP, **16 777 216 B**, startup test prolazi pri **200 MHz**.
- Bootstrap CPU **320 MHz**, dvije jezgre. Esptool informativno prikazuje 300 MHz,
  dok boot log stvarnog firmwarea potvrđuje konfiguriranih 320 MHz.
- Secure Boot i flash encryption isključeni. eFuseovi nisu mijenjani.
- Zvučnik nije spojen. Fotografija PCB-a i doslovna oznaka modula još nisu zabilježene.

Tvornički firmware je `esp32_s31_function_coreboard_fa`, `818c539-dirty`,
ESP-IDF `v6.2-dev-1135-ge36835fe0ca-dirt`. Zatečeni boot prijavljuje audio loopback.
Njegov image header deklarira 2 MiB flasha, iako alat očitava fizičkih 16 MiB.
Particije su NVS `0x9000/0x6000`, PHY `0xf000/0x1000` i factory
`0x10000/0x100000`. To je donor zatečeno stanje, bez kvalifikacije naše aplikacije.

## Sigurnosna kopija i oporavak

Lokalna, Git-ignorirana kopija cijelog flasha:
`build-device-20261111/factory-flash-16MiB.bin`, **16 777 216 B**.

SHA-256:
`fd8b53fde3ba788ed8c4e6178f91a64a346a0072072400897dc2b4a704da47ee`.

MD5 kopije i cijelog uređaja prije upisa podudaraju se:
`04ac990fdce97e117cd4c721eb8ef02c`.
Pročitano je 1 310 720 B u zasebnim MD5-provjerenim blokovima od 16 KiB.
Preostalih 15 466 496 B rekonstruirano je kao `FF` tek nakon usporedbe MD5-a
svakog raspona s uređajem. Potom je provjeren MD5 cijele složene kopije.
Sačuvani su i podaci izvan tvorničke particijske tablice.

Kontinuirani stub read pri 460800 i 230400 prekinuo se, a jedan read od 64 KiB
pri 460800 nije prošao digest. ROM read prvih 64 KiB pri 115200 i stub read
pri 115200/230400 dali su isti SHA-256. SDK i esptool nisu mijenjani.
Lokalni postupak, blokovi, manifest i izvorni logovi ostaju u
`build-device-20261111/`. Ne koristiti neuspjele pokušaje kao backup.

Oporavak download načina prema [službenom vodiču](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-function-coreboard-1/user_guide.html):
držati BOOT, pritisnuti RESET, zatim otpustiti BOOT. Automatski ulazak preko
UART bridgea i povrat u SPI boot provjereni su u ovoj sesiji. Ručni postupak
još nije fizički uvježban. Pri sljedećoj sesiji ponovno provjeriti identitet porta.

Eventualna potpuna obnova koristi navedenu kopiju na adresi `0x0`, uz prethodnu
provjeru SHA-256 i identiteta ploče. Obnova nije izvedena. Ne pokretati erase
ili eFuse naredbe u sklopu rutinskog bring-upa. Kopiju čuvati i izvan ovog računala
prije veće testne kampanje.

## Instalirani image i rezultat

Odabran je `build-bringup-kit/bootstrap/`, provjeren prema
[manifestu arhive](evidence/2026-11-09-bringup-kit.json).
P01 ima vlastiti povijesni raspored s factory slotom od 1 MiB.
Njegove argumente ne miješati s novijim profilima.

Iz mape tog profila izvršeno je pinned SDK Python okruženjem:

```powershell
python -m esptool --chip esp32s31 --port COM7 --baud 115200 --no-stub --after no-reset write-flash '@flash_project_args'
```

Bootloader na `0x2000`, particije na `0x8000` i aplikacija na `0x10000`
prošli su esptool hash provjeru. Nije rađen cjeloviti erase flasha.
Nakon dva UART bridge reseta uspješno su snimljeni boot prozori od 12 i 8 sekundi.
Oba potvrđuju IDF v6.1, `0.0.1-bootstrap`, PSRAM test OK i `diagnostic_complete=1`.
To su kratke startup provjere, bez soak ili performance prihvaćanja.

| Slobodna memorija u P01 bootstrapu | Bajtovi |
| --- | ---: |
| Internal | 457 756 |
| Najveći internal blok | 409 600 |
| DMA | 425 748 |
| PSRAM | 16 774 632 |

Ove vrijednosti nisu heap pune audio/radio aplikacije.

[Flash log](evidence/2026-11-11-p01-flash.log) ·
[Prvi boot](evidence/2026-11-11-p01-boot.log) ·
[Ponovljeni boot](evidence/2026-11-11-p01-boot-repeat.log).

## Sljedeće

Pripremiti odgovarajući zvučnik za J9. Vendor vodič navodi 4 Ω / 3 W.
J9 je diferencijalni izlaz pojačala, bez spajanja OUT− na GND.
Zatim zasebno provesti [P02](P02_AUDIO_BRINGUP.md): ES8311 ACK,
mute, I2S clockovi, UART status i jedna tiha nota uz korisnikovu slušnu potvrdu.
Aktualni P02 profil i njegove hashove provjeriti prema [nastavku rada](NEXT_SESSION.md).
