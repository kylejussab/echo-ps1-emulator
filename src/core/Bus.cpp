#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include "Bus.h"


Bus::Bus() : dma(this) {
    ram.resize(Hardware::RAM_SIZE, 0);
    bios.resize(Hardware::BIOS_SIZE, 0);

    timers.setInterruptCallback([this](uint16_t interruptBit) { interruptStatus |= interruptBit; });
    timers.setVBlankCallback([this]() {
        interruptStatus |= Hardware::IRQ_VBLANK; // IRQ0
        totalFrames++;
        gpu.toggleFrameBit();
    });

    spu.setInterruptCallback([this](uint16_t interruptBit) { interruptStatus |= interruptBit; });
    spu.setCDAudioSource([this](int16_t& left, int16_t& right) { return cdrom.readAudioSample(left, right); }); 
    timers.setVideoModeCallback([this]() { return palMode; });
    timers.setDotDividerCallback([this]() {
        switch (gpu.getDisplayWidth()) {
            case 256: return 10;
            case 320: return 8;
            case 368: return 7;
            case 512: return 5;
            default:  return 4; // 640
        }
    });
}


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


void Bus::loadRAMBlock(uint32_t address, const uint8_t* data, size_t size) {
    address &= 0x1FFFFFFF;
    for (size_t i = 0; i < size; i++) {
        ram[(address + i) & 0x001FFFFF] = data[i];
    }
}


uint8_t Bus::read8(uint32_t address) {
    /// @todo (bug): `& 0x1FFFFFFF` throws the segment away. psx-spx: the scratchpad is mirrored only in KUSEG/KSEG0 (not KSEG1); KUSEG above 512MB and KSEG2 other than FFFE0130h fault instead of aliasing onto physical memory; the write queue and i-cache apply only to KUSEG/KSEG0. Needs the unmasked address.
    address &= 0x1FFFFFFF; // KSEG masking

    // Main RAM with 8MB Mirroring (0x00000000 - 0x007FFFFF)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + (8 * 1024 * 1024)) {
        return ram[address & 0x001FFFFF];
    }

    // Expansion Region 1 (0x1F000000)
    /// @todo off by one: 1F080000h is past the 512KB DEV0 window (see the isOpenBus todo in Bus.h).
    else if (address >= 0x1F000000 && address <= 0x1F080000) {
        return 0xFF; // No cart plugged in
    }

    // Scratchpad RAM (0x1F800000 - 0x1F8003FF)
    if (address >= 0x1F800000 && address <= 0x1F8003FF) {
        return scratchpad[address & 0x3FF];
    }

    // Hardware Registers: SIO0 (0x1F801040)
    /// @todo SIO0_BR occupies 1F80104Eh-1F80104Fh (iomap.md) but this range stops at 1F80104Eh, so an 8-bit access to 1F80104Fh hits the FATAL.
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        return sio0.read8(address);
    }

    // Hardware Registers: SIO1 (0x1F801050 - 0x1F80105F)
    else if (address >= 0x1F801050 && address <= 0x1F80105F) {
        return sio1.read8(address);
    }

    // Hardware Registers: DMA (0x1F801080 - 0x1F8010FF)
    else if (address >= 0x1F801080 && address <= 0x1F8010FF) {
        return dma.read8(address);
    }

    // Hardware Registers: CD-ROM (0x1F801800)
    else if (address >= Hardware::REG_CDROM_BASE && address <= Hardware::REG_CDROM_BASE + 3) {
        return cdrom.read8(address);
    }

    // Hardware Registers: SPU (0x1F801C00 - 0x1F801FFF), byte reads return half of a halfword
    else if (address >= 0x1F801C00 && address <= 0x1F801FFF) {
        return spu.read8(address);
    }

    // BIOS ROM (0x1FC00000)
    else if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        return bios[address - Hardware::BIOS_STARTING_ADDRESS];
    }
    
    if (isOpenBus(address)) {
        return 0xFF;
    }

    /// @todo psx-spx: unused regions (including gaps in the I/O region) raise a Bus Error (DBE, ExcCode 07h); this prints and exit(1)s. Switch once crash-loud debugging is no longer needed (the CPU needs a way to receive the fault, see the CPU DBE todo). Per iomap.md the I/O gaps are 1F801024h-1F80103Fh, 1F801064h-1F80106Fh, 1F801078h-1F80107Fh, 1F801130h-1F8017FFh, 1F801804h-1F80180Fh, 1F801818h-1F80181Fh and 1F801828h-1F801BFFh (plus 1F8010F8h/FCh, listed as unknown).
    std::cout << "FATAL: Unhandled read8 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
    exit(1);
}


