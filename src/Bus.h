#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Constants.h"
#include "gpu/GPU.h"
#include "CDROM.h"
#include "SIO.h"

/// @brief Emulates the MIPS R3000A CPU, managing instruction execution, the delay slot pipeline, and Coprocessor state.
class Bus {
public:
    Bus();
    ~Bus();

    /// @brief Loads the PlayStation BIOS ROM into memory.
    /// @param filepath The path to the BIOS file (e.g., "SCPH1001.BIN").
    /// @return True if the BIOS was loaded successfully, false otherwise.
    bool loadBIOS(const std::string& filepath);

    GPU& getGPU() { return gpu; }
    CDROM& getCDROM() { return cdrom; }
    SIO& getSIO() { return sio0; }


    // Memory-Mapped I/O Interface

    uint8_t  read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);

    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);


    /// @brief Advances the system clocks for peripherals, timers, and interrupts.
    /// @param cycles The number of CPU cycles that just executed.
    void tickHardware(int cycles);


    /// @brief Checks if any unmasked hardware interrupts are pending.
    /// @return True if an interrupt needs to be serviced by the CPU.
    bool hasPendingInterrupts() const { return (interruptStatus & interruptMask) != 0; }
    
    
    /// @brief Flags a specific hardware interrupt to be processed.
    /// @param interruptBit The bitmask corresponding to the hardware component (e.g., IRQ_VBLANK).
    void triggerHardwareInterrupt(uint16_t interruptBit)  { interruptStatus |= interruptBit; };    
private:
    // Memory & Devices
    std::vector<uint8_t> ram;
    std::vector<uint8_t> bios;
    GPU gpu;
    CDROM cdrom;
    SIO sio0; 


    // Interupts & Timers
    uint32_t interruptStatus = 0;
    uint32_t interruptMask = 0;
    uint32_t vblankCounter = 0;

    uint16_t timer0 = 0;
    uint16_t timer1 = 0;
    uint16_t timer2 = 0;
    uint16_t timer2Target = 0xFFFF;
    uint32_t timer2Mode = 0;
    uint32_t timer2CycleAccumulator = 0;


    // Direct Memory Access
    uint32_t DMAControlRegister = 0x07777777; // Default PS1 startup value
    uint32_t DMAInterruptControlRegister = 0;

    // Channel 2: GPU
    uint32_t DMAChannel2MemoryAddress = 0;
    uint32_t DMAChannel2BlockControl = 0;
    uint32_t DMAChannel2ChannelControl = 0;
    
    // Channel 6: OTC (Ordering Table Clear)
    uint32_t DMAChannel6MemoryAddress = 0;
    uint32_t DMAChannel6BlockControl = 0;
    uint32_t DMAChannel6ChannelControl = 0;

    // Internal Handlers
    void performGPUDMATransfer(bool directionToRAM);
    void performOTCDMATransfer();
    void setDMAInterruptFlag(uint8_t channel);
    void updateDMAInterruptLine();



    // DEBUG

    /// @brief Monitors hardware I/O reads to detect infinite polling loops.
    /// @param address The absolute memory address being read.
    void checkSpinlock(uint32_t address);
};