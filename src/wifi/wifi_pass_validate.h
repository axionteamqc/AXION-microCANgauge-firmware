#pragma once

#include <stddef.h>

// Validates Wi-Fi AP password per project rules.
// - 8..16 chars
// - printable ASCII (32..126)
// - rejects '"', '<', '>' to avoid HTML/log issues
// Returns true if valid; if err is provided, writes a short reason.
bool ValidateWifiApPass(const char* s, char* err = nullptr, size_t err_len = 0);
