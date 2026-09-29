#include <iostream>
#include <fstream>
#include <iomanip>
#include "Bus.h"


Bus::Bus() : dma(this) {
    ram.resize(Hardware::RAM_SIZE, 0);
    bios.resize(Hardware::BIOS_SIZE, 0);
}

Bus::~Bus() {}


bool Bus::loadBIOS(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "FATAL: BIOS file couldn't be opened at " << filepath << "\n";
        exit(1);
    }
    file.read(reinterpret_cast<char*>(bios.data()), bios.size());
    file.close();
    return true;
}


uint8_t Bus::read8(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Main RAM with 8MB Mirroring (0x00000000 - 0x007FFFFF)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + (8 * 1024 * 1024)) {
        return ram[address & 0x001FFFFF];
    }

    // Expansion Region 1 (0x1F000000)
    else if (address >= 0x1F000000 && address <= 0x1F080000) {
        return 0xFF; // No cart plugged in
    }

    // Scratchpad RAM (0x1F800000 - 0x1F8003FF)
    if (address >= 0x1F800000 && address <= 0x1F8003FF) {
        return scratchpad[address & 0x3FF];
    }

    // Hardware Registers: SIO0 (0x1F801040)
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        return sio0.read8(address);
    }

    // Hardware Registers: DMA (0x1F801080 - 0x1F8010FF)
    else if (address >= 0x1F801080 && address <= 0x1F8010FF) {
        return dma.read8(address);
    }

    // Hardware Registers: CD-ROM (0x1F801800)
    else if (address >= Hardware::REG_CDROM_BASE && address <= Hardware::REG_CDROM_BASE + 3) {
        return cdrom.read8(address);
    }

    // BIOS ROM (0x1FC00000)
    else if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        return bios[address - Hardware::BIOS_STARTING_ADDRESS];
    }

    std::cout << "FATAL: Unhandled read8 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
    exit(1);
}


uint16_t Bus::read16(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Scratchpad RAM (0x1F800000 - 0x1F8003FF)
    if (address >= 0x1F800000 && address <= 0x1F8003FF) {
        uint32_t offset = address & 0x3FF;
        return scratchpad[offset] | (scratchpad[offset + 1] << 8);
    }


    // Main RAM with 8MB Mirroring (0x00000000 - 0x007FFFFF)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + (8 * 1024 * 1024)) {
        uint32_t offset = address & 0x001FFFFF;
        return ram[offset] | (ram[offset + 1] << 8);
    }

    // Hardware Registers: SIO0 (0x1F801040)
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        return sio0.read16(address);
    }

    // Hardware Registers: Interrupts (0x1F801070)
    else if (address == Hardware::REG_INTERRUPT_STATUS) {
        return interruptStatus;
    }
    else if (address == Hardware::REG_INTERRUPT_MASK) {
        return interruptMask;
    }

    // Hardware Registers: Timers (0x1F801100)
    else if (address >= 0x1F801100 && address <= 0x1F801128) {
        if (address == 0x1F801100) return timer0;
        else if (address == 0x1F801110) return timer1;
        else if (address == 0x1F801114) return timer1Mode;
        else if (address == 0x1F801118) return timer1Target;
        else if (address == 0x1F801120) return timer2;
        else if (address == 0x1F801124) return timer2Mode;
        else if (address == 0x1F801128) return timer2Target;

        std::cout << "FATAL: Unimplemented Timer read at 0x" << std::hex << address << "\n";
        exit(1);
    }

    // Hardware Registers: SPU (0x1F801C00)
    else if (address >= 0x1F801C00 && address <= 0x1F801DFF) {
       return spu.read16(address);
    }

    // BIOS ROM (0x1FC00000)
    else if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        uint32_t offset = address - Hardware::BIOS_STARTING_ADDRESS;
        return bios[offset] | (bios[offset + 1] << 8);
    }

    std::cout << "FATAL: Unhandled read16 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
    exit(1);
}


