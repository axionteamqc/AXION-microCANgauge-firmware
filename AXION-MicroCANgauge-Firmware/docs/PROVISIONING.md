# Provisioning Cheat Sheet

Edit `src/config/factory_config.h` only. All values are ASCII; keep strings short (12–16 chars recommended for 128×32/64).

- `EcuTarget kEcuTarget`: ECU profile to build for. Current: `kMs3EvoPlus`. (Future: Haltech/Maxx/Link, etc.)
- `kBootBrandText`: Brand text shown on the loading screen.
- `kBootHelloLine1` / `kBootHelloLine2`: Hello text shown after loading (two-step splash).
- `kShipWithSimulationEnabled`: `true` → mock data (no CAN), `false` → real CAN.
- `kEnableEcuDetectDebug`: Reserved; keep `false` unless you know you need it.
- `kEnableVerboseSerialLogs`: `true` to increase serial verbosity; default `false`.
- `kFactoryLockDisplayTopology`: Reserved; keep `false` (prevents user display-mode changes when wired).

After changes: rebuild and flash with `pio run -e esp32c3` (or `Upload` in PlatformIO).

Do not edit other files for provisioning.***
