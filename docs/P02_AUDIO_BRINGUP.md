# P02 — synth jezgra i ES8311 izlaz

2026-11-11 potvrđeno je osnovno sviranje na ESP32-S31-Function-CoreBoard-1
V1.0, UART COM7. Korisnik je potvrdio čistu synth notu C6 i izravni 1 kHz
ton u lijevom I2S slotu. C4 sinusna proba nije bila čujna na priključenom
zvučniku. Oznake zvučnika i uzrok razlike u odzivu još nisu poznati.

Aktivni firmware je razvojni profil `build-p02-diagnostic`, verzija
`0.0.5-soundfont-dev`, s aplikacijom na `0x20000`. SHA-256 je
`d7ed40a79bc9588322082472e6e64de99db57fef3c5d9047f5bb0e26f39da274`.
Koristi prethodno instalirani bootloader iz `build-review-local` i isti
aktualni raspored particija.
[Strojni zapis, hashovi i logovi](evidence/2026-11-11-p02-hardware.json).

## Rezultati na ploči

- ES8311 ACK na 0x18 i provjera clock, SDP i DAC registara prošli su.
- Izravni 1 kHz ton u oba slota i zasebno u lijevom slotu bio je čujan.
- C6 synth nota, gain 25/50, držanje jednu sekundu, bila je čista prema korisniku.
  PCM16 peak je 6083. Očitano je 1099 pozitivnih prolaza kroz nulu tijekom
  držanja i otpuštanja, jednako host referentnoj probi.
- Nakon C6 probe audio se vratio u `state=2`, `fault=ESP_OK`, uz nula
  pripremnih deadlinea i DMA overflowa. Pri probi je GPIO57 pad očitan HIGH.
- C4 proba na gainu 25/50 imala je peak 5676, ali korisnik je nije čuo.
- LRCK/BCLK/MCLK frekvencije i PA napon još nisu izmjereni instrumentom.
  Višeglasje, USB MIDI, stvarna latencija i soak čekaju zasebne pilote.

## UART dijagnostika aktualnog profila

`?` čita stanje, peak prethodne probe, prihvaćanje note, PA naredbu,
digitalno očitanje PA pina i pozitivne prolaze kroz nulu.
`d` u kontrolnom tasku ispisuje codec registre i GPIO konfiguraciju dok
je audio utišan.

`t` daje C4, a `h` C6 kroz synth/MIDI parser i lijevi I2S slot.
Obje probe imaju jednu sekundu držanja pri gainu 25/50 i vrate prethodni
gain. `s/l/r` daju izravni 1 kHz PCM u oba/lijevom/desnom slotu,
amplitude 8192 (oko −12 dBFS), uz fade na prvom i zadnjem bloku.
Sve probe imaju uvodnu i završnu tišinu te završavaju s ugašenim PA.
Ponavljanje tijekom aktivne probe se odbija.

Audio put radi sa 48 kHz i 128 frameova po bloku, dva 16-bitna Philips slota.
Normalni mono render šalje PCM u lijevi slot, a nule u desni.
MCLK je 256 × Fs. Audio task radi na jezgri 1.
Uzorci dijagnostičkih brojača prikupljaju se bez alokacije ili logiranja
iz audio taska. Timeout/nepotpuni I2S upis ili nekonačni PCM zaključava fault i mute.

Povijesni P02 image `0.0.2-audio` imao je C4 probu od 200 ms pri gainu 2/50.
Njegovi hashovi i tadašnji build dokazi ostaju u evidenciji iz 2026-11-09.

## Ponovljiva fizička provjera

Najprije evidentirati ploču i potvrditi P01 bootstrap boot prema
[predlošku za ploču](templates/BOARD_BRINGUP.md). Za fizičku P02 probu koristiti
aktualni lokalni build s ES8311 readbackom prema [nastavku rada](NEXT_SESSION.md).
Ostaviti USB klavijaturu odspojenu i Web kontrole neaktivne dok traje proba.
Koristiti USB–UART bridge za UART0 naredbe (115200).

1. Očitati reviziju i memoriju. U boot logu tražiti `S31_AUDIO` ES8311 ACK i
   `ES8311 playback registers verified`. Greška čitanja ili razlika u
   clock, SDP i DAC registrima zaustavlja audio startup s ugašenim PA.
   Zabilježiti stvarne clockove ako je dostupan instrument. Očekivani su
   LRCK 48 kHz, BCLK 1,536 MHz i MCLK 12,288 MHz.
2. Poslati `?`. Očekuje se `state=2`, rast broja blokova i `fault=ESP_OK`.
   Potvrditi tišinu i PA LOW prije prve probe. Druga stanja su 0 off, 1 priming,
   3 probe i 4 fault.
3. Poslati `h` jednom pa `?` nakon otprilike dvije sekunde. Očekuje se jedna
   C6 nota u trajanju jedne sekunde, povrat u `state=2` i `probes` veći za jedan. Zabilježiti klik,
   distorziju ili neočekivanu glasnoću i korisnikovu slušnu potvrdu.
4. Spremiti log, broj DMA overflowa i pripremnih deadlineova. Svaki porast traži
   analizu prije prolaza. Ciljani playback provjerava osnovni put, dok se
   kvalifikacija više glasova i opterećenja radi kasnije.

ES8311 je u DAC-only načinu, pa je mikrofon izvan P02 probe. UART naredbe služe
bringupu. USB MIDI i javne kontrole dobivaju ulaz u odgovarajućim fazama.