uint32_t Bus::read32(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Scratchpad RAM (0x1F800000 - 0x1F8003FF)
    if (address >= 0x1F800000 && address <= 0x1F8003FF) {
        uint32_t offset = address & 0x3FF;
        return scratchpad[offset] | 
              (scratchpad[offset + 1] << 8) | 
              (scratchpad[offset + 2] << 16) | 
              (scratchpad[offset + 3] << 24);
    }

    // Main RAM with 8MB Mirroring (0x00000000 - 0x007FFFFF)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + (8 * 1024 * 1024)) {
        uint32_t offset = address & 0x001FFFFF;
        return ram[offset] | (ram[offset + 1] << 8) | (ram[offset + 2] << 16) | (ram[offset + 3] << 24);
    }

    // Memory Control (0x1F801000 - 0x1F801020)
	else if (address >= 0x1F801000 && address <= 0x1F801020) {
		return memoryControlRegisters[(address - 0x1F801000) >> 2];
	}

    // Hardware Registers: SIO0 (0x1F801040)
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        return sio0.read32(address);
    }

    // Hardware Registers: Interrupts (0x1F801070)
    else if (address == Hardware::REG_INTERRUPT_STATUS) {
        return interruptStatus;
    }
    else if (address == Hardware::REG_INTERRUPT_MASK) {
        return interruptMask;
    }

    // Hardware Registers: DMA (0x1F801080 - 0x1F8010FF)
    else if (address >= 0x1F801080 && address <= 0x1F8010FF) {
  		return dma.read32(address);
  	}

    // Hardware Registers: Timers (0x1F801100)
    else if (address >= 0x1F801100 && address <= 0x1F801128) {
        if (address == 0x1F801100) return timer0;
        else if (address == 0x1F801110) return timer1;
        else if (address == 0x1F801114) return timer1Mode;
        else if (address == 0x1F801118) return timer1Target;
        else if (address == 0x1F801120) return timer2;
        else if (address == 0x1F801124) return timer2Mode;
        else if (address == 0x1F801128) return timer2Target;

        std::cout << "FATAL: Unimplemented Timer read at 0x" << std::hex << address << "\n";
        exit(1);
    }

    // Hardware Registers: GPU (0x1F801810)
    else if (address == Hardware::REG_GPU_GP0) {
        return gpu.readGP0();
    }
    else if (address == Hardware::REG_GPU_GP1) {
        return gpu.readGP1();
    }


    // Hardware Registers: MDEC (Video Decoder)
    else if (address == 0x1F801824) {
		return mdec.readStatus();
	}
	else if (address == 0x1F801820) {
		std::cout << "FATAL: Unimplemented MDEC data read at 0x1F801820\n";
		exit(1);
	}

    // Hardware Registers: SIO1 (Memory Card / Serial)
    else if (address >= 0x1F801050 && address <= 0x1F80105E) {
        std::cout << "FATAL: Unimplemented SIO1 access at 0x" << std::hex << address << "\n";
        exit(1);
    }

     // SPU
    else if(address >= 0x1F801C00 && address <= 0x1F801DFF) {
        return spu.read32(address);
    }


    // BIOS ROM (0x1FC00000)
    else if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        uint32_t offset = address - Hardware::BIOS_STARTING_ADDRESS;
        return bios[offset] | (bios[offset + 1] << 8) | (bios[offset + 2] << 16) | (bios[offset + 3] << 24);
    }

    std::cout << "FATAL: Unhandled read32 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
    exit(1);
}


void Bus::write8(uint32_t address, uint8_t value) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Scratchpad RAM (0x1F800000 - 0x1F8003FF)
    if (address >= 0x1F800000 && address <= 0x1F8003FF) {
        scratchpad[address & 0x3FF] = value;
        return;
    }


    // Main RAM with 8MB Mirroring (0x00000000 - 0x007FFFFF)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + (8 * 1024 * 1024)) {
        ram[address & 0x001FFFFF] = value;
    }

    // Hardware Registers: SIO0 (0x1F801040)
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        sio0.write8(address, value);
    }

    // Hardware Registers: DMA (0x1F801080 - 0x1F8010FF)
    else if (address >= 0x1F801080 && address <= 0x1F8010FF) {
        dma.write8(address, value);
    }

    // Hardware Registers: CD-ROM (0x1F801800)
    else if (address >= Hardware::REG_CDROM_BASE && address <= Hardware::REG_CDROM_BASE + 3) {
        cdrom.write8(address, value);
    }

    // BIOS POST Register (0x1F802041)
    else if (address == 0x1F802041) {
        std::cout << "BIOS: " << std::hex;
        switch (value) {
            case 0x00: std::cout << "Booting Shell"; break;
            case 0x01: std::cout << "CPU & ROM Check"; break;
            case 0x02: std::cout << "RAM Setup"; break;
            case 0x03: std::cout << "Initial RAM Test & Clear"; break;
            case 0x04: std::cout << "Installing Device Drivers & Interrupt Handlers"; break;
            case 0x05: std::cout << "Initializing Serial Comm & Controllers"; break;
            case 0x06: std::cout << "Initializing CD-ROM & Timers"; break;
            case 0x07: std::cout << "Configuring Machine Environment & DMA"; break;
            case 0x08: std::cout << "Loading Auto-Boot Executable from CD-ROM"; break;
            case 0x09: std::cout << "Jumping to Game Executable"; break;
            case 0x0E: std::cout << "Checking Expansion ROM (EXP1)"; break;
            case 0x0F: std::cout << "Initializing Coprocessor 0"; break;
            default:   std::cout << "Unknown POST Code"; break;
        }
        std::cout << " (0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)value << std::dec << std::nouppercase << ")" << std::endl;
    }
    else {
        std::cout << "FATAL: Unhandled write8 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
        exit(1);
    }
}


