#pragma once
#include <cstdint>
#include <string>
#include <vector>

using namespace std;

class Bus {
public:
    Bus();
    ~Bus();

    bool loadBIOS(const string& filepath);

    uint8_t read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);
    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);

private:
    // Physical hardware chips represented as byte arrays
    vector<uint8_t> ram;
    vector<uint8_t> bios;

    // Interrupt Registers
    uint32_t interruptStatus = 0;
    uint32_t interruptMask = 0;

};
