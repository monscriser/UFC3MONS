#pragma once
#include <cstdint>
uint32_t UfcFooterInspect(const uint8_t* base,uint32_t address);
uint32_t UfcFooterPrefixLength(const uint8_t* base,uint32_t address,uint32_t fallback);
