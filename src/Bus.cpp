#include "Bus.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include "Constants.h"

using namespace std;

Bus::Bus() {
    // Initialize the chips to their physical sizes
    ram.resize(Hardware::RAM_SIZE, 0);    // 2MB Main RAM
    bios.resize(Hardware::BIOS_SIZE, 0);  // 512KB BIOS ROM
}

Bus::~Bus() {}

bool Bus::loadBIOS(const string& filepath) {
    ifstream file(filepath, ios::binary);
    
    if (!file.is_open()) {
        cerr << "BIOS file couldnt be opened or found at " << filepath << endl;
        return false;
    }

    file.read(reinterpret_cast<char*>(bios.data()), bios.size());
    file.close();
    
    return true;
}

uint8_t Bus::read8(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    // Expansion Region 1 (Checking for GameShark/Cheat Carts)
    if (address >= 0x1F000000 && address <= 0x1F080000) {
        return 0xFF; // No cart plugged in
    }

    // Route to RAM
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;
        
        return ram[offset];
    }

    // BIOS ROM checking
    if (address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE) {
        uint32_t offset = address - Hardware::BIOS_STARTING_ADDRESS;
        return bios[offset]; 
    }

    cout << "Unhandled read8 at address: 0x" << hex << address << endl;
    return 0xFF; 
}

uint16_t Bus::read16(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking

    // SPU (Sound Processing Unit) Registers
    if (address >= 0x1F801C00 && address <= 0x1F801DFF) {
        // 0x1F801DAE is the SPU Status Register. 
        // Returning 0 tells the CPU that the SPU is idle and ready.
        return 0x0000; 
    }

    // Root Counters (Hardware Timers) - 32-bit (and 16-bit) read
    if (address >= 0x1F801100 && address <= 0x1F801128) {
        // Create a static counter that remembers its value between reads
        static uint32_t fakeTimer = 0;
        fakeTimer += 64; // Increment by a decent chunk so we don't wait forever
        return fakeTimer;
    }

    // Interrupt Status (I_STAT)
    if (address == 0x1F801070) {
        interruptStatus |= 0x0001; // Magically fire VBLANK so the CPU doesn't get stuck
        return interruptStatus;
    }
    // Interrupt Mask (I_MASK)
    if (address == 0x1F801074) {
        return interruptMask;
    }

    // Route to RAM
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;
        
        uint16_t b0 = ram[offset + 0];
        uint16_t b1 = ram[offset + 1];

        return b0 | (b1 << 8); // Little-Endian combine
    }

    // Route to BIOS
    if(address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE){
        uint32_t offset = address - Hardware::BIOS_STARTING_ADDRESS;
        
        uint16_t b0 = bios[offset + 0];
        uint16_t b1 = bios[offset + 1];

        return b0 | (b1 << 8);
    }

    cout << "Unhandled read16 at address: 0x" << hex << address << endl;
    return 0xFFFF; 
}

uint32_t Bus::read32(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking
    
    if (address == 0x1F801070) return interruptStatus;
    if (address == 0x1F801074) return interruptMask;

    // DMA Registers (Channels 0-6 and Global Control)
    if (address >= 0x1F801080 && address <= 0x1F8010F4) {
        // Returning 0 tells the CPU that all DMA channels are idle and any requested transfers instantly completed.
        return 0x00000000;
    }

    // Root Counters (Hardware Timers) - 32-bit (and 16-bit) read
    if (address >= 0x1F801100 && address <= 0x1F801128) {
        // Create a static counter that remembers its value between reads
        static uint32_t fakeTimer = 0;
        fakeTimer += 64; // Increment by a decent chunk so we don't wait forever
        return fakeTimer;
    }

    // GPU Read (GP0) and Status (GP1)
    if (address == 0x1F801814) {
        // Return GPUSTAT ready flags (Bits 26, 27, 28)
        return 0x1C000000; 
    }
    if (address == 0x1F801810) {
        return 0x00000000; // Dummy VRAM read
    }

    // Interrupt Status (I_STAT)
    if (address == 0x1F801070) {
        interruptStatus |= 0x0001; // Magically fire VBLANK so the CPU doesn't get stuck
        return interruptStatus;
    }
    // Interrupt Mask (I_MASK)
    if (address == 0x1F801074) {
        return interruptMask;
    }

    // Route to RAM
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;
        
        uint32_t b0 = ram[offset + 0];
        uint32_t b1 = ram[offset + 1];
        uint32_t b2 = ram[offset + 2];
        uint32_t b3 = ram[offset + 3];

        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }

    // Route to BIOS
    if(address >= Hardware::BIOS_STARTING_ADDRESS && address < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE){
        // Convert the raw hardware address to a basic vector index (0 to 524,287)
        uint32_t offset = address - Hardware::BIOS_STARTING_ADDRESS;
        
        uint32_t b0 = bios[offset + 0];
        uint32_t b1 = bios[offset + 1];
        uint32_t b2 = bios[offset + 2];
        uint32_t b3 = bios[offset + 3];

        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }

    cout << "Unhandled read32 at address: 0x" << hex << address << endl;
    return 0xFFFFFFFF; 
}

