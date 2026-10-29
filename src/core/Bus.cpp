#include <cstring>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include "Bus.h"

namespace {
    // Reads from nothing-is-plugged-in regions return all ones (open bus). Games and tests probe these.
    bool isOpenBus(uint32_t address) {
        return (address >= 0x1F000000 && address < 0x1F800000)   // Expansion region 1 (no cartridge)
            || (address >= 0x1F802000 && address < 0x1F803000)   // Expansion region 2
            || (address >= 0x1FA00000 && address < 0x1FC00000);  // Expansion region 3
        // NOTE: real expansion 3 reads back some bytes you wrote (see ps1-tests cpu/io-access-bitwidth); all-ones is good enough for now
    }

    // Writes the hardware really does ignore.
    bool isIgnoredWrite(uint32_t address) {
        return isOpenBus(address)
            || (address >= 0x1FC00000 && address < 0x1FC00000 + 512 * 1024); // BIOS ROM
    }
}


Bus::Bus() : dma(this) {
    ram.resize(Hardware::RAM_SIZE, 0);
    bios.resize(Hardware::BIOS_SIZE, 0);

    timers.setInterruptCallback([this](uint16_t interruptBit) { interruptStatus |= interruptBit; });
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


void Bus::loadRAMBlock(uint32_t address, const uint8_t* data, size_t size) {
    address &= 0x1FFFFFFF;
    for (size_t i = 0; i < size; i++) {
        ram[(address + i) & 0x001FFFFF] = data[i];
    }
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

    // Hardware Registers: SPU (0x1F801C00 - 0x1F801FFF), byte reads return half of a halfword
    else if (address >= 0x1F801C00 && address <= 0x1F801FFF) {
        return spu.read8(address);
    }

    // BIOS ROM (0x1FC00000)
    else if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        return bios[address - Hardware::BIOS_STARTING_ADDRESS];
    }

    if (address >= 0x1F801050 && address < 0x1F801060) return static_cast<uint8_t>(sio1Read(address, 1));
    
    if (isOpenBus(address)) {
        return 0xFF;
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

    if (address >= 0x1F801050 && address < 0x1F801060) return static_cast<uint16_t>(sio1Read(address, 2));

    if (isOpenBus(address)) {
        return 0xFFFF;
    }

    std::cout << "FATAL: Unhandled read16 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
    exit(1);
}


uint32_t Bus::read32(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Main RAM first: nearly every instruction fetch and data access lands here, and this range overlaps none of the others
    if (address < 8 * 1024 * 1024) {
        uint32_t value;
        std::memcpy(&value, &ram[address & 0x001FFFFF], sizeof(value)); // little-endian host, same as the byte-by-byte assembly
        return value;
    }

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

    // Hardware Registers: SIO1 (Memory Card / Serial)
    else if (address >= 0x1F801050 && address <= 0x1F80105E) {
        std::cout << "FATAL: Unimplemented SIO1 access at 0x" << std::hex << address << "\n";
        exit(1);
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

    if (address >= 0x1F801050 && address < 0x1F801060) return static_cast<uint32_t>(sio1Read(address, 4));

    if (isOpenBus(address)) {
        return 0xFFFFFFFF;
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
    else if (address >= 0x1F801050 && address < 0x1F801060) {
        sio1Write(address, value, 1);
    }
    else if (isIgnoredWrite(address)) {
        // The hardware ignores these writes
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
    else if (address >= 0x1F801100 && address < 0x1F801130) {
        timers.write(address, value);
    }

    // Hardware Registers: SPU (0x1F801C00)
    else if (address >= 0x1F801C00 && address <= 0x1F801FFF) {
        spu.write16(address, value);
    }
    else if (address >= 0x1F801050 && address < 0x1F801060) {
        sio1Write(address, value, 2);
    }
    else if (isIgnoredWrite(address)) {
        // The hardware ignores these writes
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

    // Hardware Registers: SIO1 (Memory Card / Serial)
    else if (address >= 0x1F801050 && address <= 0x1F80105E) {
        std::cout << "FATAL: Unimplemented SIO1 access at 0x" << std::hex << address << "\n";
        exit(1);
    }

    // SPU
    else if(address >= 0x1F801C00 && address <= 0x1F801FFF) {
        spu.write32(address, value);
    }

    // Cache Control (0x1FFE0130)
    else if (address == 0x1FFE0130) {
        cacheControlRegister = value; // not modelled, but it reads back what was written
    }
    else if (address >= 0x1F801050 && address < 0x1F801060) {
        sio1Write(address, value, 4);
    }
    else if (isIgnoredWrite(address)) {
        // The hardware ignores these writes
    }
    else {
        std::cout << "FATAL: Unhandled write32 at address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << address << "\n";
        exit(1);
    }
}


uint32_t Bus::sio1Read(uint32_t address, int bytes) const {
    uint32_t offset = address - 0x1F801050;
    uint32_t result = 0;
    for (int i = 0; i < bytes; i++) {
        uint32_t byteOffset = offset + i;
        uint8_t byteValue = sio1Registers[byteOffset & 0xF];
        if (byteOffset >= 4 && byteOffset < 8) byteValue = (0x00000005 >> ((byteOffset - 4) * 8)) & 0xFF; // SIO1_STAT: TX ready, TX empty
        result |= static_cast<uint32_t>(byteValue) << (8 * i);
    }
    return result;
}


void Bus::sio1Write(uint32_t address, uint32_t value, int bytes) {
    uint32_t offset = address - 0x1F801050;
    for (int i = 0; i < bytes; i++) {
        sio1Registers[(offset + i) & 0xF] = (value >> (8 * i)) & 0xFF;
    }
}