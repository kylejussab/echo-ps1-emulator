#pragma once
#include <cstdint>
#include "Bus.h"

class CPU {
public:
    CPU(Bus* bus);
    void step(); // The main execution cycle

    uint64_t instructionCount = 0; // Temporary tracker

    uint32_t getProgramCounter() const { return currentProgramCounter; }

    void requestInterrupt();

private:
    Bus* bus;
    uint32_t programCounter;                  // Current execution address
    uint32_t nextProgramCounter;              // Used for pipelining
    uint32_t currentProgramCounter = 0;       
    uint32_t registers[32];                   // General Purpose Registers
    uint32_t coprocessor0Registers[32] = {0}; // Coprocessor0 Registers
    
    struct CacheEntry {
        uint32_t tag = 0;
        uint32_t data = 0;
        bool valid = false;
    };
    
    // 4KB Cache / 4 bytes per word = 1024 entries
    CacheEntry iCache[1024];

    // These ensure each LW is followed by a NOP
    uint32_t pendingLoadRegister = 0;
    uint32_t pendingLoadValue = 0;

    uint32_t hi = 0;
    uint32_t lo = 0;
    uint32_t lastWrittenRegister = 0xFFFFFFFF; // sentinel: no register written this instruction

    bool interruptPending = false;

    bool isInstructionCacheIsolated() const;

    int32_t signExtend16(uint32_t value) const {
        return static_cast<int32_t>(static_cast<int16_t>(value));
    }

    void execute(uint32_t instruction);
    void triggerException(uint32_t cause);    // Exception Engine
    void triggerHardwareInterrupt();

    void setRegister(uint32_t index, uint32_t value);
    uint32_t getRegister(uint32_t index) const;

    bool isDelaySlot = false;
    bool nextIsDelaySlot = false;
};