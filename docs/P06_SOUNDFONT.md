# P06 SoundFont na S31

P06 priprema ponovljivo mjerenje 16 live sample glasova i 32 glasa u miksu
16 live + 4 × 4 looper. Host replay i S31 build pokrivaju softversku pripremu.
Mjerenje vremena, kapaciteta i zvuka na ploči ostaje **HARDWARE NOT RUN**.

## Buildovi i host replay

Iz korijena projekta u PowerShellu:

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
.\tools\build.ps1 -BuildDirectory build-p06-normal -SdkConfig build-p06-normal/sdkconfig
.\tools\build.ps1 -BuildDirectory build-p06-telemetry -SdkConfig build-p06-telemetry/sdkconfig -SdkConfigDefaults @('sdkconfig.defaults','config/sdkconfig.p06.defaults')
.\tests\run_p06_tests.ps1
```

Zasebni svježi sdkconfig učitava overlay samo u dijagnostički build.
Kconfig opcija `NEKTAR_P06_TELEMETRY` zadano je isključena. P05b image i
njegov identitet ostaju u [P05b evidenciji](evidence/2026-11-09-p05b.json).
[IDF 6.1 pravila za sdkconfig defaults](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s31/api-guides/build-system.html#custom-sdkconfig-defaults).

Host runner generira tri CC0 SF2 fixturea u temp direktoriju. Pokreće istu
S31 synth i looper rutu s tri varijante renderera. Prva kompajlira SF2 engine
iz zamrznutog P4 izvora, druga aktivni S31 engine, treća aktivni S31 engine s
telemetrijom. Po fixtureu uspoređuje SHA-256 nad 17 536 frameova float PCM-a.
Svaki replay potvrđuje osam event16 blokova i 128 uzastopnih steady32 blokova.
Host fixture s dva sloja puni 16 sample glasova s osam MIDI nota.
[Rezultati i hashovi](evidence/2026-11-09-p06.json).

## UART mjerenje

Nakon [identifikacije i bring-upa](NEXT_SESSION.md), na dijagnostičkom imageu
pritisni `p` u UART0 konzoli dok je audio u stanju LIVE. Pričekaj završetak
4096 zapisanih blokova i pritisni `P`. Pri 48 kHz i 128 frameova nominalni
prozor traje oko 10,92 s. Idući prozor može se pokrenuti nakon čitanja
prethodnog. Uz UART log zabilježi build hash, fizičku reviziju, uvjete USB-a i
Wi-Fija te primijenjene kontrole. Log učitane banke povezuje runtime generaciju
i SHA-256 s brojem banke i programa u zapisu mjernog prozora.

| Mjera | Što obuhvaća |
| --- | --- |
| `all` | Cijela priprema bloka od početka audio iteracije preko MIDI/Web obrade, looper i synth/SF2 miksa do pretvorbe u PCM, prije čekanja na I2S. |
| `event16` | Valjan blok od 128 frameova s prihvaćenim live SF2 Note On događajem i 16 live glasova na granicama svakog segmenta rendera. SF2 trake u tom bloku nemaju render ni događaje. |
| `steady32` | Valjan blok od 128 frameova u svih pet partova, s rasporedom 16/4/4/4/4 na granicama svakog segmenta i bez SF2 događaja. |
| `write_path_max_us`, `start_gap_max_us`, `long_gaps` | Zasebni maksimumi putanje I2S upisa i razmaka između početaka audio blokova. Dugi razmak je preko 5334 µs. |
| `dma_queue_overflows` | Delta IDF TX callbacka za prelijevanje reda. Za ocjenu stvarnog underruna trebaju i I2S te zvučna mjerenja. |
| `usb_dispatches`, `web_blocks` | Broj USB događaja proslijeđenih iz ingressa i broj blokova s Web naredbama. Latencija USB-a u postojećem statusu je maksimum od boota do dispatcha. |
| `stack_min_free_bytes` i heap | Najmanji slobodan audio stack od pokretanja taska, očitan nakon zadnjeg bloka. UART pri čitanju rezultata uzorkuje free/min/largest za interni, DMA i PSRAM heap. |

`result=0` označava dovršen prozor, `1` prekid zbog maintenancea, `2` audio
fault i `3` promjenu aktivnog zvuka. `invalid_trace` broji blokove s odbijenim
SF2 operacijama ili nepotpunim tragom rendera. Kontrolni task ispisuje rezultate,
a audio task dok mjeri drži samo fiksne brojače u memoriji. Prozor mjeri izvedbu
instrumentiranog builda. Host PCM usporedba pokriva sadržaj signala, dok
trošak same instrumentacije čeka mjerenje na ploči.

## Kratki pilot na ploči

1. Prođi P01–P05 fizički bring-up. Koristi poznati dijagnostički image i učitaj
   sintetički `two_layers.sf2`, program bank 0/program 0. Zabilježi SHA iz
   [host dokaza](evidence/2026-11-09-p06.json) i potvrdi da se podudara s UART-om.
   Postavi arp, delay i chorus na Off te jedan takt na 120 BPM. Zabilježi
   glasnoću. Promjene postavki i pohrane završi prije mjernog prozora.
2. **Event16.** Zaustavi sve trake i oslobodi SF2 trake od banke. U prozoru
   ponavljaj akord MIDI nota 60–67. Između akorda otpusti sve tipke i pričekaj
   kraj releasea. Bilježi najmanje osam kvalificiranih `event16` blokova kroz
   dovršene prozore. Osam je broj opaženih blokova u pilotu.
3. **Steady32.** Snimi odvojene parove 48–49, 50–51, 52–53 i 54–55 na
   četiri prazne trake. Svako snimanje započni dok ostale trake stoje. Drži
   par otprilike 750 ms pri početku takta i otpusti prije zaustavljanja trake.
   Pokreni sve trake, pričekaj prvi puni zajednički takt i drži note 60–67.
   U jednom dovršenom prozoru ciljaj najmanje 128 kvalificiranih
   `steady32` blokova. To je oko 341 ms rada s punom polifonijom.
4. Spremi potpuni log uključujući blokove izvan kvalificiranih skupina.
   Provjeri `invalid_trace=0`, `deadlines=0`, `long_gaps=0` i
   `dma_queue_overflows=0`. Predloženi cilj za svaku kvalificiranu skupinu je
   `max_prepare_us <= 2200`. Deadline se broji od 2667 µs. Ako uzorak nema
   dovoljno kvalificiranih blokova, zabilježi manjak pokrića i ponovi scenarij.
5. Nakon pilota ponovi uz aktivan Wi-Fi/Web i fizički USB MIDI. Zasebno
   izmjeri note-to-audio i provedi slušnu provjeru te najmanje 30 min
   live/looper/Web/radio soaka na normalnom imageu. Spremi UART status
   `?` na početku i kraju da dobiješ delte kumulativnih brojača.
   [Ugovor provjera](VALIDATION.md#s31-performance-ugovor-za-p06).
