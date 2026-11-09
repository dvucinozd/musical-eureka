# NEKTARINA S31 — radne upute

- Razgovaraj hrvatski. Ovo je zaseban C/ESP-IDF port iz `D:\AI\NEKTAR-P4`, ne Rust rewrite niti nastavak drugog NEKTARINA projekta.
- Prvo pročitaj README, docs/STATE_SUMMARY.md, docs/NEXT_SESSION.md i docs/DECISIONS.md.
- Korisnik je 2026-11-09 isključio microSD i WAV recorder. Zadrži četiri MIDI looper trake. Ne vraćaj SD/recording UI ni WAV testne kampanje u ciljnu aplikaciju.
- P4 izvor i instalaciju IDF-a pregledavaj bez izmjena. Autoritet za baseline je config/source-baseline.json; referentni export nije aktivna S31 aplikacija.
- Koristi pinove iz docs/HARDWARE.md, s oznakom dokumentarno/fizički provjereno. Ne prepisuj P4 pinove, COM port, flash image, sdkconfig ili dependencies.lock.
- SDK je ESP-IDF 6.1.0 na commitu iz manifesta. S31 je preview; aktiviraj `C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1` i koristi tools/build.ps1.
- Audio put: bez alokacije, logiranja, flash/filesystem/mrežnog I/O-a i neograničenog čekanja. Učitavanje i zamjena banke ostaju worker/ACK operacije.
- Pisanje flasha i NVS-a tijekom sviranja nije početno podržano. Potreban je koordiniran mute/stop i ekskluzivna kontrolna operacija; sam zaseban task nije dokaz RT sigurnosti.
- Za sada je odrađena priprema bez uređaja. Prije fizičkog rada identificiraj točnu ploču, port, reviziju i način oporavka u kontekstu tadašnjeg korisničkog zahtjeva. Ne prenosi staro P4 dopuštenje za flash na novi uređaj. Ne diraj eFuseove niti radi erase po automatizmu.
- Build PASS nije boot/audio/performance PASS. P4 prihvaćanje izdanja 1.0.0 ne kvalificira S31.
- Svaka faza završava ciljanim provjerama i ažuriranjem STATE_SUMMARY/NEXT_SESSION. Ne izmišljaj trajanje, rezultat ili korisničku slušnu potvrdu.
- Vendor dokumenti, lokalni SF2/WAV materijal, privatni ključevi i build izlazi ostaju izvan Gita. Čuvaj licence i hashove preuzetog koda.

