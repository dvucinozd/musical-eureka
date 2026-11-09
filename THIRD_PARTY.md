# Licence i preuzeti sadržaj

- P02 MIDI, synth, looper i pripadajući testovi preuzeti su iz pinned P4 izvora.
  Pojedinačni SHA-256 sačuvani su u `reference/source-files.json`, a P02 izvještaj
  bilježi identitet uvezenih datoteka.
- ES8311 driver dolazi iz
  [Espressif komponente esp_codec_dev 1.6.2](https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.6.2/readme?language=en).
  Verzija i hash paketa su u `dependencies.lock`.

Aktivni bootstrap je novo napisani projektni kod nad ESP-IDF API-jima. Ovaj dokument
ne dodjeljuje novu licencu cijelom korisničkom projektu.

- ESP-IDF: službeni checkout `fff9895c82d744c7237be8847347bdd1b07c6643`; sačuvati
  njegove licence i licence ovisnosti pri distribuciji. Ne kopirati SDK u repozitorij.
- NEKTAR-P4: lokalni pinned export služi auditu i početku porta. Root LICENSE nije
  pronađen u pregledanom vrhu projekta; ne proglašavati cijeli projekt MIT niti
  javno objavljivati pod novom licencom na temelju licence jedne komponente.
- USB MIDI adapter: izvor nosi MIT notice i `components/usb_midi_host/LICENSE-PAJONIIIR`.
  Provenance u izvornom kodu: PAJONIIIR-M3 commit `ccebdb96e302d8be8bf2b1269ee8afdad583e430`.
  Pri prijenosu sačuvati komentare i licencu; ne treba drugi originalni projekt mijenjati.
- TinySoundFont: `853a0a171759f1ddba0de1442133a75912bbeffa`, MIT. Originalni `tsf.h`
  SHA-256 `70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197`.
  Sačuvati original, LICENSE, UPSTREAM.md i patch generator; adaptirati privatni wrapper.
- Toolchain libc/IRAM extract: biblioteke nose vlastite licence; prije prijenosa
  generatora ponovno dokumentirati točne objekte i archive hash u IDF 6.1.
- Espressif shema/datasheet i board YAML: razvojne reference, ne dodavanje licence
  na naše datoteke. Vendor kopije ostaju ignorirane; linkovi/revizije u REFERENCES.
- SF2 banke, PCM/WAV testni izlazi i privatni ključevi nisu uključeni u Git. Svaka
  buduća distribuirana banka treba vlastito podrijetlo/licencu; hardware test ne daje
  pravo redistribucije banke.

