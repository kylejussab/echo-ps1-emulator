#include <iostream>
#include <fstream>
#include <iomanip>
#include "Bus.h"

Bus::Bus() {
    ram.resize(Hardware::RAM_SIZE, 0);
    bios.resize(Hardware::BIOS_SIZE, 0);
}

Bus::~Bus() {}


bool Bus::loadBIOS(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "BIOS file couldn't be opened at " << filepath << std::endl;
        return false;
    }
    file.read(reinterpret_cast<char*>(bios.data()), bios.size());
    file.close();
    return true;
}


uint8_t Bus::read8(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    checkSpinlock(address); // Debug line

    // Main RAM (0x00000000)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        return ram[address - Hardware::RAM_STARTING_ADDRESS];
    }

    // Expansion Region 1 (0x1F000000)
    else if (address >= 0x1F000000 && address <= 0x1F080000) {
        return 0xFF; // No cart plugged in
    }

    // Hardware Registers: SIO0 (0x1F801040)
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        return sio0.read8(address);
    }

    // Hardware Registers: CD-ROM (0x1F801800)
    else if (address >= 0x1F801800 && address <= 0x1F801803) {
        return cdrom.read8(address);
    }

    // BIOS ROM (0x1FC00000)
    else if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        return bios[address - Hardware::BIOS_STARTING_ADDRESS];
    }

    std::cout << "Unhandled read8 at address: 0x" << std::hex << address << std::endl;
    return 0xFF;
}


uint16_t Bus::read16(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    checkSpinlock(address); // Debug line

    // Main RAM (0x00000000)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;
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
        else if (address == 0x1F801120) return timer2;
        return 0; 
    }

    // Hardware Registers: SPU (0x1F801C00)
    else if (address >= 0x1F801C00 && address <= 0x1F801DFF) {
        return 0x0000; // SPU idle stub
    }

    // BIOS ROM (0x1FC00000)
    else if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        uint32_t offset = address - Hardware::BIOS_STARTING_ADDRESS;
        return bios[offset] | (bios[offset + 1] << 8);
    }

    std::cout << "Unhandled read16 at address: 0x" << std::hex << address << std::endl;
    return 0xFFFF;
}


uint32_t Bus::read32(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    checkSpinlock(address); // Debug line

    // Main RAM (0x00000000)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;
        return ram[offset] | (ram[offset + 1] << 8) | (ram[offset + 2] << 16) | (ram[offset + 3] << 24);
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
        if (address == 0x1F8010A0) return DMAChannel2MemoryAddress;
        else if (address == 0x1F8010A4) return DMAChannel2BlockControl;
        else if (address == 0x1F8010A8) return DMAChannel2ChannelControl;
        else if (address == 0x1F8010E0) return DMAChannel6MemoryAddress;
        else if (address == 0x1F8010E4) return DMAChannel6BlockControl;
        else if (address == 0x1F8010E8) return DMAChannel6ChannelControl;
        else if (address == Hardware::REG_DMA_DPCR) return DMAControlRegister;
        else if (address == Hardware::REG_DMA_DICR) return DMAInterruptControlRegister;
        return 0x00000000; // Fake remaining unhandled DMA channels as idle
    }

    // Hardware Registers: Timers (0x1F801100)
    else if (address >= 0x1F801100 && address <= 0x1F801128) {
        if (address == 0x1F801100) return timer0;
        else if (address == 0x1F801110) return timer1;
        else if (address == 0x1F801120) return timer2;
        return 0;
    }

    // Hardware Registers: GPU (0x1F801810)
    else if (address == Hardware::REG_GPU_GP0) {
        return gpu.readGP0();
    }
    else if (address == Hardware::REG_GPU_GP1) {
        return gpu.readGP1();
    }

    // BIOS ROM (0x1FC00000)
    else if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        uint32_t offset = address - Hardware::BIOS_STARTING_ADDRESS;
        return bios[offset] | (bios[offset + 1] << 8) | (bios[offset + 2] << 16) | (bios[offset + 3] << 24);
    }

    std::cout << "Unhandled read32 at address: 0x" << std::hex << address << std::endl;
    return 0xFFFFFFFF;
}


