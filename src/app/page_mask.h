#pragma once

#include <stdint.h>

constexpr uint32_t PageMaskAll(uint8_t page_count) {
  return (page_count >= 32u) ? 0xFFFFFFFFu
                             : static_cast<uint32_t>((1u << page_count) - 1u);
}

constexpr bool IsValidPageIndex(uint8_t idx, uint8_t page_count) {
  return idx < page_count;
}
