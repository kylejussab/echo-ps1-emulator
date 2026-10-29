#pragma once

#include <cstdint>

namespace Hardware {
    // BIOS Memory Map
    constexpr uint32_t BIOS_STARTING_ADDRESS = 0x1FC00000;
    constexpr uint32_t BIOS_SIZE = 512 * 1024;

    // BIOS ROM (physical addresses) and the cost of fetching one instruction word from it
    constexpr uint32_t BIOS_ROM_START = 0x1FC00000;
    constexpr uint32_t BIOS_ROM_END = 0x1FC80000;
    constexpr int BIOS_ROM_FETCH_CYCLES = 24;
    
    // Hardware Register Base Addresses
    constexpr uint32_t REG_INTERRUPT_STATUS = 0x1F801070;
    constexpr uint32_t REG_INTERRUPT_MASK = 0x1F801074;
    
    constexpr uint32_t REG_GPU_GP0 = 0x1F801810;
    constexpr uint32_t REG_GPU_GP1 = 0x1F801814;

    constexpr uint32_t REGISTER_DMA_BASE = 0x1F801080; // 0x1F801080 - 0x1F8010FF
    constexpr uint32_t REGISTER_DMA_CONTROL = 0x1F8010F0;
    constexpr uint32_t REGISTER_DMA_INTERRUPT_CONTROL = 0x1F8010F4;


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
    constexpr uint16_t IRQ_SPU    = 0x0200; // Bit 9: SPU

    // Display Timing
    constexpr double TARGET_FRAMES_PER_SECOND = 60.0; // Used for frame pacing only

    // NTSC video timing: a scanline is 3413 video clocks, converted to CPU cycles through the real clock ratio
    constexpr double NTSC_VIDEO_CLOCK_SPEED_HERTZ = 53693175.0;
    constexpr int VIDEO_CLOCKS_PER_SCANLINE_NTSC = 3413;
    constexpr int SCANLINES_PER_FRAME_NTSC = 263;
    constexpr int CYCLES_PER_SCANLINE = static_cast<int>(VIDEO_CLOCKS_PER_SCANLINE_NTSC * CPU_CLOCK_SPEED_HERTZ / NTSC_VIDEO_CLOCK_SPEED_HERTZ + 0.5); // 2153

    // PAL video timing: a scanline is 3406 video clocks and a frame is 314 lines (about 49.76 Hz)
    constexpr double PAL_VIDEO_CLOCK_SPEED_HERTZ = 53203425.0;
    constexpr int VIDEO_CLOCKS_PER_SCANLINE_PAL = 3406;
    constexpr int SCANLINES_PER_FRAME_PAL = 314;
    constexpr int CYCLES_PER_SCANLINE_PAL = static_cast<int>(VIDEO_CLOCKS_PER_SCANLINE_PAL * CPU_CLOCK_SPEED_HERTZ / PAL_VIDEO_CLOCK_SPEED_HERTZ + 0.5); // 2168
    constexpr int CYCLES_PER_FRAME_PAL = CYCLES_PER_SCANLINE_PAL * SCANLINES_PER_FRAME_PAL;

    // Pre-calculated metrics and overscan
    constexpr int CYCLES_PER_FRAME = CYCLES_PER_SCANLINE * SCANLINES_PER_FRAME_NTSC; // 566239, about 59.82 Hz
    constexpr uint32_t MILLISECONDS_PER_FRAME = static_cast<uint32_t>(1000.0 / TARGET_FRAMES_PER_SECOND);
    
    // Display Offset & Overscan Constants
    constexpr int DISPLAY_OFFSET_NTSC_V1 = 16; // Baseline scanlines for NTSC vertical blanking
    constexpr int OVERSCAN_CROP_240P = 12;     // Standard crop for progressive scan games
    constexpr int OVERSCAN_CROP_480I = 24;     // Standard crop for interlaced menus/BIOS

    // CD-ROM Memory Map
    constexpr uint32_t REG_CDROM_BASE   = 0x1F801800; // 0x1F801800 - 0x1F801803

    // CD-ROM Drive Mechanics & Timing
    constexpr int CD_SECTORS_PER_SECOND_1X = 75;
    constexpr int CD_SECTORS_PER_SECOND_2X = 150;
    
    constexpr int DELAY_CDROM_ACK = 50401;                    // First response, 0xC4E1 (Nop)
    constexpr int DELAY_CDROM_INIT_ACK = 81102;               // First response of Init, 0x13CCE
    constexpr int DELAY_CDROM_GETID_SECOND_RESPONSE = 18944;  // 0x4A00
    constexpr int DELAY_CDROM_PAUSE_SINGLE_SPEED = 2168860;   // 0x21181C, about 5 sectors
    constexpr int DELAY_CDROM_PAUSE_DOUBLE_SPEED = 1097107;   // 0x10BD93
    constexpr int DELAY_CDROM_PAUSE_WHEN_PAUSED = 7666;       // 0x1DF2
    constexpr int DELAY_CDROM_READ_SINGLE_SPEED = 451584;     // CPU clock * 0x930 / 4 / 44100
    constexpr int DELAY_CDROM_READ_DOUBLE_SPEED = 225792;

    // Real mechanical CD motor spin-up time (~1 to 1.6 seconds on real hardware)
    constexpr int DELAY_CDROM_MOTOR_SPINUP = static_cast<int>(CPU_CLOCK_SPEED_HERTZ * 0.005);



    // GPU DRAW TIMING MODEL: EMPIRICAL, NOT HARDWARE-ACCURATE
    // These costs were NOT measured on real hardware or taken from documentation. They were found by
    // trial and error so that one known scene (Resident Evil 2's opening narration) takes about as long
    // as it does on a real console. They could be wrong, and other games may need different values.
    // The model only estimates how long drawing keeps the GPU busy. It does not model the GPU's real
    // pipeline, command FIFO or memory bandwidth. If a game runs too slow or too fast, suspect
    // GPU_COST_SCALE first.
    constexpr double GPU_CLOCKS_TO_CPU_CYCLES = CPU_CLOCK_SPEED_HERTZ / NTSC_VIDEO_CLOCK_SPEED_HERTZ; // about 0.63
    constexpr double GPU_COST_SCALE           = 1.45;   // global multiplier
    constexpr double GPU_COST_PIXEL_BASE      = 1.0;
    constexpr double GPU_COST_PIXEL_TEXTURED  = 1.0;   // extra per textured pixel
    constexpr double GPU_COST_PIXEL_BLEND     = 1.0;   // extra per semi-transparent pixel
    constexpr double GPU_COST_PRIMITIVE_SETUP = 40.0;
    constexpr double GPU_COST_ROW_SETUP       = 2.0;
    constexpr int64_t GPU_MAX_BACKLOG_CYCLES  = 3 * CYCLES_PER_FRAME;   // stand-in for the real GPU FIFO throttling the CPU
}