#pragma once

// Accessors for AP SSID/password and persistence.
const char* WifiApSsid();
const char* WifiApPass();
bool WifiApPassSetAndPersist(const char* new_pass);
void WifiApPassResetToDefault();
bool WifiApPassValid(const char* pass);
