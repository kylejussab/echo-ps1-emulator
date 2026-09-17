#pragma once
#include <cstdint>
#include "Bus.h"

/// @brief Emulates the MIPS R3000A CPU, managing instruction execution, the delay slot pipeline, and Coprocessor state.
class CPU {
public:
    /// @brief Initializes the CPU and links it to the main memory bus.
    /// @param bus Pointer to the main Bus for memory and peripheral access.
    CPU(Bus* bus);
    
    
    /// @brief Executes a single CPU instruction cycle, including load delays and pipelining.
    void step(); // The main execution cycle


    /// @brief Gets the exact memory address of the instruction currently being executed.
    /// @return The 32-bit program counter.
    uint32_t getProgramCounter() const { return currentProgramCounter; }


    /// @brief Flags the CPU to handle a hardware interrupt on the next execution cycle.
    void requestInterrupt() { interruptPending = true; }



    // DEBUG
    uint64_t instructionCount = 0; // Temporary tracker

private:
    Bus* bus;

    // Core CPU State
    uint32_t programCounter = 0; // Current execution address
    uint32_t nextProgramCounter = 0; // Used for pipelining
    uint32_t currentProgramCounter = 0;     

    uint32_t registers[32] = {0}; // General Purpose Registers

    uint32_t hi = 0;
    uint32_t lo = 0;
    uint32_t lastWrittenRegister = 0xFFFFFFFF; // Sentinel: no register written this instruction


    // Pipeline & Load Delay State

    // These ensure each LW is followed by a NOP
    uint32_t pendingLoadRegister = 0;
    uint32_t pendingLoadValue = 0;

    bool isDelaySlot = false;
    bool nextIsDelaySlot = false;
    bool interruptPending = false;


    // Instruction Cache (I-Cache)
    struct CacheEntry {
        uint32_t tag = 0;
        uint32_t data = 0;
        bool valid = false;
    };

    // 4KB Cache / 4 bytes per word = 1024 entries
    CacheEntry iCache[1024];
    bool isInstructionCacheIsolated() const;


    // Coprocessors

    // Coprocessor 0 (System Control)
    uint32_t coprocessor0Registers[32] = {0};

    // Coprocessor 2 (GTE) Registers
    uint32_t coprocessor2DataRegisters[32] = {0};
    uint32_t coprocessor2ControlRegisters[32] = {0};
    
    
    // Internal Execution Engine
    void execute(uint32_t instruction);

    void setRegister(uint32_t index, uint32_t value);
    uint32_t getRegister(uint32_t index) const;

    void triggerException(uint32_t cause);
    void triggerHardwareInterrupt();
    
    /// @brief Extends a 16-bit signed integer to a 32-bit signed integer.
    int32_t signExtend16(uint32_t value) const { return static_cast<int32_t>(static_cast<int16_t>(value)); }
};