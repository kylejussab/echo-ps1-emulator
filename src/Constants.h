#pragma once
#include <cstdint>

namespace Hardware {
    // BIOS Memory Map
    constexpr uint32_t BIOS_STARTING_ADDRESS = 0x1FC00000;
    constexpr uint32_t BIOS_SIZE  = 512 * 1024;
    
    // RAM Memory Map 
    constexpr uint32_t RAM_STARTING_ADDRESS  = 0x00000000;
    constexpr uint32_t RAM_SIZE   = 2 * 1024 * 1024;

    // CPU Specifications
    constexpr uint32_t INSTRUCTION_SIZE = 4; // 4 bytes (32 bits)
}
