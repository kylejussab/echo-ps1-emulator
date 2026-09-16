#include "Bus.h"
#include <iostream>
#include <fstream>
#include <iomanip>

Bus::Bus() {
    ram.resize(Hardware::RAM_SIZE, 0);
    bios.resize(Hardware::BIOS_SIZE, 0);
}

Bus::~Bus() {}


bool Bus::loadBIOS(const string& filepath) {
    ifstream file(filepath, ios::binary);
    if (!file.is_open()) {
        cerr << "BIOS file couldn't be opened at " << filepath << endl;
        return false;
    }
    file.read(reinterpret_cast<char*>(bios.data()), bios.size());
    file.close();
    return true;
}


uint8_t Bus::read8(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        return ram[address - Hardware::RAM_STARTING_ADDRESS];
    }
    if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        return bios[address - Hardware::BIOS_STARTING_ADDRESS];
    }
    if (address >= 0x1F000000 && address <= 0x1F080000) {
        return 0xFF; // Expansion Region 1 (No cart plugged in)
    }

    // CD-ROM Controller Stubs (Tell the BIOS the drive is empty)
    if (address >= 0x1F801800 && address <= 0x1F801803) {
        return 0x00;
    }

    cout << "Unhandled read8 at address: 0x" << hex << address << endl;
    return 0xFF;
}


uint16_t Bus::read16(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Joypad / Memory Card Status Stub
    if (address == 0x1F801044) {
        return 0x0005; 
    }

    // Main Memory
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;
        return ram[offset] | (ram[offset + 1] << 8);
    }
    if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        uint32_t offset = address - Hardware::BIOS_STARTING_ADDRESS;
        return bios[offset] | (bios[offset + 1] << 8);
    }

    // Hardware Registers
    if (address == Hardware::REG_INTERRUPT_STATUS) return interruptStatus;
    if (address == Hardware::REG_INTERRUPT_MASK) return interruptMask;
    
    if (address >= 0x1F801100 && address <= 0x1F801128) {
        if (address == 0x1F801100) return timer0;
        if (address == 0x1F801110) return timer1;
        if (address == 0x1F801120) return timer2;
        return 0; 
    }
    if (address >= 0x1F801C00 && address <= 0x1F801DFF) return 0x0000; // SPU idle

    cout << "Unhandled read16 at address: 0x" << hex << address << endl;
    return 0xFFFF;
}


uint32_t Bus::read32(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Joypad / Memory Card Status Stub
    if (address == 0x1F801044) {
        return 0x00000005; // TX Ready (Bit 0) | TX Empty (Bit 2)
    }

    // Main Memory
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;
        return ram[offset] | (ram[offset + 1] << 8) | (ram[offset + 2] << 16) | (ram[offset + 3] << 24);
    }
    if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        uint32_t offset = address - Hardware::BIOS_STARTING_ADDRESS;
        return bios[offset] | (bios[offset + 1] << 8) | (bios[offset + 2] << 16) | (bios[offset + 3] << 24);
    }

    // Hardware Registers
    if (address == Hardware::REG_INTERRUPT_STATUS) return interruptStatus;
    if (address == Hardware::REG_INTERRUPT_MASK) return interruptMask;
    
    if (address == Hardware::REG_GPU_GP0) return gpu.readGP0();
    if (address == Hardware::REG_GPU_GP1) return gpu.readGP1();
    
    if (address == Hardware::REG_DMA_DPCR) return DMAControlRegister;
    if (address == Hardware::REG_DMA_DICR) return DMAInterruptControlRegister;
    
    // Return real values for GPU DMA (Channel 2)
    if (address == 0x1F8010A0) return DMAChannel2MemoryAddress;
    if (address == 0x1F8010A4) return DMAChannel2BlockControl;
    if (address == 0x1F8010A8) return DMAChannel2ChannelControl;

    // Return real values for OTC DMA (Channel 6)
    if (address == 0x1F8010E0) return DMAChannel6MemoryAddress;
    if (address == 0x1F8010E4) return DMAChannel6BlockControl;
    if (address == 0x1F8010E8) return DMAChannel6ChannelControl;

    // Fake remaining unhandled DMA channels as idle
    if (address >= 0x1F801080 && address <= 0x1F8010E8) return 0x00000000; 


    if (address >= 0x1F801100 && address <= 0x1F801128) {
        if (address == 0x1F801100) return timer0;
        if (address == 0x1F801110) return timer1;
        if (address == 0x1F801120) return timer2;
        return 0;
    }

    cout << "Unhandled read32 at address: 0x" << hex << address << endl;
    return 0xFFFFFFFF;
}