void Bus::write16(uint32_t address, uint16_t value) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Scratchpad RAM (0x1F800000 - 0x1F8003FF)
    if (address >= 0x1F800000 && address <= 0x1F8003FF) {
        uint32_t offset = address & 0x3FF;
        scratchpad[offset + 0] = value & 0xFF;
        scratchpad[offset + 1] = (value >> 8) & 0xFF;
        return;
    }


    // Main RAM with 8MB Mirroring (0x00000000 - 0x007FFFFF)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + (8 * 1024 * 1024)) {
        uint32_t offset = address & 0x001FFFFF;
        ram[offset + 0] = value & 0xFF;
        ram[offset + 1] = (value >> 8) & 0xFF;
    }

    // Hardware Registers: SIO0 (0x1F801040)
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        sio0.write16(address, value);
    }

    // Hardware Registers: Interrupts (0x1F801070)
    else if (address == Hardware::REG_INTERRUPT_STATUS) {
        interruptStatus &= value;
    }
    else if (address == Hardware::REG_INTERRUPT_MASK) {
        interruptMask = value;
    }

    // Hardware Registers: Timers (0x1F801100)
    else if (address >= 0x1F801100 && address <= 0x1F801128) {
        if (address == 0x1F801100) {
            timer0 = value & 0xFFFF;
        }
        else if (address == 0x1F801110) {
            timer1 = value & 0xFFFF;
        }
        else if (address == 0x1F801114) {
            timer1Mode = value & 0xFFFF;
            timer1 = 0;
        }
        else if (address == 0x1F801118) {
            timer1Target = value & 0xFFFF;
        }
        else if (address == 0x1F801120) {
            timer2 = value & 0xFFFF;
        }
        else if (address == 0x1F801124) {
            timer2Mode = value & 0xFFFF;
            timer2 = 0; // Mode write resets the counter
            timer2CycleAccumulator = 0;
        }
        else if (address == 0x1F801128) {
            timer2Target = value & 0xFFFF;
        }
    }

    // Hardware Registers: SPU (0x1F801C00)
    else if (address >= 0x1F801C00 && address <= 0x1F801DFF) {
        spu.write16(address, value);
    }
    else {
        std::cout << "FATAL: Unhandled write16 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
        exit(1);
    }
}


void Bus::write32(uint32_t address, uint32_t value) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Scratchpad RAM (0x1F800000 - 0x1F8003FF)
    if (address >= 0x1F800000 && address <= 0x1F8003FF) {
        uint32_t offset = address & 0x3FF;
        scratchpad[offset + 0] = value & 0xFF;
        scratchpad[offset + 1] = (value >> 8) & 0xFF;
        scratchpad[offset + 2] = (value >> 16) & 0xFF;
        scratchpad[offset + 3] = (value >> 24) & 0xFF;
        return;
    }


    // Main RAM with 8MB Mirroring (0x00000000 - 0x007FFFFF)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + (8 * 1024 * 1024)) {
        uint32_t offset = address & 0x001FFFFF;
        ram[offset + 0] = value & 0xFF;
        ram[offset + 1] = (value >> 8) & 0xFF;
        ram[offset + 2] = (value >> 16) & 0xFF;
        ram[offset + 3] = (value >> 24) & 0xFF;
    }

    // Memory Control (0x1F801000 - 0x1F801020)
    else if (address >= 0x1F801000 && address <= 0x1F801020) {
        memoryControlRegisters[(address - 0x1F801000) >> 2] = value;
    }

    // Hardware Registers: SIO0 (0x1F801040)
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        sio0.write32(address, value);
    }

    // RAM Size Configuration (0x1F801060)
    else if (address == 0x1F801060) {
        // Emulators can safely ignore physical RAM size configuration
    }

    // Hardware Registers: Interrupts (0x1F801070)
    else if (address == Hardware::REG_INTERRUPT_STATUS) {
        interruptStatus &= value;
    }
    else if (address == Hardware::REG_INTERRUPT_MASK) {
        interruptMask = value;
    }

    // Hardware Registers: DMA (0x1F801080 - 0x1F8010FF)
    else if (address >= 0x1F801080 && address <= 0x1F8010FF) {
  		dma.write32(address, value);
  	}

    // Hardware Registers: Timers (0x1F801100)
    else if (address >= 0x1F801100 && address <= 0x1F801128) {
        if (address == 0x1F801100) {
            timer0 = value & 0xFFFF;
        }
        else if (address == 0x1F801110) {
            timer1 = value & 0xFFFF;
        }
        else if (address == 0x1F801114) {
            timer1Mode = value & 0xFFFF;
            timer1 = 0;
        }
        else if (address == 0x1F801118) {
            timer1Target = value & 0xFFFF;
        }
        else if (address == 0x1F801120) {
            timer2 = value & 0xFFFF;
        }
        else if (address == 0x1F801124) {
            timer2Mode = value & 0xFFFF;
            timer2 = 0;
            timer2CycleAccumulator = 0;
        }
        else if (address == 0x1F801128) {
            timer2Target = value & 0xFFFF;
        }
    }

    // Hardware Registers: GPU (0x1F801810)
    else if (address == Hardware::REG_GPU_GP0) {
        gpu.writeGP0(value);
    }
    else if (address == Hardware::REG_GPU_GP1) {
        gpu.writeGP1(value);
    }

    // Hardware Registers: MDEC (Video Decoder)
    else if (address == 0x1F801824) {
		mdec.writeControl(value);
	}
	else if (address == 0x1F801820) {
		mdec.writeCommand(value);
	}

    // Hardware Registers: SIO1 (Memory Card / Serial)
    else if (address >= 0x1F801050 && address <= 0x1F80105E) {
        std::cout << "FATAL: Unimplemented SIO1 access at 0x" << std::hex << address << "\n";
        exit(1);
    }

    // SPU
    else if(address >= 0x1F801C00 && address <= 0x1F801DFF) {
        spu.write32(address, value);
    }

    // Cache Control (0x1FFE0130)
    else if (address == 0x1FFE0130) {
        // Safe to ignore
    }
    else {
        std::cout << "FATAL: Unhandled write32 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
        exit(1);
    }
}


