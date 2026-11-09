# Projekt i opseg

## Cilj

Prenijeti postojeći instrument na ESP32-S31-Function-CoreBoard-1 uz očuvanje glazbenog
ponašanja tamo gdje ga novi hardver podržava. Zadržati postojeći C DSP i MIDI kod te
ESP-IDF; promjena procesora nije razlog za novi jezik ili novi synth engine.

Izvor je prihvaćeno P4 izdanje 1.0.0, puni commit u [manifestu](../config/source-baseline.json).
Njegovi fizički nalazi pripadaju P4. Ciljna verzija počinje zasebno: `0.0.1-bootstrap`.

## Funkcionalni ugovor

| Funkcija | Odluka za S31 |
| --- | --- |
| USB MIDI klavijatura / Nektar GX49 | Zadržati class-based host, parser, session i cleanup ponašanje |
| Synth, 32 tvornička preseta, ADSR/filter/LFO/chorus/delay/arp | Zadržati DSP ponašanje i PCM regresije |
| SoundFont SF2 | Zadržati; novi flash bank store i limit za 16 MB PSRAM-a |
| Polifonija | Cilj 16 live + 4×4 looper sample glasa; dokaz tek mjerenjem S31 |
| Četiri MIDI looper trake | Zadržati record/play/overdub događaja; to nije WAV recorder |
| Audio | 48 kHz, 128 frameova, 16-bit PCM. Zadani izlaz je zvučnik na ES8311/PA. |
| Bluetooth zvučnik | Korisnik može odabrati upareni Bluetooth zvučnik kao izlaz. Klasični A2DP Source je planirana putanja. |
| Wi-Fi/Web/virtualne tipke | Zadržati operatorove funkcije; prijeći na nativni S31 Wi-Fi |
| Postavke/preseti/zadnja banka | Interni flash/NVS, verzioniran format i provjeren boot restore |
| OTA | Zadržati kao kasniju fazu, S31 identitet imagea i rollback health |
| microSD/SDMMC i WAV snimanje | **Ukloniti po korisničkoj odluci 2026-11-09** |
| ST7789 i zasebno tipkalo | Otvoreno; ne blokira početni headless/Web port |
| Mikrofon | Ploča ga ima; nije uvjet prvog porta niti zamjena za recorder |
| Bluetooth MIDI, Ethernet, Thread/Zigbee | Izvan opsega porta |

Nema obećanja da će proizvoljna SF2 banka stati ili postići 32 glasa. Slojeviti preset
troši više sample glasova po MIDI noti. Sačuvati postojeće odbijanje novih live nota iznad
limita i vlasništvo nota između Weba, USB-a i looper traka.

## Završetak porta

Reproducibilan S31 build, identificirana fizička ploča, potvrđen audio/USB/Web,
četiri looper trake, flash upload i restore banke, regresije bez zapele note te
završena kampanja s točno navedenom bankom, presetom, brojem glasova i latencijom.
Objavljeni UI i firmware nemaju SD/WAV funkcije. OTA i dodatni zaslon imaju zasebne
gateove. Bluetooth zvučnik ima zaseban test uparivanja, reprodukcije, prekida veze
i izmjerenog kašnjenja.

