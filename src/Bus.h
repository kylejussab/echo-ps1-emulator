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

    uint32_t read32(uint32_t address);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);

private:
    // Physical hardware chips represented as byte arrays
    vector<uint8_t> ram;
    vector<uint8_t> bios;
};
