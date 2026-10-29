#pragma once

#include <cstdint>
#include "GTE.h"
#include "../core/Bus.h"

/// @brief Emulates the MIPS R3000A CPU, managing instruction execution, the delay slot pipeline, and Coprocessor state.
class CPU {
public:
    /// @brief Initializes the CPU and links it to the main memory bus.
    /// @param bus Pointer to the main Bus for memory and peripheral access.
    CPU(Bus* bus);
    
    
    /// @brief Executes a single CPU instruction cycle, including load delays and pipelining.
    void step();


    /// @brief Reads a general purpose register ($0 always reads as zero).
    /// @param index Register index (0-31).
    /// @return The 32-bit register value.
    uint32_t getRegister(uint32_t index) const;


    /// @brief Writes to a general purpose register (writes to $0 are ignored).
    /// @param index Register index (0-31).
    /// @param value The 32-bit value to write.
    void setRegister(uint32_t index, uint32_t value);


    /// @brief Gets the exact memory address of the instruction currently being executed.
    /// @return The 32-bit program counter.
    uint32_t getProgramCounter() const { return currentProgramCounter; }


    /// @brief Flags the CPU to handle a hardware interrupt on the next execution cycle.
    void requestInterrupt() { interruptPending = true; }


    // Unit Testing Helpers

    /// @brief Redirects execution to an address, discarding any pending load or delay slot.
    /// @param address The 32-bit memory address to jump to.
    void jumpTo(uint32_t address);

    /// @brief Gets the address of the next instruction step() will execute.
    uint32_t getNextInstructionAddress() const { return programCounter; }

private:
    // Instruction Cache (I-Cache)
    struct CacheEntry {
        uint32_t tag = 0;
        uint32_t data = 0;
        bool valid = false;
    };

    Bus* bus;
    GTE gte;

    // Core CPU State
    uint32_t programCounter = 0; // Current execution address
    uint32_t nextProgramCounter = 0; // Used for pipelining
    uint32_t currentProgramCounter = 0; // Exact instruction being executed

    uint32_t registers[32] = {0}; // General Purpose Registers

    uint32_t hi = 0;
    uint32_t lo = 0;
    uint32_t lastWrittenRegister = 0xFFFFFFFF; // Sentinel: no register written this instruction

    uint32_t pendingLoadRegister = 0;
    uint32_t pendingLoadValue = 0;
    uint32_t inFlightLoadRegister = 0;
    uint32_t inFlightLoadValue = 0;

    bool isDelaySlot = false;
    bool nextIsDelaySlot = false;
    bool interruptPending = false;

    // 4KB Cache / 4 bytes per word = 1024 entries
    CacheEntry iCache[1024];

    // Coprocessor 0 (System Control)
    uint32_t coprocessor0Registers[32] = {0}; 
    


    /// @brief Checks if the Instruction Cache is currently isolated (COP0 SR bit 16).
    bool isInstructionCacheIsolated() const;


    /// @brief Dispatches and executes a single decoded MIPS instruction.
    /// @param instruction The 32-bit opcode.
    void execute(uint32_t instruction);


    /// @brief Triggers a Coprocessor Unusable exception.
    /// @param coprocessorNumber Coprocessor index (0-3).
    void triggerCoprocessorUnusable(uint32_t coprocessorNumber);


    /// @brief Triggers a general exception (Syscall, Break, Overflow, etc.).
    /// @param cause The exception cause code.
    void triggerException(uint32_t cause);


    /// @brief Services pending hardware interrupts if the IEc bit is active.
    void triggerHardwareInterrupt();


    /// @brief Accrues cycle timing costs based on memory load wait states.
    /// @param address The physical memory address being loaded.
    /// @warning The extra cycle count is an empirical hack, not a measured hardware RAM latency. 
    /// It is the smallest value (7) that allows Resident Evil 2's second FMV to start.
    void chargeLoad(uint32_t address);
    

    /// @brief Checks if a specific coprocessor is usable (Status.CU0-CU3 = bits 28-31).
    /// @param coprocessorNumber Coprocessor index (0-3).
    bool isCoprocessorEnabled(uint32_t coprocessorNumber) const { return (coprocessor0Registers[12] >> (28 + coprocessorNumber)) & 1; }


    /// @brief Extends a 16-bit signed integer to a 32-bit signed integer.
    int32_t signExtend16(uint32_t value) const { return static_cast<int32_t>(static_cast<int16_t>(value)); }
};