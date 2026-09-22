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

    // Hardware Interrupts (IRQ) ---
    constexpr uint16_t IRQ_VBLANK = 0x0001; // Bit 0: Vertical Blank (TV Refresh)
    constexpr uint16_t IRQ_GPU    = 0x0002; // Bit 1: GPU Interrupt
    constexpr uint16_t IRQ_CDROM  = 0x0004; // Bit 2: CD-ROM Interrupt
    constexpr uint16_t IRQ_DMA    = 0x0008; // Bit 3: DMA Interrupt
    constexpr uint16_t IRQ_TIMER0 = 0x0010; // Bit 4: Timer 0
    constexpr uint16_t IRQ_TIMER1 = 0x0020; // Bit 5: Timer 1
    constexpr uint16_t IRQ_TIMER2 = 0x0040; // Bit 6: Timer 2
    constexpr uint16_t IRQ_PAD_MEM= 0x0080; // Bit 7: Controller & Memory Card

    // Display Timing
    constexpr double TARGET_FRAMES_PER_SECOND = 60.0; // NTSC refresh rate


    // Pre-calculated metrics and overscan
    constexpr int CYCLES_PER_FRAME = static_cast<int>(CPU_CLOCK_SPEED_HERTZ / TARGET_FRAMES_PER_SECOND);
    constexpr uint32_t MILLISECONDS_PER_FRAME = static_cast<uint32_t>(1000.0 / TARGET_FRAMES_PER_SECOND);
    
    constexpr int OVERSCAN_CROP_TOP = 0;
    constexpr int OVERSCAN_CROP_BOTTOM = 5;

    // Display Timing & Scanlines
    constexpr int SCANLINES_PER_FRAME_NTSC = 263;
    constexpr int CYCLES_PER_SCANLINE = CYCLES_PER_FRAME / SCANLINES_PER_FRAME_NTSC;


    // CD-ROM Memory Map
    constexpr uint32_t REG_CDROM_BASE   = 0x1F801800; // 0x1F801800 - 0x1F801803

    // CD-ROM Drive Mechanics & Timing
    constexpr int CD_SECTORS_PER_SECOND_1X = 75;
    constexpr int CD_SECTORS_PER_SECOND_2X = 150;
    
    // ~225,792 cycles for a 2x speed sector read
    constexpr int DELAY_CDROM_READ = static_cast<int>(CPU_CLOCK_SPEED_HERTZ / CD_SECTORS_PER_SECOND_2X); 
    
    // General mechanical delay for seek/acknowledge (~10k-50k cycles on real silicon)
    constexpr int DELAY_CDROM_ACK = 25000; 
}
