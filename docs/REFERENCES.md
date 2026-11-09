# Primarni izvori i podrijetlo

Pregledano **2026-11-09**. Linkovi `latest` mogu se promijeniti; sačuvani commitovi i
hashovi razlikuju pregledanu verziju od buduće. Vendor PDF-ovi se ne prenose u Git.

| ID | Izvor | Korištenje / stanje |
| --- | --- | --- |
| HW1 | [Espressif CoreBoard user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-function-coreboard-1/user_guide.html) | Ugrađeni uređaji, 16 MB flash/16 MB PSRAM, konektori i header |
| HW2 | [CoreBoard schematic](https://dl.espressif.com/schematics/esp32-s31-function-coreboard-1-schematics.pdf) | V1.0, 2026-05-13, 3 lista; listovi 2 i 3 vizualno pregledani |
| HW3 | [S31 SoC datasheet](https://documentation.espressif.com/esp32-s31_datasheet_en.pdf) | Pre-release v0.5, PRELIMINARY; mogućnosti čipa, ne stanje našeg primjerka |
| HW4 | [WROOM-3/3U module datasheet](https://documentation.espressif.com/esp32-s31-wroom-3_datasheet_en.pdf) | Pre-release v0.7, PRELIMINARY; CPU/memorija i varijante modula |
| SDK1 | [ESP-IDF v6.1 release](https://github.com/espressif/esp-idf/releases/tag/v6.1) | Tag odgovara lokalnom `fff9895c82d744c7237be8847347bdd1b07c6643` |
| SDK2 | [ESP-IDF v6.1 S31 Get Started](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s31/get-started/index.html) | S31 razvojno okruženje |
| SDK3 | [S31 flash concurrency](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s31/api-reference/peripherals/spi_flash/spi_flash_concurrency.html) | Cache/PSRAM/flash write ograničenja, maintenance plan |
| BSP1 | [esp_boards 0.6.1 registry](https://components.espressif.com/components/espressif/esp_boards/versions/0.6.1/readme?language=en) | Tablica podrške: audio, bez onboard SD/LCD za CoreBoard |
| BSP2 | [Pinned board peripherals](https://github.com/espressif/esp-board-manager/blob/391f8bc5bd15beab3f2045e677b6675def25833f/esp_boards/esp32_s31_function_coreboard_1/board_peripherals.yaml) | Usporedba sa shemom; uočeni netočni tentative komentari |
| BSP3 | [Pinned board devices](https://github.com/espressif/esp-board-manager/blob/391f8bc5bd15beab3f2045e677b6675def25833f/esp_boards/esp32_s31_function_coreboard_1/board_devices.yaml) | Codec address i PA; proturječan LED komentar |
| BSP4 | [Pinned board defaults](https://github.com/espressif/esp-board-manager/blob/391f8bc5bd15beab3f2045e677b6675def25833f/esp_boards/esp32_s31_function_coreboard_1/sdkconfig.defaults.board) | CPU320, QIO80/16MB i OCT200 bootstrap postavke |

Lokalna shema: `reference/vendor/esp32-s31-function-coreboard-1-schematics.pdf`.
SHA-256: `fb2a6a742013357388a8f55f6fe8d04c05e9dc7c716796acde0eb8b18cb89e0b`.
PNG pregledi su privremeni, `tmp/pdfs/`; lokalni vendor dokument je referenca bez izmjena.

Lokalni SDK izvori dodatno provjereni:

- `tools/idf_py_actions/constants.py`: S31 u PREVIEW_TARGETS.
- `components/esp_system/port/soc/esp32s31/Kconfig.cpu`: CPU default320.
- `components/esp_psram/esp32s31/Kconfig.spiram`: OCT i default200.
- `components/esp_hal_usb/esp32s31/include/hal/usb_dwc_ll.h`: jedan HS DWC.

P4 izvor: lokalni Git `D:\AI\NEKTAR-P4`, commit i odabrani putevi u
[source-baseline.json](../config/source-baseline.json). Release bilješke nalaze se i
u lokalnom ignored exportu `reference/nektar-p4/docs/RELEASE_1.0.0.md`.
Nije korišten drugi NEKTARINA/Rust projekt kao autoritet za ovaj port.

