# Plan provjera i dokazni ugovor

Svaki rezultat označiti kao **HOST**, **BUILD**, **HARDWARE** ili **USER LISTENING**.
Uz rezultat spremiti revision, IDF/compiler, sdkconfig hash, binary/ELF hash, naredbu,
trajanje, hardver/port i sirovi log. NOT RUN nije PASS. Predlošci su u `docs/templates/`.

## Matrica

| Područje | Host/build | Fizička provjera |
| --- | --- | --- |
| Bootstrap | Target/SDK guard, build/link, particije | Oznaka modula, chip revision, 16 MB flash/PSRAM, reset/boot |
| Audio | Format/finite/clamp/tišina/PCM regresije, ELF/heap | Codec ACK, PA, clockovi, mikrofon samo ako je u opsegu, bez popa |
| USB/MIDI | Deskriptori, queue bounds, stale session, ownership | GX49, sustain, pitch, CC, reset/unplug/reconnect, simultani Web |
| Looper | 4 trake, tempo, wrap, full buffer, overdub, mute, release | Zajedničko live + looper sviranje i kontrola |
| Bluetooth izlaz | Build A2DP Source, promjene stanja izlaza i granice PCM reda | Uparivanje, odspajanje, lokalni izlaz nakon promjene, audio latencija i rad uz Wi-Fi/USB MIDI |
| Native Wi-Fi/Web | Retry/lifecycle, Web HOLD expiry, API/UI | AP/STA odabrani način, reconnect, Web latency i radio/audio contention |
| Flash SF2 | Truncated/invalid/oversize SF2, CRC/SHA, fault injection, capacity | Prekid uploada/napajanja, reboot, očuvanje starog slota, readback |
| NVS | Schema, bounds, neispravna/stara verzija, preserve-on-error | Save/restore/reboot u mirnom audio stanju |
| OTA | S31 image validation, size, hash/signature policy, health | Odbij P4 image, interrupted upload, rollback i oporavak |
| No-SD/no-WAV | Nema ciljnog recorder/SD modula/rute/UI/pollinga | Instrument radi bez kartice i bez nepostojeće SD ovisnosti |

Pretraživati aktivni kod, ne `reference/`, `build/` i povijesni audit. Riječi `record`
u MIDI looperu i WAV-format host PCM testnom izlazu nisu same po sebi kršenje opsega.

## S31 performance ugovor za P06

Postupak za dijagnostički build, kratki pilot i odvajanje steady/event blokova
je u [P06 SoundFont](P06_SOUNDFONT.md). Kriteriji služe za engineering pilot.

- 48 kHz / 128 frameova daje period **2666,67 µs**. Mjeri se potpuna priprema bloka,
  DMA underrun/service gap, latencija, min/free/largest heap i stack high-water.
- Cilj headrooma: full preparation <=2200 µs u registriranim steady/event segmentima;
  1800 µs je optimizacijski cilj, ne automatsko prihvaćanje. Stvarni deadline je period.
- Event16 i steady32 odvojiti; razlikovati MIDI note, layer/sample glasove i release tails.
  Koristiti stvarnu banku/preset uz SHA, velocity, note pattern i konfiguraciju efekata.
- Najprije kratki pilot; zatim najmanje 30 min live/looper/Web/radio soaka na zamrznutom
  normalnom imageu. Cilj: 0 underruna, 0 zapečenih nota, 0 rastućih leakova, 0 stvarnih
  preparation deadlinea. Rijetki tail nije razlog da ga sakrijemo u prosjeku.
- USB segment s fizičkom klavijaturom i unplug/reconnect, ne samo sintetski/Web input.
  USB i Web latenciju izmjeriti odvojeno; P4 rezultat 2,7 ms nije S31 rezultat.
- Flash upload, NVS save i OTA su zasebne maintenance provjere s utišanim/zaustavljenim
  audio putem. Odbijanje upisa dok instrument svira testira se kao očekivano ponašanje.
- Slušna potvrda korisnika za glasnoću, klikove, looper, SF timbre i promjene izvora.

To su početni engineering kriteriji, nisu već korisnički prihvaćen release ugovor.
Ako 32 glasa ne prođu, dokumentirati banku i usko grlo prije promjene cilja/polifonije.
Promjena kriterija mora ostaviti prethodni FAIL vidljivim.

## Što ne izvoditi kao S31 test

Nema flashanja P4 imagea, SD fill/format testa, WAV recorder kampanje niti otvaranja
starih COM portova po pretpostavci. Nema potrebe za dodatnim P4 fizičkim testiranjem
da bi se ovaj port pripremio. Pun corpus/golden/sanitizer test dolazi pri prijenosu
enginea; početnih 30 baseline skupova nije zamjena za njega.

