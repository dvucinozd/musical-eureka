# Lokalne reference

- `nektar-p4/`: ignorirani, byte-exact Git export s 450 datoteka, ne aktivni firmware.
  `python tools/export_source.py` rekonstruira ga iz pinned lokalnog repozitorija.
- `source-files.json`: tracked hash manifest tog exporta.
- `vendor/`: ignorirane službene reference. Izvori i hash sheme u
  [REFERENCES](../docs/REFERENCES.md).

P4 kopija namjerno još sadrži recorder/SD i stare dokumente radi usporedbe. To nije
suprotno ciljnom opsegu: pri aktivnom portu ti se moduli ne prenose. Ne izvršavati
njezine hardware/flash skripte kao pripremu S31 projekta.