void Bus::write8(uint32_t address, uint8_t value) {
    address &= 0x1FFFFFFF; // KSEG masking

    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        ram[address - Hardware::RAM_STARTING_ADDRESS] = value;
        return;
    }

    if (address == 0x1F802041) { // POST Register
        cout << "BIOS: " << hex;
        switch (value) {
            case 0x00: cout << "Booting Shell"; break;
            case 0x01: cout << "CPU & ROM Check"; break;
            case 0x02: cout << "RAM Setup"; break;
            case 0x03: cout << "Initial RAM Test & Clear"; break;
            case 0x04: cout << "Installing Device Drivers & Interrupt Handlers"; break;
            case 0x05: cout << "Initializing Serial Comm & Controllers"; break;
            case 0x06: cout << "Initializing CD-ROM & Timers"; break;
            case 0x07: cout << "Configuring Machine Environment & DMA"; break;
            case 0x0E: cout << "Checking Expansion ROM (EXP1)"; break;
            case 0x0F: cout << "Initalizing Coprocessor 0"; break;
            default:   cout << "Unknown POST Code"; break;
        }
        cout << " (0x" << hex << uppercase << setfill('0') << setw(2) << (int)value << dec << nouppercase << ")" << endl;
        return;
    }

    // CD-ROM Controller Stubs (Quietly absorb setup commands)
    if (address >= 0x1F801800 && address <= 0x1F801803) {
        return;
    }

    cout << "Unhandled write8 at address: 0x" << hex << address << endl;
}


void Bus::write16(uint32_t address, uint16_t value) {
    address &= 0x1FFFFFFF; // KSEG masking

    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;
        ram[offset + 0] = value & 0xFF;
        ram[offset + 1] = (value >> 8) & 0xFF;
        return;
    }

    if (address == Hardware::REG_INTERRUPT_STATUS) {
        interruptStatus &= value; // Acknowledge
        return;
    }
    if (address == Hardware::REG_INTERRUPT_MASK) {
        interruptMask = value;
        return;
    }

    if (address >= 0x1F801100 && address <= 0x1F801128) return; // Ignore timer setup
    if (address >= 0x1F801C00 && address <= 0x1F801DFF) return; // Ignore audio setup

    cout << "Unhandled write16 at address: 0x" << hex << address << endl;
}


void Bus::write32(uint32_t address, uint32_t value) {
    address &= 0x1FFFFFFF; // KSEG masking

    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;
        ram[offset + 0] = value & 0xFF;
        ram[offset + 1] = (value >> 8) & 0xFF;
        ram[offset + 2] = (value >> 16) & 0xFF;
        ram[offset + 3] = (value >> 24) & 0xFF;
        return;
    }

    // DMA & Interrupt Hardware Control
    if (address >= 0x1F801000 && address <= 0x1F8010FF) {
        switch (address) {
            case Hardware::REG_INTERRUPT_STATUS: interruptStatus &= value; return;
            case Hardware::REG_INTERRUPT_MASK: interruptMask = value; return;
            
            // DMA Channel 2 (GPU)
            case 0x1F8010A0: DMAChannel2MemoryAddress = value; return;
            case 0x1F8010A4: DMAChannel2BlockControl = value; return;
            case 0x1F8010A8: {
                DMAChannel2ChannelControl = value;
                if ((value & 0x01000000) != 0) {
                    performGPUDMATransfer((value & 0x00000001) == 0);
                    DMAChannel2ChannelControl &= ~0x01000001;
                }
                return;
            }
            // DMA Channel 6 (OTC)
            case 0x1F8010E0: DMAChannel6MemoryAddress = value; return;
            case 0x1F8010E4: DMAChannel6BlockControl = value; return;
            case 0x1F8010E8: {
                DMAChannel6ChannelControl = value;
                if ((value & 0x01000000) != 0) {
                    performOTCDMATransfer();
                    DMAChannel6ChannelControl &= ~0x01000000;
                }
                return;
            }
            case Hardware::REG_DMA_DPCR: DMAControlRegister = value; return;
            case Hardware::REG_DMA_DICR: {
                // Writing 1 to a flag bit (24-30) clears it
                uint32_t flagsToClear = value & 0x7F000000;
                DMAInterruptControlRegister &= ~flagsToClear;
                
                // Update the lower bits (Enables, Master Enable, etc.)
                DMAInterruptControlRegister = (DMAInterruptControlRegister & 0xFF000000) | (value & 0x00FFFFFF);

                // Re-evaluate the Master Flag (Bit 31)
                bool masterEnable = (DMAInterruptControlRegister & (1 << 23)) != 0;
                uint32_t enables = (DMAInterruptControlRegister >> 16) & 0x7F;
                uint32_t flags = (DMAInterruptControlRegister >> 24) & 0x7F;
                
                if (masterEnable && (enables & flags) > 0) {
                    DMAInterruptControlRegister |= (1 << 31);
                } else {
                    DMAInterruptControlRegister &= ~(1 << 31);
                }
                return;
            }
            default: return; // Safely ignore other DMA/Control registers for now
        }
    }

    if (address == Hardware::REG_GPU_GP0) { gpu.writeGP0(value); return; }
    if (address == Hardware::REG_GPU_GP1) { gpu.writeGP1(value); return; }

    if (address >= 0x1F801100 && address <= 0x1F801128) return; // Ignore timer setup
    if (address == 0x1FFE0130) return; // Ignore Cache Control

    cout << "Unhandled write32 at address: 0x" << hex << address << endl;
}



