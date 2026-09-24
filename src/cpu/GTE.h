#pragma once
#include <cstdint>

class GTE {
public:
    GTE();

    void writeDataRegister(uint32_t index, uint32_t value);
    uint32_t readDataRegister(uint32_t index) const;

    void writeControlRegister(uint32_t index, uint32_t value);
    uint32_t readControlRegister(uint32_t index) const;

    void executeCommand(uint32_t instruction);

private:
    uint32_t dataRegisters[32];
    uint32_t controlRegisters[32];
    
    // Command Implementations
    void commandRTPT(uint32_t instruction);
    void commandNCLIP(uint32_t instruction);
    void commandAVSZ3(uint32_t instruction);
    void commandNCDS(uint32_t instruction);

    static int32_t clampS(int64_t val, int32_t lo, int32_t hi);
    static int32_t clampSFlag(int64_t val, int32_t lo, int32_t hi, uint32_t flagBit, uint32_t& flags);
    static uint32_t divideProjection(uint16_t h, uint16_t sz3, uint32_t& flags);
};
