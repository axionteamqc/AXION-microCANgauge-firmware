# CAN autobaud – current implementation (Megasquirt profile)

Scope: firmware build Vcurrent – files `src/can_link/can_autobaud.cpp`, `src/can_link/twai_link.cpp`, `src/ecu/ecu_profile.h`, `src/ecu/profiles/ms3_evoplus_profile.cpp`.

## States and timing
* Listen-only phase per candidate bitrate (profile-supplied list; MS3: 500k, 250k, 1M, 125k).
  * Window: `spec.listen_window_ms` (MS3: 150 ms).
  * Early exit if `rx_dash >= min_rx_dash` (spec min_rx_dash or default 2) AND `meetsDashMinimum` AND no bus_off/err_pass/rx_overrun.
  * Counts: rx_total, rx_dash, id_counts[0..4], id_present_mask, bus_off, err_passive, rx_overrun; rx_missed later via twai_get_status_info.
* On early candidate: stop/uninstall listen-only, immediately run NORMAL validation.
* If no early candidate: at end of window, if `rx_dash >= min_rx_dash` (default 5) and meetsDashMinimum and no errors -> run NORMAL validation.
* If normal validation fails, driver is stopped/uninstalled and scan continues with next bitrate.
* If no bitrate locks, best stats (highest rx_dash) are returned with `locked=false`.

## NORMAL validation (per candidate)
* Mode: TWAI_MODE_NORMAL (acks transmitted).
* Window: `validationSpec.window_ms` or default 800 ms (MS3: 400 ms).
* Pass criteria:
  * rx_dash >= spec.min_rx_dash (MS3: 12)
  * ID stability: `meetsIdStability` (>=3 per ID or >=require_distinct_ids) AND `meetsDashMinimum` (ID0 seen AND at least one other ID).
  * bus_off == 0, err_passive == 0, rx_overrun == 0, rx_missed <= spec.max_rx_missed (MS3: 2).
* On pass: lock stored with bitrate, id_present_mask from NORMAL stats, hash_match=true.

## TWAI bring-up details
* `TwaiLink::startWithMode` installs driver with:
  * tx_queue_len = 0 in LISTEN_ONLY, =1 in NORMAL (but no app TX is issued).
  * rx_queue_len = 64.
  * Alerts enabled: RX_DATA, ERR_PASS, BUS_OFF, RX_QUEUE_FULL.
  * Filter: accept all; timing: 500k default, or 1M/250k/125k presets.

## Profile (Megasquirt)
* Dash IDs: 0x5E8..0x5EC (5 slots). Requires ID0 seen and at least one other ID.
* Autobaud spec: window 150 ms listen, 12 min rx_dash overall, require_min_per_id=3 or >=4 distinct, max_rx_missed=2, require_bus_off_clear=true.
* Validation spec: 400 ms window, min_rx_dash=12.

## Risks / points d’attention
1) NORMAL validation sends ACKs during scan: if connected to a live MS3 that tolerates this it is fine, but on other ECUs this could be intrusive. Current design does not offer a “listen-only only” lock.
2) Early promotion to NORMAL after as few as 2 dash frames (default) + meetsDashMinimum can start ACKing within ~150 ms of boot. If the bus owner is sensitive to unexpected ACKs, this is the main risk.
3) Filters are accept-all; any non-MS3 traffic can satisfy rx_total/rx_dash thresholds if it shares the same IDs. The profile limits dash IDs via acceptFrame, but rx_total can still be high due to other frames; only rx_dash/ID stability gates the lock.
4) No back-off between rates: repeatedly starting/stopping driver quickly across 4 rates could cause momentary gaps; however tx_queue_len=0 in listen-only so no TX frames during scan.
5) When lock fails, best stats are returned but no driver remains installed; caller must handle this (today: wizard fails / Wi-Fi shows unlock).
