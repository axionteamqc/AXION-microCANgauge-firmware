# CAN_GAUGE V44 LOCK - Behavior Guardrails

Keep these invariants unchanged during clean-ups/refactors:
- ECU profile forced to **Megasquirt** (no AUTO detect in release).
- Auto-baud flow: listen-only candidate + normal ACK validation; bitrate list and MS3 dash ID expectations unchanged.
- UI inversion: no screen invert while menu/edit/wizard is active; alert inversion only when allowed by current logic.
- Per-page units and per-page alert enable masks persist in NVS; defaults all alerts enabled, units metric unless set.
- Factory Reset wipes the entire `cangauge` NVS namespace and reboots.
- Data source switch: mock vs real CAN controlled by the single `AppConfig::kDataSource` "ONE-LINE SWITCH"; when set to CAN and lock present, CAN feeds the datastore (UI wiring unchanged).
- Page navigation vs boot pages: runtime `page_index` changes are not auto-persisted; boot pages persist only via the explicit **Save Screens** action (stored in `boot_page_index`).

Build (ESP32-C3, Arduino):
```sh
pio run -e esp32c3
```
