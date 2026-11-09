# ESP32-S31-Function-CoreBoard-1 — hardverski ugovor

Status 2026-11-09: **dokumentarno provjereno, fizički primjerak nije identificiran**.
Izvori i točne revizije su u [REFERENCES](REFERENCES.md). S31 nije ESP32-S3 niti ESP32-P4.

## Usporedba platformi

| Stavka | P4 baseline | Ciljna ploča |
| --- | --- | --- |
| CPU | Dual-core RISC-V, konfiguriran 360 MHz | Dual-core RISC-V, do 320 MHz |
| SRAM | Ne prenositi raspoloživi heap iz P4 logova | 512 KB shared + 32 KB LP prema module datasheetu; nije sve heap |
| Flash / PSRAM | 16 MB / 32 MB HEX | Vodič navodi 16 MB / 16 MB; OCT prema board defaults |
| Radio | ESP32-C6 preko Hosted SDIO | Ugrađeni Wi-Fi; BT/802.15.4 nisu funkcije početnog porta |
| Audio | ES8311 + PA | ES8311 + NS4150B, mikrofon i speaker konektor |
| USB | P4 HS host | Type-A HS host, do 500 mA; zasebni USB Serial/JTAG i UART Type-C |
| SD / LCD | SD i vanjski ST7789 u izvornom projektu | Nema ugrađenog SD utora/LCD-a; SD isključen iz projekta |
| Ethernet | Nije uvjet izvornog instrumenta | YT8531 RGMII/RJ45; ostaje neaktivan, pinovi ipak zauzeti |

Podaci o ploči: [Espressif vodič](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-function-coreboard-1/user_guide.html).
CPU/memorija: [module datasheet v0.7](https://documentation.espressif.com/esp32-s31-wroom-3_datasheet_en.pdf).
Stvarni flash/PSRAM kapacitet i reviziju potvrditi oznakom modula i boot očitanjem.

## Audio i rezervirani pinovi

Smjer DIN/DOUT uvijek je **u odnosu na MCU**. Mapa je očitana iz sheme V1.0,
2026-05-13, listovi 2 i 3, vizualno provjerena. P02 adapter koristi ovu mapu za DAC
izlaz. Fizički test pinova još je otvoren.

| Signal | GPIO / priključak | Napomena |
| --- | --- | --- |
| ES8311 I2C SDA | 51 | `ESP_I2C_SDA` |
| ES8311 I2C SCL | 50 | `ESP_I2C_SCL` |
| I2S MCLK | 52 | `I2S_MCLK` |
| I2S BCLK | 53 | `I2S_SCLK` |
| I2S WS/LRCK | 55 | `I2S_LRCK` |
| MCU DOUT → codec DAC | 56 | `I2S_DSDIN` |
| MCU DIN ← codec ADC | 54 | `I2S_ASDOUT` |
| PA CTRL | 57 | NS4150B, pulldown; vendor config active-high, početno LOW |
| ES8311 adresa | 0x18 (7-bit), 0x30 (8-bit) | Ne miješati konvencije codec API-ja |
| BOOT | 61 | Active-low, boot strap; ne koristiti kao običnu tipku pri resetu |
| RGB | 60 | Q1 level-shift mreža; bez preuzimanja proturječnog vendor komentara o invertiranju |
| USB Serial/JTAG | 33 D− / 34 D+ | J1 USB-C, nije USB-MIDI host konektor |
| USB host | namjenski USB_DP/DM, J4 Type-A | Nema proizvoljnog GPIO remapa |
| UART0 | 58 TX / 59 RX | USB-UART bridge i J2, ne dodjeljivati drugim funkcijama |
| Ethernet | 3, 5–19 | INT, SMI, reset, RGMII; rezervirati i kada driver nije aktivan |

USB VBUS je preko TPS2051C limitiran na 500 mA; EN je u shemi podignut otpornikom,
bez identificirane MCU kontrole. Ne obećavati software power-cycle klavijature.
Speaker J9 je izlaz pojačala između OUT+ i OUT−, nije line-out niti GND-referencirani
izlaz. Za fizički test koristiti prikladan zvučnik prema vendor specifikaciji.

## Nalazi neslaganja u službenom primjeru

U `esp-board-manager` commitu `391f8bc5bd15beab3f2045e677b6675def25833f` komentari
`board_peripherals.yaml` navode MCLK50/SCL52 kao tentative, ali konfiguracija ispravno
navodi **MCLK52/SCL50**, što se vidi i u shemi. Isti komentar miješa USB host s FS
USB1P1 parom te J1 s J2 headerom. LED komentar traži `invert_out=true`, a vrijednost
je false. Zato se pinovi ovdje temelje na shemi, a ponašanje se potvrđuje mjerenjem.
Ne unositi cijeli board manager/GMF kao dependency samo radi tih primjera.

## Vanjske kontrole

Zaslon i tipka: **TBD**, prva integracija headless/Web. Kandidati za naknadnu raspodjelu
su izloženi GPIO35–49; to nije odobrena shema spajanja. Provjeriti header redoslijed,
strapping, pin capability, postojeće veze i napajanje odabranog modula prije dodjele.
GPIO20–25 vode na SDIO oznake J2, ali ovaj projekt nema SD funkciju.

Prvi fizički zapis mora sadržavati reviziju PCB-a/modula, fotografiju oznake u lokalnim
dokazima, chip revision, flash/PSRAM očitanje, korišteni USB konektor i točan port.
COM4/COM6 iz P4 projekta nisu identitet nove ploče.