void Bus::write8(uint32_t address, uint8_t value) {
    /// @todo (bug): segment info is lost here, see the segment masking todo in Bus::read8.
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
    /// @todo SIO0_BR occupies 1F80104Eh-1F80104Fh (iomap.md) but this range stops at 1F80104Eh, so an 8-bit access to 1F80104Fh hits the FATAL.
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        sio0.write8(address, value);
    }

    // Hardware Registers: SIO1 (0x1F801050 - 0x1F80105F)
    else if (address >= 0x1F801050 && address <= 0x1F80105F) {
        sio1.write8(address, value);
    }

    // Hardware Registers: DMA (0x1F801080 - 0x1F8010FF)
    else if (address >= 0x1F801080 && address <= 0x1F8010FF) {
        dma.write8(address, value);
    }

    // Hardware Registers: CD-ROM (0x1F801800)
    else if (address >= Hardware::REG_CDROM_BASE && address <= Hardware::REG_CDROM_BASE + 3) {
        cdrom.write8(address, value);
    }

    // Hardware Registers: SPU (0x1F801C00 - 0x1F801FFF), a byte write lands on the whole halfword (pcsx-redux spu-wide-writes)
    else if (address >= 0x1F801C00 && address <= 0x1F801FFF) {
        spu.write8(address, value);
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
        //std::cout << " (0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)value << std::dec << std::nouppercase << ")" << " [frame " << totalFrames << "]" << std::endl;
	}
    else if (isIgnoredWrite(address)) {
        // The hardware ignores these writes
    }
    else {
        /// @todo psx-spx: unmapped regions raise a Bus Error (DBE), see the todo in Bus::read8.
        std::cout << "FATAL: Unhandled write8 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
        exit(1);
    }
}


uint16_t Bus::read16(uint32_t address) {
    /// @todo (bug): segment info is lost here, see the segment masking todo in Bus::read8.
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

    // Hardware Registers: SIO1 (0x1F801050 - 0x1F80105F)
    else if (address >= 0x1F801050 && address <= 0x1F80105F) {
        return sio1.read16(address);
    }

    // Hardware Registers: Interrupts (0x1F801070)
    /// @todo (bug): no DMA route in read16/write16, so any 16-bit access to a DMA register hits the FATAL below. psx-spx (CPU spec): 8/16-bit stores to 32-bit I/O registers behave like a 32-bit store of the full GPR value (soundscope uses `sh` on DMA registers and hangs without it); see the CPU 8/16-bit store todos.
    else if (address == Hardware::REG_INTERRUPT_STATUS) {
        return interruptStatus;
    }
    else if (address == Hardware::REG_INTERRUPT_MASK) {
        return interruptMask;
    }

    // Hardware Registers: Timers (0x1F801100)
    else if (address >= 0x1F801100 && address < 0x1F801130) {
        return timers.read(address);
    }

    // Hardware Registers: SPU (0x1F801C00)
    else if (address >= 0x1F801C00 && address <= 0x1F801FFF) {
       return spu.read16(address);
    }

    // BIOS ROM (0x1FC00000)
    else if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        uint32_t offset = address - Hardware::BIOS_STARTING_ADDRESS;
        return bios[offset] | (bios[offset + 1] << 8);
    }

    if (isOpenBus(address)) {
        return 0xFFFF;
    }

    /// @todo psx-spx: unmapped regions raise a Bus Error (DBE), see the todo in Bus::read8.
    std::cout << "FATAL: Unhandled read16 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
    exit(1);
}