void Bus::tickHardware(int cycles) {
    // Feed cycles to the CD-ROM state machine
    cdrom.tick(cycles);

    dma.tick(cycles);


    bool currentCDROMInt = cdrom.checkInterrupt();
    bool newInterruptDelivered = cdrom.consumeDeliveredInterrupt();
    if (currentCDROMInt && (!lastCDROMInt || newInterruptDelivered)) {
        interruptStatus |= Hardware::IRQ_CDROM;
    }
    lastCDROMInt = currentCDROMInt;

    // GPU Interrupt (IRQ1) - Edge Triggered
    if (gpu.consumeInterruptRequest()) {
        interruptStatus |= Hardware::IRQ_GPU;
    }

    for (int i = 0; i < cycles; i++) {
        bool currentSio0Int = sio0.tick();
        if (currentSio0Int && !lastSio0Int) {
            interruptStatus |= 0x0080;
        }
        lastSio0Int = currentSio0Int;
    }


    // Timers
    // TODO: Implement full hardware logic for Timer 0 (Dot Clock) and Timer 1 (HBlank)
    timer0 += cycles;

    // Timer 1
    bool hblankMode = ((timer1Mode >> 8) & 1) != 0;   // sources 1 and 3

    if (!hblankMode) {
        // System Clock mode (Ticks every cycle)
        timer1 += cycles;
    } else {
        // H-Blank mode
        static int hblankAccumulator = 0;
        hblankAccumulator += cycles;
        while (hblankAccumulator >= Hardware::CYCLES_PER_SCANLINE) {
            timer1++;
            hblankAccumulator -= Hardware::CYCLES_PER_SCANLINE;
        }
    }

    // Timer 2 (System Clock / 8)
    int clockSource = (timer2Mode >> 8) & 0x03;
    int divisor = (clockSource == 2 || clockSource == 3) ? 8 : 1;

    timer2CycleAccumulator += cycles;

    while (timer2CycleAccumulator >= divisor) {
        timer2CycleAccumulator -= divisor;
        bool resetToZero = false;

        // 16-bit Overflow check (Checked BEFORE we increment)
        if (timer2 == 0xFFFF) {
            resetToZero = true;
            // Bit 5: IRQ on Overflow
            if ((timer2Mode & 0x0020) != 0) {
                interruptStatus |= 0x0040; // Trigger IRQ6 (Timer 2)
            }
        }

        timer2++; // Naturally wraps to 0x0000 if it was 0xFFFF

        // Target hit check
        if (timer2 == timer2Target) {
            // Bit 3: Reset on Target
            if ((timer2Mode & 0x0008) != 0) resetToZero = true;

            // Bit 4: IRQ on Target
            if ((timer2Mode & 0x0010) != 0) {
                interruptStatus |= 0x0040; // Trigger IRQ6 (Timer 2)
            }
        }

        if (resetToZero) timer2 = 0;
    }

    // VBlank (60Hz / NTSC Pacing)
    vblankCounter += cycles;
    if (vblankCounter >= Hardware::CYCLES_PER_FRAME) {
        interruptStatus |= Hardware::IRQ_VBLANK; // Trigger IRQ0
        vblankCounter -= Hardware::CYCLES_PER_FRAME; // Subtract to maintain sub-cycle accuracy

        gpu.toggleFrameBit();
    }
}