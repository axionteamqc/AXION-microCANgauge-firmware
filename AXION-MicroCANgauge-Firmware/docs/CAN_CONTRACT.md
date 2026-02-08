# CAN Gauge Integration Contract (MS3)

- Boot normal direct si le bitrate est verrouillé (pas de scan auto, pas de listen-only implicite).
- TWAI reste actif après `can_ready=true` : pas de stop/uninstall une fois prêt.
- TX applicatif désactivé par défaut (ACK-only). Toute émission doit être explicitement activée et justifiée.
- Interprétation overlay : `NO FRAMES` (silence) ≠ `BAD BUS/BAD BIT` (erreurs/bitrate), `BUS OFF` prioritaire si récent.
- TXD doit être tiré au haut (4.7k–10k recommandé). Firmware force `INPUT_PULLUP` dès le boot pour garder TX recessif.
