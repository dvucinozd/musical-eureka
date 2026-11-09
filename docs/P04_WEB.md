# P04 — nativni Wi-Fi, Web i MIDI looper

Firmware `0.0.4-web` koristi nativni ESP32-S31 SoftAP `NEKTARINA-S31`.
Razvojna WPA2 lozinka je `nektarina-s31`. Mijenja se kroz
`CONFIG_NEKTAR_AP_PASSWORD` u lokalnom sdkconfigu. Web adresa je
`http://192.168.8.1/`. Radio i PHY inicijaliziraju se prije audio taska.
HTTP server i USB ingress rade na jezgri 0, a synth na jezgri 1.

Web služi lokalni HTML. `GET /api/status` čita kopiju stanja, a
`POST /api/control` validira i stavlja komandu u ograničeni red. Odgovor 202
znači da je komanda u redu. Audio task obrađuje do osam Web i 16 MIDI događaja
po bloku. HTTP prihvaća AP host i provjerava Origin za browser POST.

Virtualna tipkovnica ima 25 tipki, pomak oktave, podršku za akorde i obnavljanje
držanja svakih 500 ms. Na gubitku Web klijenta pritisak istječe nakon 1,5 s.
Oscilator dijeli istu notu između USB i Web izvora dok je barem jedan drži.
USB odspajanje otpušta USB note i resetira njegove kontrolere. Web i looper
mogu nastaviti svirati. Korisnički All sound off je globalni panic.

Sučelje prenosi funkcije tipkovnice, synth preseta, filtra, ADSR-a, FX-a,
arpeggiatora i četiri MIDI trake u kompaktnu stranicu. Svaka traka ima
Record/Play/Overdub, Stop, Clear, Mute, Volume i Preset. Tempo i duljina
loopa su zajednički. Fizičke display stranice i OTA imaju vlastite faze.

## Dokaz

Chrome kroz postojeći Playwright na lokalnom mock API-ju. Browser plugin nije
dostupan u ovoj sesiji. Provjereni su identitet stranice, sadržaj, konzola,
desktop 1280 × 900 i mobilni 390 × 844 prikaz bez horizontalnog prelijevanja.
Interakcija Track 1 Record → virtualna tipka → HOLD i Release → Play → Stop
→ Clear promijenila je prikaz i poslala očekivane naredbe. Provjereni su
odabir synth preseta i pomak oktave. Renderirani test koristi simulirani API.

## Fizički test — NOT RUN

Poslije P01–P03 provjere spojiti se na SoftAP i otvoriti stranicu. Provjeriti
Web pritisak uz USB pritisak iste note u oba redoslijeda otpuštanja. Držati
Web notu, prekinuti vezu i potvrditi istek bez zaustavljanja USB-a ili loopera.
Provjeriti snimanje/overdub na sve četiri trake te audio DMA i deadline brojače
pod Wi-Fi prometom. Izmjeriti latenciju Web i USB tipki odvojeno.