void Bus::write8(uint32_t address, uint8_t value) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Main RAM (0x00000000)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        ram[address - Hardware::RAM_STARTING_ADDRESS] = value;
    }

    // Hardware Registers: SIO0 (0x1F801040)
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        sio0.write8(address, value);
    }

    // Hardware Registers: CD-ROM (0x1F801800)
    else if (address >= 0x1F801800 && address <= 0x1F801803) {
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
            case 0x0E: std::cout << "Checking Expansion ROM (EXP1)"; break;
            case 0x0F: std::cout << "Initializing Coprocessor 0"; break;
            default:   std::cout << "Unknown POST Code"; break;
        }
        std::cout << " (0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)value << std::dec << std::nouppercase << ")" << std::endl;
    }
    else {
        std::cout << "Unhandled write8 at address: 0x" << std::hex << address << std::dec << std::endl;
    }
}


void Bus::write16(uint32_t address, uint16_t value) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Main RAM (0x00000000)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;
        ram[offset + 0] = value & 0xFF;
        ram[offset + 1] = (value >> 8) & 0xFF;
    }

    // Hardware Registers: SIO0 (0x1F801040)
    else if (address >= 0x1F801040 && address <= 0x1F80104E) {
        sio0.write16(address, value);
    }

    // Hardware Registers: Interrupts (0x1F801070)
    else if (address == Hardware::REG_INTERRUPT_STATUS) {
        interruptStatus &= value; // Acknowledge
    }
    else if (address == Hardware::REG_INTERRUPT_MASK) {
        interruptMask = value;
    }

    // Hardware Registers: Timers (0x1F801100)
    else if (address >= 0x1F801100 && address <= 0x1F801128) {
        if (address == 0x1F801120) {
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
        // Ignore audio setup silently
    }
    else {
        std::cout << "Unhandled write16 at address: 0x" << std::hex << address << std::dec << std::endl;
    }
}