void Bus::write8(uint32_t address, uint8_t value) {
    address &= 0x1FFFFFFF; // KSEG masking
    
    // Implement this
    // POST (Power-On Self-Test) diagnostic register
    if (address == 0x1F802041) {
        // Acknowledged
        cout << "BIOS: " << hex;
        switch (value) {
            case 0x01: cout << "CPU & ROM Check"; break;
            case 0x02: cout << "RAM Setup"; break;
            case 0x03: cout << "Initial RAM Test & Clear"; break;
            case 0x04: cout << "Installing Device Drivers & Interrupt Handlers"; break;
            case 0x05: cout << "Initializing Serial Communication Interface & Controllers"; break;
            case 0x06: cout << "Initializing CD-ROM & Timer Event Subsystems"; break;
            case 0x07: cout << "Configuring Machine Environment & Direct Memory Access Setup"; break;
            case 0x0E: cout << "Checking Expansion ROM (EXP1)"; break;
            case 0x0F: cout << "Initalizing Coprocessor 0"; break;
            default:   cout << "NEED TO ADD OUTPUT MESSAGE"; break;
        }
        
        cout << " (0x" << hex << uppercase << setfill('0') << setw(2) << (int)value << dec << nouppercase << ")" << endl;
        return;
    }

    // Route to RAM
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;

        // Drop the single byte directly into the slot
        ram[offset] = value;
        return;
    }

    cout << "Unhandled write8 at address: 0x" << hex << address << endl;
}

void Bus::write16(uint32_t address, uint16_t value) {
    address &= 0x1FFFFFFF; // KSEG masking

    // TODO: Implement this
    // Root Counters (Hardware Timers)
    if (address >= 0x1F801100 && address <= 0x1F801128) {
        // Acknowledged, ignoring timer setup for now
        return;
    }

    // SPU (Sound Processing Unit) configuration
    if (address >= 0x1F801C00 && address <= 0x1F801DFF) {
        // Acknowledged, ignoring audio setup for now
        return;
    }

    // Interrupt Status (I_STAT)
    if (address == 0x1F801070) {
        // CPU writes a 0 to acknowledge and clear the interrupt
        interruptStatus &= value; 
        return;
    }
    // Interrupt Mask (I_MASK)
    if (address == 0x1F801074) {
        interruptMask = value;
        return;
    }

    // Route to RAM
    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;

        // Little-Endian: Least significant byte goes into the lowest address
        ram[offset + 0] = value & 0xFF;
        ram[offset + 1] = (value >> 8) & 0xFF;
        return;
    }

    cout << "Unhandled write16 at address: 0x" << hex << address << endl;
}

void Bus::write32(uint32_t address, uint32_t value){
    address &= 0x1FFFFFFF; // KSEG masking

    // TODO: Implement this
    // Hardware Control Registers
    if (address >= 0x1F801000 && address <= 0x1F8010FF) {
        switch (address) {
            case 0x1F801070:
                // I_STAT: Interrupt Status (Writing usually acknowledges/clears bits)
                // For now, writing 0 or clearing bits:
                interruptStatus &= value; 
                return;
            case 0x1F801074:
                // I_MASK: Interrupt Mask
                interruptMask = value;
                return;
            default:
                // Handle other hardware registers or ignore safely for now
                return;
        }
    }

    // Cache Control Register
    if (address == 0x1FFE0130) {
        // Acknowledged, ignoring cache config for now
        return;
    }

    // GPU Write Commands (GP0 and GP1)
    if (address == 0x1F801810 || address == 0x1F801814) {
        return; // Acknowledged, ignoring drawing commands for now
    }

    // Root Counters (Hardware Timers) - 32-bit access
    if (address >= 0x1F801100 && address <= 0x1F801128) {
        return; // Acknowledged, ignoring timer setup
    }

    // Interrupt Status (I_STAT)
    if (address == 0x1F801070) {
        // CPU writes a 0 to acknowledge and clear the interrupt
        interruptStatus &= value; 
        return;
    }
    // Interrupt Mask (I_MASK)
    if (address == 0x1F801074) {
        interruptMask = value;
        return;
    }

    if (address >= Hardware::RAM_STARTING_ADDRESS && address < Hardware::RAM_STARTING_ADDRESS + Hardware::RAM_SIZE) {
        uint32_t offset = address - Hardware::RAM_STARTING_ADDRESS;

        // Little-Endian: Least significant byte goes into the lowest address
        ram[offset + 0] = value & 0xFF;
        ram[offset + 1] = (value >> 8) & 0xFF;
        ram[offset + 2] = (value >> 16) & 0xFF;
        ram[offset + 3] = (value >> 24) & 0xFF;
        return;
    }

    cout << "Unhandled write32 at address: 0x" << hex << address << endl;
};