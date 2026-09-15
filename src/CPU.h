#pragma once
#include <cstdint>
#include "Bus.h"

class CPU {
public:
    CPU(Bus* bus);
    void step(); // The main execution cycle

    uint64_t instructionCount = 0; // Temporary tracker

private:
    Bus* bus;
    uint32_t programCounter;                  // Current execution address
    uint32_t nextProgramCounter;              // Used for pipelining
    uint32_t registers[32];                   // General Purpose Registers
    uint32_t coprocessor0Registers[32] = {0}; // Coprocessor0 Registers
    
    // These ensure each LW is followed by a NOP
    uint32_t pendingLoadRegister = 0;
    uint32_t pendingLoadValue = 0;

    void execute(uint32_t instruction);
    void setRegister(uint32_t index, uint32_t value);
    uint32_t getRegister(uint32_t index) const;
};