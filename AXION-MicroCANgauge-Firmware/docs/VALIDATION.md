# Display Inversion / Alert Validation (dev checklist)

- SmallOnly: alert inverts the whole small display (unchanged vs baseline).
- DualSmall: left and right displays invert independently (unchanged).
- LargeOnly: top zone can invert while bottom stays normal, and vice versa.
- LargePlusSmall: large top/bottom invert independently; small display inversion unchanged.
- Menu/Edit/Wizard: inversion disabled (existing gating) — no flashing while in these states.
- Alert marker: still blinks ~2 Hz and remains to the left of the big value.

No code changes required for this checklist; use it to verify visual behavior on hardware.
