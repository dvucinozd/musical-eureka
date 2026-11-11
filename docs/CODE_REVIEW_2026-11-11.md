# Code review prije P02 — 2026-11-11

R1 i R2 implementirani su nakon pregleda. HAL sada provjerava clock, SDP i
DAC registre prije pokretanja audio taska. OTA potvrda traži uspješan start
USB MIDI hosta i HTTP servera s registriranim rutama. Web naredbe i USB MIDI
ulaz otvaraju se nakon potvrde. Host provjere simuliraju pogrešan clock
registar i odvojene greške starta USB-a i Weba.
[Aktualni buildovi i fizički koraci](NEXT_SESSION.md).

Pregledan je aktualni C/ESP-IDF port na commitu
`dec4a0b8b5c709459a7a7c9ddc360e79fc7f7a74` i lokalna dokumentacija P01 bring-upa.
Ručni pregled obuhvatio je audio HAL i pinove, USB ingress, synth/looper,
SoundFont lifecycle, A/B flash i preset, Bluetooth rutu/PCM te Web OTA.
SDK integracija uspoređena je s lokalnim pinned ESP-IDF izvorima i
`esp_codec_dev` 1.6.2. COM7 tijekom pregleda nije otvaran.

CodeRabbit CLI 0.9.2 završio je pregled promjena od `0b80cc3` kroz P04–P07
s **0 issues**. Raniji P01–P03 kod dodatno je pregledan ručno, izvan tog diffa.
[Zabilježeni rezultat CodeRabbita](evidence/2026-11-11-coderabbit-review.json).
Zapis prenosi završni događaj iz izlaza alata. Izvorni privremeni WSL log
pri naknadnom arhiviranju više nije bio dostupan.

## R1 — P2, nedovoljna provjera konfiguracije ES8311

Mjesto je `components/audio_hal/audio_hal.c`, redci 411–413.
HAL provjerava rezultat `codec->set_fs()` i čita SDP registar `0x09`.
U korištenoj ovisnosti `es8311_set_fs()` zanemaruje rezultate
`es8311_set_bits_per_sample()`, `es8311_config_fmt()` i
`es8311_config_sample()` te vraća `ESP_CODEC_DEV_OK`.

Ako I2C upis clock dividera ne uspije, a naredni upisi i čitanje SDP-a uspiju,
HAL može nastaviti i dopustiti PA uz neispravnu konfiguraciju clockova.
SDP čitanje potvrđuje format, ali ne provjerava clock dividere.
To je konkretna rupa u detekciji greške, bez dokaza da se dogodila na ploči.

Prije audio kvalifikacije dodati HAL provjeru relevantnih clock i DAC registara
za 48 kHz / MCLK 256 i zadržati mute pri nepodudaranju. Alternativno koristiti
provjeren adapter koji propagira sve greške konfiguracije. Provjeru pokriti
ubrizganom greškom jednog clock upisa uz uspješan SDP readback.
P02 arhivirani BIN/ELF/bootloader/particije odgovaraju povijesnim hashovima.
Ako se HAL popravi, novi image treba zaseban build i identitet.

## R2 — P2, OTA potvrda prethodi pokretanju Web i USB servisa

Mjesto je `main/app_main.c`, redci 154–164, i `main/app_ota.c`, redak 172.
`app_ota_check_boot()` može zapisati VALID prije `midi_ingress_start()` i
`app_web_start()`. Ako nakon toga ne uspije kreiranje HTTP taska, registracija
ruta ili pokretanje USB hosta, aplikacija samo ispiše grešku i nastavi UART petlju.
Kandidat je već potvrđen i sljedeći reset više ne aktivira rollback zbog
neuspješnog prvog boota. Kvar Web servisa ujedno uklanja mogućnost Web OTA oporavka.

Prije OTA pilota uključiti uspješnu inicijalizaciju potrebnih servisa u boot
health uvjet. Ulazne naredbe zadržati zatvorenima tijekom provjere i zapisati
VALID pod postojećim audio maintenance protokolom. Provjeriti neuspjeh HTTP
starta i USB inicijalizacije te potvrditi da kandidat ostaje nepotvrđen.
P07 dokumentacija opisuje postojeći redoslijed, pa se s popravkom ažurira i ona.

## Ponovljene provjere

Svih pet postojećih runnera završilo je s exit code 0.

- `tests/run_tests.ps1`, 14 osnovnih host skupova.
- `tests/run_p05a_tests.ps1`, uključujući P05b preset i storage testove.
- `tests/run_p06_tests.ps1`, PCM replay triju fixture banki kroz tri konfiguracije.
- `tests/run_p06b_tests.ps1`, PCM ring i prijelazi izlaza.
- `tests/run_p07_tests.ps1 -Image build-p07-combined/nektarina_s31.bin`,
  provjera stvarnog zajedničkog imagea i grešaka OTA transakcije.

Prošli su i `tools/verify_project.py`, `tools/verify_bringup_kit.py` te
`git diff --check`. P02 aplikacija, ELF, bootloader i particije podudaraju se
s manifestom iz 2026-11-09. Današnji root `sdkconfig` i `dependencies.lock`
pripadaju kasnijim fazama i razlikuju se od P02 povijesnih hashova.

Prethodni host testovi obuhvaćali su portable logiku. Naknadna implementacija
koristi `tests/run_review_tests.ps1` za provjeru readbacka i OTA boot odluke
uz simulirane pozive drivera. P01 fizički dokaz obuhvaća dva bootstrap boota
i startup PSRAM test.
