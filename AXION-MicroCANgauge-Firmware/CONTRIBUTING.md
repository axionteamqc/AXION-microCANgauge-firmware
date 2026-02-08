# Contributing

## Ground rules
- Keep changes small and reviewable.
- Avoid “drive‑by refactors” that mix formatting + logic.
- When touching runtime‑critical paths (CAN receive, UI render, portal handlers), include a short rationale in the PR.

## Build checks
- `pio run -e esp32c3_release`
- (Optional) run unit tests: `pio test -e esp32c3_release` (if configured)

## Issue reports
Include:
- ECU type + firmware version
- Whether SDB is **enabled** and **Automatic**
- Logs (if enabled), and exact reproduction steps
