# ECU Profiles — Dev Notes

Goal: add ECU profiles without impacting UI, alerts, or datastore.

What exists now
- Interface `IEcuProfile` (src/ecu/ecu_profile.h)
- Megasquirt profile (src/ecu/profiles/ms3_evoplus_profile.*)
- EcuManager (src/ecu/ecu_manager.*) selects the active profile (Auto or MS3) and exposes `profile()`.
- can_autobaud and processCan already use `IEcuProfile` (acceptFrame, decode, dashIndexForId, scanBitrates).
- Current release: MS3-only (forced profile). Other ECUs would need a separate build/provisioning.
- `ECU_DETECT_DEBUG` (if defined) keeps a passive detect helper for debug only (not used in release).

Rules to respect
- Do not modify UI, pages, alerts, or DataStore::SignalId.
- Do not rename ms3_decode/ or break existing MS3 decoding.
- No CAN write/echo for detection: passive listen only.
- Build: `pio run -e esp32c3` must remain warning-free.

Adding a new profile
1) Create src/ecu/profiles/<name>_profile.h/.cpp
   - Implement `IEcuProfile`.
   - Provide DashSpec:
     * ids[] (pointer to a static array)
     * count (number of dash IDs)
     * require_min_rx_dash (minimum for early detect)
     * required_first_id (expected base ID)
   - Filtering:
     * acceptFrame() must reject msg.extd/msg.rtr if unsupported.
     * acceptId() tests membership in expected IDs.
   - Decode:
     * decode() fills `DecodedSignal out[]` and `count` via your decoder.
   - Dash helpers:
     * dashIndexForId(id): index 0..count-1 or -1 if unexpected.
     * dashIdCount()/dashIdAt(i): dash ID accessors.
   - Bitrates:
     * scanBitrates(count): ordered list to scan.
     * hasFixedBitrate()/fixedBitrate(): true if fixed, else false.

2) Wire the profile in EcuManager (today: auto or MS3):
   - Add the ID in `EcuProfileId`.
   - Expose the instance in ecu_manager.cpp (local singleton pattern is fine).
   - Optional: update detectOnBus() for your brand (passive listen, heuristic).

3) can_autobaud / processCan:
   - Already rely on `profile()` (acceptFrame/decode/dashIndex/scanBitrates).
   - Ensure DashSpec.count <= 5 while id_present_mask stays 5 bits.

Decode / bit-extract
- Utility `extractBits(..., BitOrder::MotorolaDBC|IntelLE)` in
  src/ecu/bit_extract.* (DBC-correct sawtooth for Motorola).
- Ms3Decoder uses `extractBits(..., BitOrder::MotorolaDBC)` + local sign
  extension. Do not break existing values.
- Bit order is defined per-signal (DBC semantics). There is no global/default
  bit order per ECU; each signal must specify its BitOrder explicitly.

Heuristic detection (current)
- EcuManager::detectOnBus passively listens after bitrate lock (~400 ms).
- Detects MS3 if base + at least one other dash ID and >=2 frames.
- Otherwise logs “Broadcast not detected / Ask your tuner” and remains auto (fallback MS3 by default).

Out-of-scope for other ECUs
- Add their decode table, DashSpec, bitrates, and safe detection heuristics.
+- Extend NVS persistence (EcuPersist.profile_id) if more profiles are supported.