void Bus::write16(uint32_t address, uint16_t value) {
    /// @todo (bug): segment info is lost here, see the segment masking todo in Bus::read8.
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

    // Hardware Registers: SIO1 (0x1F801050 - 0x1F80105F)
    else if (address >= 0x1F801050 && address <= 0x1F80105F) {
        sio1.write16(address, value);
    }

    // Hardware Registers: Interrupts (0x1F801070)
    /// @todo (bug): no DMA route in read16/write16, so any 16-bit access to a DMA register hits the FATAL below. psx-spx (CPU spec): 8/16-bit stores to 32-bit I/O registers behave like a 32-bit store of the full GPR value (soundscope uses `sh` on DMA registers and hangs without it); see the CPU 8/16-bit store todos.
    else if (address == Hardware::REG_INTERRUPT_STATUS) {
        interruptStatus &= value;
    }
    else if (address == Hardware::REG_INTERRUPT_MASK) {
        interruptMask = value;
    }

    // Hardware Registers: Timers (0x1F801100)
    else if (address >= 0x1F801100 && address < 0x1F801130) {
        timers.write(address, value);
    }

    // Hardware Registers: SPU (0x1F801C00)
    else if (address >= 0x1F801C00 && address <= 0x1F801FFF) {
        spu.write16(address, value);
    }
    else if (isIgnoredWrite(address)) {
        // The hardware ignores these writes
    }
    else {
        /// @todo psx-spx: unmapped regions raise a Bus Error (DBE), see the todo in Bus::read8.
        std::cout << "FATAL: Unhandled write16 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
        exit(1);
    }
}


uint32_t Bus::read32(uint32_t address) {
    /// @todo (bug): segment info is lost here, see the segment masking todo in Bus::read8.
    address &= 0x1FFFFFFF; // KSEG masking

    // Main RAM first: nearly every instruction fetch and data access lands here, and this range overlaps none of the others
    if (address < 8 * 1024 * 1024) {
        uint32_t value;
        std::memcpy(&value, &ram[address & 0x001FFFFF], sizeof(value)); // little-endian host, same as the byte-by-byte assembly
        return value;
    }

    /// @todo (bug): BCC (FFFE0130h) is raw storage, 32-bit only. psx-spx: bits 6 and 10 always read 0; TAG(2)/INV(1)/LOCK(0)/IS1(11) steer isolated-cache stores (see the CPU.cpp isolate todo); IBLKSZ (8-9) sets the i-cache refill burst; RAM(3)/DS(7) control the scratchpad mapping (clearing bit 3 unmaps it: loads update it but never hit).
    if (address == 0x1FFE0130) return cacheControlRegister;

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
    /// @todo memory control regs (1F801000h-1F801020h) are raw storage and 32-bit. psx-spx: 1F801000h/04h bits 24-31 are fixed 1Fh; Delay/Size bits 21-23 are always 0 and bit 28 is a write-1-to-clear address-error flag; COM_DELAY bits 16-31 read 0. The base, size, width and delay fields have no effect (see the timing todo).
	else if (address >= 0x1F801000 && address <= 0x1F801020) {
		return memoryControlRegisters[(address - 0x1F801000) >> 2];
	}

    // Hardware Registers: SIO0 (0x1F801040)
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        return sio0.read32(address);
    }

    // Hardware Registers: SIO1 (Serial Port)
    else if (address >= 0x1F801050 && address <= 0x1F80105F) {
        return sio1.read32(address);
    }

    // Hardware Registers: Interrupts (0x1F801070)
    /// @todo psx-spx: I_STAT/I_MASK bits 11-15 read as zero and 16-31 are garbage; the raw stored values are returned.
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
    else if (address >= 0x1F801100 && address < 0x1F801130) {
        return timers.read(address);
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
		return mdec.readData();
	}

    // SPU
    else if(address >= 0x1F801C00 && address <= 0x1F801FFF) {
        return spu.read32(address);
    }


    // BIOS ROM (0x1FC00000)
    else if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        uint32_t offset = address - Hardware::BIOS_STARTING_ADDRESS;
        return bios[offset] | (bios[offset + 1] << 8) | (bios[offset + 2] << 16) | (bios[offset + 3] << 24);
    }

    if (isOpenBus(address)) {
        return 0xFFFFFFFF;
    }

    /// @todo psx-spx: unmapped regions raise a Bus Error (DBE), see the todo in Bus::read8.
    std::cout << "FATAL: Unhandled read32 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
    exit(1);
}