void Bus::write32(uint32_t address, uint32_t value) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Main RAM (0x00000000)
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;
        ram[offset + 0] = value & 0xFF;
        ram[offset + 1] = (value >> 8) & 0xFF;
        ram[offset + 2] = (value >> 16) & 0xFF;
        ram[offset + 3] = (value >> 24) & 0xFF;
    }

    // Memory Control (0x1F801000 - 0x1F801020)
    else if (address >= 0x1F801000 && address <= 0x1F801020) {
        // Emulators can safely ignore hardware memory access timings
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
        switch (address) {
            case 0x1F8010A0: DMAChannel2MemoryAddress = value; break;
            case 0x1F8010A4: DMAChannel2BlockControl = value; break;
            case 0x1F8010A8: {
                DMAChannel2ChannelControl = value;
                if ((value & 0x01000000) != 0) {
                    performGPUDMATransfer((value & 0x00000001) == 0);
                    DMAChannel2ChannelControl &= ~0x01000001;
                }
                break;
            }
            case 0x1F8010E0: DMAChannel6MemoryAddress = value; break;
            case 0x1F8010E4: DMAChannel6BlockControl = value; break;
            case 0x1F8010E8: {
                DMAChannel6ChannelControl = value;
                if ((value & 0x01000000) != 0) {
                    performOTCDMATransfer();
                    DMAChannel6ChannelControl &= ~0x01000000;
                }
                break;
            }
            case Hardware::REG_DMA_DPCR: DMAControlRegister = value; break;
            case Hardware::REG_DMA_DICR: {
                uint32_t flagsToClear = value & 0x7F000000;
                DMAInterruptControlRegister &= ~flagsToClear;
                DMAInterruptControlRegister = (DMAInterruptControlRegister & 0xFF000000) | (value & 0x00FFFFFF);
                updateDMAInterruptLine();
                break;
            }
            default: break; // Safely ignore other DMA registers silently
        }
    }

    // Hardware Registers: Timers (0x1F801100)
    else if (address >= 0x1F801100 && address <= 0x1F801128) {
        if (address == 0x1F801120) {
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

    // Cache Control (0x1FFE0130)
    else if (address == 0x1FFE0130) {
        // Ignore Cache Control silently
    }
    else {
        std::cout << "Unhandled write32 at address: 0x" << std::hex << address << std::dec << std::endl;
    }
}


void Bus::tickHardware(int cycles) {
    // CD-ROM
    if (cdrom.checkInterrupt()) { interruptStatus |= Hardware::IRQ_CDROM; } // Trigger IRQ2

    // SIO0 (Controllers & Memory Cards)
    for (int i = 0; i < cycles; i++) {
        if (sio0.tick()) {
            interruptStatus |= 0x0080; // Trigger IRQ7
        }
    }

    // Timers
    // TODO: Implement full hardware logic for Timer 0 (Dot Clock) and Timer 1 (HBlank)
    timer0 += cycles; 
    timer1 += cycles; 

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



void Bus::performGPUDMATransfer(bool directionToRAM) {
    uint8_t syncMode = (DMAChannel2ChannelControl >> 9) & 0x03;
    uint32_t blockSize = DMAChannel2BlockControl & 0xFFFF;
    uint32_t blockCount = (DMAChannel2BlockControl >> 16) & 0xFFFF;
    
    // Hardware quirk: A block size of 0 actually translates to 65,536 words
    if (blockSize == 0) { blockSize = 0x10000; }

    uint32_t totalWords = (syncMode == 0) ? blockSize : (blockSize * blockCount);


    // Linked List Mode (Sync Mode 2)

    if (syncMode == 2) {
        // Linked lists are strictly constrained to the 2MB main RAM
        uint32_t currentAddress = DMAChannel2MemoryAddress & 0x001FFFFF; 

        while (true) {
            uint32_t header = read32(currentAddress);
            uint8_t wordCount = header >> 24;
            uint32_t nextAddress = header & 0x001FFFFF; 

            // Transfer the packet payload (skipping the header) to the GPU
            uint32_t payloadAddress = currentAddress + 4;
            for (uint8_t i = 0; i < wordCount; i++) {
                gpu.writeGP0(read32(payloadAddress));
                payloadAddress += 4;
            }
            
            // Standard hardware termination conditions
            if ((header & 0x00FFFFFF) == 0x00FFFFFF) break; // Official End-of-List marker
            if (nextAddress == 0x000000) break;             // Null pointer fallback
            if (nextAddress == currentAddress) break;       // Self-referential fallback
            
            currentAddress = nextAddress;
        }
        return; 
    }


    // Block Transfer Mode (Sync Modes 0 and 1)

    uint32_t currentAddress = DMAChannel2MemoryAddress;
    
    for (uint32_t wordIndex = 0; wordIndex < totalWords; wordIndex++) {
        if (directionToRAM) {
            write32(currentAddress, gpu.readGP0());
        } else {
            gpu.writeGP0(read32(currentAddress));
        }
        currentAddress += 4; // Advance by one 32-bit word
    }
}


void Bus::performOTCDMATransfer() {
    uint32_t currentAddress = DMAChannel6MemoryAddress & 0x1FFFFFFF;
    uint32_t blockCount = DMAChannel6BlockControl & 0xFFFF;

    // Hardware quirk: A block size of 0 translates to 65,536 words
    if (blockCount == 0) blockCount = 0x10000;

    // The OTC (Ordering Table Clear) channel builds a reversed linked list in RAM
    for (uint32_t i = 0; i < blockCount; i++) {
        uint32_t nextAddress = (i == blockCount - 1) ? 0x00FFFFFF : (currentAddress - 4);
        write32(currentAddress, nextAddress);
        currentAddress -= 4;
    }

    setDMAInterruptFlag(6);
}


void Bus::setDMAInterruptFlag(uint8_t channel) {
    DMAInterruptControlRegister |= (1 << (24 + channel));

    updateDMAInterruptLine();
}


void Bus::updateDMAInterruptLine() {
    bool     masterEnable = (DMAInterruptControlRegister & 0x00800000) != 0; // Bit 23
    uint32_t enables = (DMAInterruptControlRegister >> 16) & 0x7F;
    uint32_t flags = (DMAInterruptControlRegister >> 24) & 0x7F;

    // If the master enable is set and any unmasked flag is active, pull the IRQ3 line high
    if (masterEnable && (enables & flags) > 0) {
        DMAInterruptControlRegister |= 0x80000000; // Set Master IRQ Flag (Bit 31)
        interruptStatus |= 0x08;                   // Trigger IRQ3 (DMA) on the main CPU
    } else {
        DMAInterruptControlRegister &= ~0x80000000;
        interruptStatus &= ~0x08;
    }
}



// Debug

void Bus::checkSpinlock(uint32_t address) {
    if (address >= 0x1F801000 && address <= 0x1F802FFF) {
        static uint32_t lastReadA = 0, lastReadB = 0;
        static int spinCount = 0;
        
        if (address == lastReadA || address == lastReadB) {
            if (++spinCount == 100000) {
                std::cout << "SPINLOCK DETECTED: Alternating 0x" << std::hex << lastReadA << " and 0x" << lastReadB << std::dec << std::endl;
                spinCount = 0; // Reset so we don't spam the terminal to death
            }
        } else {
            lastReadB = lastReadA;
            lastReadA = address;
            spinCount = 0;
        }
    }
}