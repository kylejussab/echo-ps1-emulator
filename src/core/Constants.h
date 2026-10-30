#pragma once

#include <cstdint>

namespace Hardware {
    // System Clock & Architecture
    constexpr double CPU_CLOCK_SPEED_HERTZ = 33868800.0; // Real PS1 CPU clock speed
    constexpr uint32_t INSTRUCTION_SIZE = 4;             // 4 bytes (32 bits)

    // Main Memory Map
    constexpr uint32_t RAM_STARTING_ADDRESS = 0x00000000;
    constexpr uint32_t RAM_SIZE = 2 * 1024 * 1024; // 2MB RAM

    constexpr uint32_t BIOS_STARTING_ADDRESS = 0x1FC00000;
    constexpr uint32_t BIOS_SIZE = 512 * 1024; // 512KB ROM

    constexpr uint32_t BIOS_ROM_START = 0x1FC00000;
    constexpr uint32_t BIOS_ROM_END = 0x1FC80000;

    /// @todo (timing): fixed 24-cycle ROM fetch. psx-spx derives access time from DEV2 Delay/Size (1F801010h), COM_DELAY (1F801020h) and the bus width (a 32-bit access on an 8-bit bus is 4 byte accesses, 1ST + 3xSEQ). The BIOS boots with the slowest settings and speeds up after writing 1F801010h. SPU, CD-ROM and expansion accesses use their own delay registers (1F801014h/18h/08h/1Ch).
    constexpr int BIOS_ROM_FETCH_CYCLES = 24; // Cost of fetching one instruction word from ROM
    
    // ardware Register Base Addresses
    constexpr uint32_t REG_INTERRUPT_STATUS = 0x1F801070;
    constexpr uint32_t REG_INTERRUPT_MASK   = 0x1F801074;
    
    constexpr uint32_t REG_GPU_GP0 = 0x1F801810;
    constexpr uint32_t REG_GPU_GP1 = 0x1F801814;

    constexpr uint32_t REGISTER_DMA_BASE = 0x1F801080; // 0x1F801080 - 0x1F8010FF
    constexpr uint32_t REGISTER_DMA_CONTROL = 0x1F8010F0;
    constexpr uint32_t REGISTER_DMA_INTERRUPT_CONTROL = 0x1F8010F4;

    constexpr uint32_t REG_CDROM_BASE = 0x1F801800; // 0x1F801800 - 0x1F801803

    // Hardware Interrupts (IRQ)
    constexpr uint16_t IRQ_VBLANK  = 0x0001; // Bit 0: Vertical Blank (TV Refresh)
    constexpr uint16_t IRQ_GPU     = 0x0002; // Bit 1: GPU
    constexpr uint16_t IRQ_CDROM   = 0x0004; // Bit 2: CD-ROM
    constexpr uint16_t IRQ_DMA     = 0x0008; // Bit 3: DMA
    constexpr uint16_t IRQ_TIMER0  = 0x0010; // Bit 4: Timer 0
    constexpr uint16_t IRQ_TIMER1  = 0x0020; // Bit 5: Timer 1
    constexpr uint16_t IRQ_TIMER2  = 0x0040; // Bit 6: Timer 2
    constexpr uint16_t IRQ_PAD_MEM = 0x0080; // Bit 7: Controller & Memory Card (SIO0)
    constexpr uint16_t IRQ_SIO0    = 0x0080; // Alias for Bit 7 to keep code intent clear
    constexpr uint16_t IRQ_SIO1    = 0x0100; // Bit 8: Serial Port 1 (SIO1)
    constexpr uint16_t IRQ_SPU     = 0x0200; // Bit 9: SPU

    // GPU Memory & Display Defaults
    constexpr uint32_t VRAM_SIZE = 1 * 1024 * 1024; // 1MB total
    constexpr uint32_t VRAM_WIDTH = 1024;
    constexpr uint32_t VRAM_HEIGHT = 512;
    
    constexpr uint32_t GPU_DEFAULT_STATUS = 0x14800000;
    constexpr uint16_t GPU_DEFAULT_DISPLAY_WIDTH = 320;
    constexpr uint16_t GPU_DEFAULT_DISPLAY_HEIGHT = 240;

    // Display Timing & Overscan
    constexpr double TARGET_FRAMES_PER_SECOND = 60.0; // Used for frontend frame pacing only
    constexpr uint32_t MILLISECONDS_PER_FRAME = static_cast<uint32_t>(1000.0 / TARGET_FRAMES_PER_SECOND);
    
