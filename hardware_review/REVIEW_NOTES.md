# Review notes / minor schematic hygiene

These notes are here to reduce reviewer confusion. They do **not** replace the schematic.

## Buck rail labeling
The schematic block heading mentions a “+3V3 buck converter section”, while the module is labelled “MP1584 5V Fixed Module”
and there is also a note “(PLACEHOLDER: USE +3V3 VERSION)”.

**Interpretation (needs confirmation in source project):**
- The external buck module likely generates **+5V**, which feeds the ESP32‑C3 dev board.
- The **+3V3** rail is then provided by the ESP32‑C3 dev board’s onboard regulator and exported to peripherals.

If this is correct, the schematic section title should be renamed to “+5V buck converter section” and placeholder text removed.

## R5 / R6 annotation
R5 and R6 are shown as **2R2** on the PCB silkscreen/layout, but the schematic text includes “OPTIONAL … 0”.
If populated, they should be treated as 2R2 series resistors (signal conditioning / EMI / edge damping).

## Intent of this folder
Gerbers / full CAD files are intentionally not published; this folder is for review only.
