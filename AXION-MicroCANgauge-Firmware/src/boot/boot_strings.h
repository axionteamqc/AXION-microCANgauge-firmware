#pragma once

#include <Arduino.h>

// Runtime boot strings sourced from FactoryConfig defaults, overridable via NVS.
void BootStringsInitFromNvs();
const char* BootHello1();      // first hello line
const char* BootBrandText();   // loading bar title
const char* BootHello2();      // second hello line
void BootStringsSet(const char* brand, const char* hello1,
                    const char* hello2);  // used after /apply