    // NTSC video timing: a scanline is 3413 video clocks, converted to CPU cycles through the real clock ratio
    constexpr double NTSC_VIDEO_CLOCK_SPEED_HERTZ = 53693175.0;
    constexpr int VIDEO_CLOCKS_PER_SCANLINE_NTSC = 3413;
    constexpr int SCANLINES_PER_FRAME_NTSC = 263;
    constexpr int CYCLES_PER_SCANLINE = static_cast<int>(VIDEO_CLOCKS_PER_SCANLINE_NTSC * CPU_CLOCK_SPEED_HERTZ / NTSC_VIDEO_CLOCK_SPEED_HERTZ + 0.5); // ~2153
    constexpr int CYCLES_PER_FRAME = CYCLES_PER_SCANLINE * SCANLINES_PER_FRAME_NTSC; // 566239, ~59.82 Hz

    // PAL video timing: a scanline is 3406 video clocks and a frame is 314 lines (~49.76 Hz)
    constexpr double PAL_VIDEO_CLOCK_SPEED_HERTZ = 53203425.0;
    constexpr int VIDEO_CLOCKS_PER_SCANLINE_PAL = 3406;
    constexpr int SCANLINES_PER_FRAME_PAL = 314;
    constexpr int CYCLES_PER_SCANLINE_PAL = static_cast<int>(VIDEO_CLOCKS_PER_SCANLINE_PAL * CPU_CLOCK_SPEED_HERTZ / PAL_VIDEO_CLOCK_SPEED_HERTZ + 0.5); // ~2168
    constexpr int CYCLES_PER_FRAME_PAL = CYCLES_PER_SCANLINE_PAL * SCANLINES_PER_FRAME_PAL;
    
    // Display Offset & Overscan Constants
    constexpr int DISPLAY_OFFSET_NTSC_V1 = 16; // Baseline scanlines for NTSC vertical blanking
    constexpr int OVERSCAN_CROP_240P = 12; // Standard crop for progressive scan games
    constexpr int OVERSCAN_CROP_480I = 24; // Standard crop for interlaced menus/BIOS

    // CD-ROM Drive Mechanics & Timing

    constexpr int CD_SECTORS_PER_SECOND_1X = 75;
    constexpr int CD_SECTORS_PER_SECOND_2X = 150;
    
    constexpr int DELAY_CDROM_ACK                   = 50401;   // First response, 0xC4E1 (Nop)
    constexpr int DELAY_CDROM_INIT_ACK              = 81102;   // First response of Init, 0x13CCE
    constexpr int DELAY_CDROM_GETID_SECOND_RESPONSE = 18944;   // 0x4A00
    constexpr int DELAY_CDROM_PAUSE_SINGLE_SPEED    = 2168860; // 0x21181C, ~5 sectors
    constexpr int DELAY_CDROM_PAUSE_DOUBLE_SPEED    = 1097107; // 0x10BD93
    constexpr int DELAY_CDROM_PAUSE_WHEN_PAUSED     = 7666;    // 0x1DF2
    constexpr int DELAY_CDROM_READ_SINGLE_SPEED     = 451584;  // CPU clock * 0x930 / 4 / 44100
    constexpr int DELAY_CDROM_READ_DOUBLE_SPEED     = 225792;

    // Real mechanical CD motor spin-up time (~1 to 1.6 seconds on real hardware)
    constexpr int DELAY_CDROM_MOTOR_SPINUP = static_cast<int>(CPU_CLOCK_SPEED_HERTZ * 0.005);

    // GPU Draw Timing Model
    
    // WARNING: EMPIRICAL, NOT HARDWARE-ACCURATE
    // These costs were NOT measured on real hardware or taken from documentation. They were found by trial and error so that one known scene (Resident Evil 2's opening narration) takes about as long as it does on a real console. They could be wrong, and other games may need different values.
    // The model only estimates how long drawing keeps the GPU busy. It does not model the GPU's real pipeline, command FIFO or memory bandwidth. If a game runs too slow or too fast, suspect GPU_COST_SCALE first.
    constexpr double GPU_CLOCKS_TO_CPU_CYCLES = CPU_CLOCK_SPEED_HERTZ / NTSC_VIDEO_CLOCK_SPEED_HERTZ; // ~0.63
    constexpr double GPU_COST_SCALE           = 1.45;  // global multiplier
    constexpr double GPU_COST_PIXEL_BASE      = 1.0;
    constexpr double GPU_COST_PIXEL_TEXTURED  = 1.0;   // extra per textured pixel
    constexpr double GPU_COST_PIXEL_BLEND     = 1.0;   // extra per semi-transparent pixel
    constexpr double GPU_COST_PRIMITIVE_SETUP = 40.0;
    constexpr double GPU_COST_ROW_SETUP       = 2.0;
    constexpr int64_t GPU_MAX_BACKLOG_CYCLES  = 3 * CYCLES_PER_FRAME; // stand-in for the real GPU FIFO throttling the CPU
}
