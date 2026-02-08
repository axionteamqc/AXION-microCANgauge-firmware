# Build Instructions (ESP32-C3 / PlatformIO)

- Tooling: PlatformIO CLI with the `esp32c3` environment defined in `platformio.ini`.
- Clean build:
  ```sh
  pio run -e esp32c3
  ```
- No additional setup required beyond a standard PlatformIO install. The firmware is Arduino-ESP32 based; networking is not required to build.
- Hardware note: keep CAN TX recessive with an external pull-up (≈4.7k–10k) on TXD; firmware also drives TXD to `INPUT_PULLUP` immediately at boot.
- See also `docs/CAN_CONTRACT.md` for CAN integration rules (boot flow, TX off by default, no auto-stop).
