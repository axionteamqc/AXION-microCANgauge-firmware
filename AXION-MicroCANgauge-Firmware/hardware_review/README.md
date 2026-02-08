# Hardware review package (no Gerbers)

This folder exists to enable external review without publishing manufacturing files.

Included:
- PCB TOP photo (PNG)
- PCB BOTTOM photo (PNG)
- Schematic export (SVG) — `schematic.svg`
- Schematic export (PDF fallback) — `schematic.pdf`
- Legacy schematic export (SVG) — `schematic_legacy_2026-02-03.svg` (kept for traceability)

Not included:
- Gerbers / pick-and-place / full EasyEDA project files

Review focus (suggested):
- Power entry + protection topology
- CAN physical layer protection (PTC, TVS, choke/filters if present)
- Termination strategy (presence/placement)
- Connector pin labeling consistency
- Decoupling placement relative to ICs


## Reference renders
See `../media/README.md` for enclosure / product renders used in public project presentation.