void Bus::performGPUDMATransfer(bool directionToRAM) {
    uint8_t syncMode = (DMAChannel2ChannelControl >> 9) & 0x03;
    uint32_t blockSize = DMAChannel2BlockControl & 0xFFFF;
    uint32_t blockCount = (DMAChannel2BlockControl >> 16) & 0xFFFF;
    
    if (blockSize == 0) blockSize = 0x10000;

    uint32_t totalWords = (syncMode == 0) ? blockSize : (blockSize * blockCount);

    if (syncMode == 2) {
        uint32_t currentAddress = DMAChannel2MemoryAddress & 0x001FFFFF; 
        while (true) {
            uint32_t header = read32(currentAddress);
            uint8_t wordCount = header >> 24;
            uint32_t nextAddress = header & 0x001FFFFF; 

            uint32_t payloadAddress = currentAddress + 4;
            for (uint8_t i = 0; i < wordCount; i++) {
                gpu.writeGP0(read32(payloadAddress));
                payloadAddress += 4;
            }
            if ((header & 0x00FFFFFF) == 0x00FFFFFF) break;
            if (nextAddress == 0x000000) break;
            if (nextAddress == currentAddress) break;
            
            currentAddress = nextAddress;
        }
        setDMAInterruptFlag(2);
        return; 
    }

    uint32_t currentAddress = DMAChannel2MemoryAddress;
    for (uint32_t wordIndex = 0; wordIndex < totalWords; wordIndex++) {
        if (directionToRAM) {
            write32(currentAddress, gpu.readGP0());
        } else {
            gpu.writeGP0(read32(currentAddress));
        }
        currentAddress += 4;
    }

    setDMAInterruptFlag(2);
}


void Bus::performOTCDMATransfer() {
    uint32_t currentAddress = DMAChannel6MemoryAddress & 0x1FFFFFFF;
    uint32_t blockCount = DMAChannel6BlockControl & 0xFFFF;

    if (blockCount == 0) blockCount = 0x10000;

    for (uint32_t i = 0; i < blockCount; i++) {
        uint32_t nextAddress = (i == blockCount - 1) ? 0x00FFFFFF : (currentAddress - 4);
        write32(currentAddress, nextAddress);
        currentAddress -= 4;
    }

    setDMAInterruptFlag(6);
}


void Bus::setDMAInterruptFlag(uint8_t channel) {
    uint32_t flagBit = 1 << (24 + channel);
    
    // Raise the channel's completion flag
    DMAInterruptControlRegister |= flagBit;

    // Check if the Master Interrupt Enable (Bit 23) is turned on
    bool masterEnable = (DMAInterruptControlRegister & (1 << 23)) != 0;
    
    // Check if any raised flags also have their corresponding Enable bits (16-22) set
    uint32_t enables = (DMAInterruptControlRegister >> 16) & 0x7F;
    uint32_t flags = (DMAInterruptControlRegister >> 24) & 0x7F;

    if (masterEnable && (enables & flags) > 0) {
        DMAInterruptControlRegister |= (1 << 31); // Set Master Flag
        triggerHardwareInterrupt(0x08);           // Fire DMA IRQ (Bit 3 of I_STAT)
    }
}




void Bus::triggerHardwareInterrupt(uint16_t interruptBit) {
    interruptStatus |= interruptBit;
}


void Bus::tickTimers(uint32_t cycles) {
    timer0 += cycles;
    timer1 += cycles;
    timer2 += cycles;
}