void Bus::write32(uint32_t address, uint32_t value) {
    /// @todo (bug): segment info is lost here, see the segment masking todo in Bus::read8.
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
    /// @todo memory control regs (1F801000h-1F801020h) are raw storage and 32-bit. psx-spx: 1F801000h/04h bits 24-31 are fixed 1Fh; Delay/Size bits 21-23 are always 0 and bit 28 is a write-1-to-clear address-error flag; COM_DELAY bits 16-31 read 0. The base, size, width and delay fields have no effect (see the timing todo).
    else if (address >= 0x1F801000 && address <= 0x1F801020) {
        memoryControlRegisters[(address - 0x1F801000) >> 2] = value;
    }

    // Hardware Registers: SIO0 (0x1F801040)
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        sio0.write32(address, value);
    }

    // Hardware Registers: SIO1 (Serial Port)
    else if (address >= 0x1F801050 && address <= 0x1F80105F) {
        sio1.write32(address, value);
    }

    // RAM Size Configuration (0x1F801060)
    /// @todo (bug): 1F801060h (DRAM_CTRL/RAM_SIZE) is write-ignored and has NO read path, so a read hits the FATAL below (16-bit accesses also hit FATAL). psx-spx: R/W; bits 8-11 set the /RAS0 and /RAS1 bank sizes (1/2/4/8MB), bit 10 enables bank 1, and unmapped areas fault instead of mirroring. The 2MB-in-8MB mirrors exist only because the BIOS programs an 8MB bank (Wipeout shrinks it with SetMem). Bits 0-2 and 7 affect DMA/RAM timing.
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
    else if (address >= 0x1F801100 && address < 0x1F801130) {
        timers.write(address, value);
    }

    // Hardware Registers: GPU (0x1F801810)
    else if (address == Hardware::REG_GPU_GP0) {
        gpu.writeGP0(value);
    }
    else if (address == Hardware::REG_GPU_GP1) {
        uint32_t gp1Command = value >> 24;
        if (gp1Command == 0x00) palMode = false;                    // GP1(00h) reset puts the display mode back to NTSC
        else if (gp1Command == 0x08) palMode = (value & 0x08) != 0; // GP1(08h) bit 3: video mode
        gpu.writeGP1(value);
        timers.notifyVideoStateMayHaveChanged(); // display width / video mode may have changed
    }

    // Hardware Registers: MDEC (Video Decoder)
    else if (address == 0x1F801824) {
		mdec.writeControl(value);
	}
	else if (address == 0x1F801820) {
		mdec.writeCommand(value);
	}

    // SPU
    else if(address >= 0x1F801C00 && address <= 0x1F801FFF) {
        spu.write32(address, value);
    }

    // Cache Control (0x1FFE0130)
    /// @todo (bug): BCC (FFFE0130h) is raw storage, 32-bit only. psx-spx: bits 6 and 10 always read 0; TAG(2)/INV(1)/LOCK(0)/IS1(11) steer isolated-cache stores (see the CPU.cpp isolate todo); IBLKSZ (8-9) sets the i-cache refill burst; RAM(3)/DS(7) control the scratchpad mapping (clearing bit 3 unmaps it: loads update it but never hit).
    else if (address == 0x1FFE0130) {
        cacheControlRegister = value; // not modelled, but it reads back what was written
    }
    else if (isIgnoredWrite(address)) {
        // The hardware ignores these writes
    }
    else {
        /// @todo psx-spx: unmapped regions raise a Bus Error (DBE), see the todo in Bus::read8.
        std::cout << "FATAL: Unhandled write32 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
        exit(1);
    }
}


void Bus::advanceClock(int cycles) {
    // Feed cycles to the CD-ROM state machine
    cdrom.advanceClock(cycles);

    dma.advanceClock(cycles);

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

    gpu.advanceClock(cycles);

    for (int i = 0; i < cycles; i++) {
        bool currentSio0Int = sio0.advanceClock();
        
        if (currentSio0Int && !lastSio0Int) {
            interruptStatus |= Hardware::IRQ_SIO0;
        }

        lastSio0Int = currentSio0Int;
    }

    sio1.advanceClock(cycles);
    
    if (sio1.consumeInterruptRequest()) {
        interruptStatus |= Hardware::IRQ_SIO1;
    }

    timers.advanceClock(cycles);
    spu.advanceClock(cycles);
};

