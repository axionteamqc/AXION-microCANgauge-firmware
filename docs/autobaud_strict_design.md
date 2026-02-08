# Autobaud Strict Mode (Gauge – ESP32-C3 TWAI)

## Goals
- Never transmit application frames (ACK-only bus participation).
- Avoid false-positive locks by requiring a 2-phase listen-only confirmation.
- Roll back immediately if NORMAL guard window shows bus health issues.
- Produce deterministic logs for every outcome.

## Algorithm (per profile)
1. **Phase A — Listen-only scan per bitrate**
   - Window: default 300 ms (profile tunable).
   - Collect `rx_total`, `rx_dash`, per-ID counts, errors (bus_off/err_passive/overrun/missed).
   - Compute a score (dash hits + distinct IDs, penalise errors/overrun/missed).
   - Track best/second-best by score.

2. **Pre-filter**
   - If no activity at all: `NO_CAN_ACTIVITY`.
   - If best does not beat second by a safe delta: `NO_CLEAR_WINNER`.

3. **Phase B — Confirmation (listen-only)**
   - Re-run listen-only on best (and second if needed) for 2 windows (default, profile-tunable).
   - Require min frames/distinct IDs/expected hits and zero errors; otherwise `CONFIRMATION_FAILED`.

4. **NORMAL guard (ACK-only)**
   - Start TWAI NORMAL with `tx_queue_len=0` (no TX).
   - Guard window ~250 ms: if bus_off/err_passive/overrun/missed exceed limits, stop, rollback to scan.
   - Only on a clean guard + successful validateNormal() the bitrate is locked.

## Thresholds (MS3 profile defaults)
- Listen window: 300 ms
- Confirm: 2 windows × 300 ms
- Min frames: 12, min distinct IDs: 2, min expected hits: 8
- Max rx_missed: 2, errors must be zero
- Score delta needed to avoid `NO_CLEAR_WINNER`

## Safety
- TWAI configured with `tx_queue_len=0` in all modes ⇒ no app frames emitted.
- Rollback on any guard failure; scan continues.

## Logging
- Per bitrate: `rate, rx_total, rx_dash, distinct, errs, missed, overrun, score`
- Final failure reasons: `NO_CAN_ACTIVITY`, `NO_CLEAR_WINNER`, `CONFIRMATION_FAILED`, `NORMAL_VERIFY_FAILED_ROLLBACK`.

## Notes
- Profiles can tighten/loosen windows and minimums via `AutobaudSpec`.
- Generic mode placeholder exists for future multi-ECU detection; MS3 remains the default profile.
