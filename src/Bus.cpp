#include "Bus.h"
#include <iostream>
#include <fstream>
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

uint32_t Bus::read32(uint32_t address) {
    address &= 0x1FFFFFFF; // KSEG masking
    
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

void Bus::write16(uint32_t address, uint16_t value) {
    address &= 0x1FFFFFFF; // KSEG masking
    
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

    // TODO: Implement these, MCRs configure memory timings, delay sizes and so on
    // CCR lets the CPU know how to handled caching
    // Memory Control Registers
    if (address >= 0x1F801000 && address <= 0x1F801060) {
        // Acknowledged, but ignoring memory timings for now
        return;
    }

    // Cache Control Register
    if (address == 0x1FFE0130) {
        // Acknowledged, ignoring cache config for now
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