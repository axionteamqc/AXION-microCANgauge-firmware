#pragma once

// Dev-only build flags (set via platformio.ini build_flags).
// NOTE: Do not define defaults for DEBUG_* here because some sites use #ifdef.
// Known flags:
//   DEBUG_STALE_OLED2
//   DEBUG_VALIDATE_SIGNAL_CONTRACT
//   DEBUG_DNS

#ifndef DEV_INJECT_INVALID
#define DEV_INJECT_INVALID 0
#endif

#ifndef DEV_INJECT_STALE
#define DEV_INJECT_STALE 0
#endif

