#pragma once
#include <cstdint>

namespace Hardware {
    // BIOS Memory Map
    constexpr uint32_t BIOS_STARTING_ADDRESS = 0x1FC00000;
    constexpr uint32_t BIOS_SIZE = 512 * 1024;
    
    // Hardware Register Base Addresses
    constexpr uint32_t REG_INTERRUPT_STATUS = 0x1F801070;
    constexpr uint32_t REG_INTERRUPT_MASK = 0x1F801074;
    
    constexpr uint32_t REG_GPU_GP0 = 0x1F801810;
    constexpr uint32_t REG_GPU_GP1 = 0x1F801814;

    constexpr uint32_t REG_DMA_DPCR = 0x1F8010F0;
    constexpr uint32_t REG_DMA_DICR = 0x1F8010F4;


    // RAM Memory Map 
    constexpr uint32_t RAM_STARTING_ADDRESS = 0x00000000;
    constexpr uint32_t RAM_SIZE = 2 * 1024 * 1024;

    // CPU Specifications
    constexpr uint32_t INSTRUCTION_SIZE = 4; // 4 bytes (32 bits)
    constexpr double CPU_CLOCK_SPEED_HERTZ = 33868800.0; // Real PS1 CPU clock speed

    // GPU Memory Map
    constexpr uint32_t VRAM_SIZE = 1 * 1024 * 1024; // 1MB total
    constexpr uint32_t VRAM_WIDTH = 1024;
    constexpr uint32_t VRAM_HEIGHT = 512;
    
    constexpr uint32_t GPU_DEFAULT_STATUS = 0x14800000;
    constexpr uint16_t GPU_DEFAULT_DISPLAY_WIDTH = 320;
    constexpr uint16_t GPU_DEFAULT_DISPLAY_HEIGHT = 240;

    // Display Timing
    constexpr double TARGET_FRAMES_PER_SECOND = 60.0; // NTSC refresh rate
}
