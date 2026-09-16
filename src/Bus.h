#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Constants.h"

#include "gpu/GPU.h"



using namespace std;

class Bus {
private:
    // Memory & Devices
    vector<uint8_t> ram;
    vector<uint8_t> bios;
    GPU gpu;

    // Interupts & Timers
    uint32_t interruptStatus = 0;
    uint32_t interruptMask = 0;

    uint16_t timer0 = 0;
    uint16_t timer1 = 0;
    uint16_t timer2 = 0;

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

public:
    Bus();
    ~Bus();

    bool loadBIOS(const string& filepath);
    GPU& getGPU() { return gpu; }

    // Bus I/O Interface
    uint8_t  read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);
    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);

    // System Control
    bool hasPendingInterrupts() const { return (interruptStatus & interruptMask) != 0; }
    void triggerHardwareInterrupt(uint16_t interruptBit);
    void tickTimers(uint32_t cycles);
};