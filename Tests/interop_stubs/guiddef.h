#pragma once
#include <cstdint>
struct GUID {
    std::uint32_t Data1;
    std::uint16_t Data2, Data3;
    std::uint8_t Data4[8];
};
