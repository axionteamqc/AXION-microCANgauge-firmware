# THREADING

This project runs a few FreeRTOS tasks plus the Arduino main loop.

Tasks:
- App main loop (setup + AppLoopTick): owns most AppState fields and UI flow.
- CanRxTask: TWAI receive/alerts, updates CAN stats/diag and TWAI status.
- ButtonTask: debounces button and posts click/hold events.
- Wi-Fi portal/server: handled in main loop when Wi-Fi mode active.

Shared state:
- g_state is the global AppState shared across tasks.
- Main loop owns non-volatile UI/config fields; other tasks read them only.
- ButtonTask writes volatile btn_* fields; main loop consumes/clears.
- CanRxTask writes CAN-related fields (last_can_*, can_stats, can_diag, TWAI status).
- UI/menus read CAN fields via GetCanStateSnapshot().

Synchronization rules:
- Use g_state_mux for any cross-task read/write of CAN fields.
- Keep critical sections short and copy locals before locking when possible.
- Do not hold g_state_mux while calling drivers (TWAI/WiFi/OLED).

Datastore consistency:
- data/datastore.* uses a seqlock per slot (seq odd while writing).
- Writers: seq++ (odd), write fields, seq++ (even).
- Readers: read seq1/fields/seq2 and retry on mismatch/odd.

UI guidance:
- Prefer snapshot helpers in UI code to avoid torn reads.
- Avoid blocking loops in AppLoopTick; budget work per tick.
