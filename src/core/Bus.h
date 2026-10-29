#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Constants.h"
#include "DMA.h"
#include "Timers.h"
#include "../cdrom/CDROM.h"
#include "../gpu/GPU.h"
#include "../mdec/MDEC.h"
#include "../io/SIO.h"
#include "../spu/SPU.h"


/// @brief Emulates the MIPS R3000A CPU, managing instruction execution, the delay slot pipeline, and Coprocessor state.
class Bus {
public:
    Bus();

    GPU& getGPU() { return gpu; }
    CDROM& getCDROM() { return cdrom; }
    SIO& getSIO() { return sio0; }
    SPU& getSPU() { return spu; }
    MDEC& getMDEC() { return mdec; }


    /// @brief Loads the PlayStation BIOS ROM into memory.
    /// @param filepath The path to the BIOS file (e.g., "SCPH1001.BIN").
    /// @return True if the BIOS was loaded successfully, false otherwise.
    bool loadBIOS(const std::string& filepath);


    /// @brief Copies raw bytes straight into main RAM (used by the PS-EXE sideloader).
    /// @param address The physical memory address to start writing to.
    /// @param data Pointer to the raw byte buffer.
    /// @param size The number of bytes to copy.
    void loadRAMBlock(uint32_t address, const uint8_t* data, size_t size);

    
    /// @brief Reads an 8-bit value from the physical memory map, routing to RAM, BIOS, or the target peripheral.
    /// @param address The 32-bit physical memory address.
    /// @return The 8-bit value read from the target device.
    uint8_t read8(uint32_t address);


    /// @brief Writes an 8-bit value to the physical memory map. Writes to ROM/BIOS are ignored.
    /// @param address The 32-bit physical memory address.
    /// @param value The 8-bit value to write.
    void write8(uint32_t address, uint8_t value);


    /// @brief Reads a 16-bit value from the physical memory map (must be halfword-aligned).
    /// @param address The 32-bit physical memory address.
    /// @return The 16-bit value read from the target device.
    uint16_t read16(uint32_t address);


    /// @brief Writes a 16-bit value to the physical memory map (must be halfword-aligned).
    /// @param address The 32-bit physical memory address.
    /// @param value The 16-bit value to write.
    void write16(uint32_t address, uint16_t value);


    /// @brief Reads a 32-bit value from the physical memory map (must be word-aligned).
    /// @param address The 32-bit physical memory address.
    /// @return The 32-bit value read from the target device.
    uint32_t read32(uint32_t address);


    /// @brief Writes a 32-bit value to the physical memory map (must be word-aligned).
    /// @param address The 32-bit physical memory address.
    /// @param value The 32-bit value to write.
    void write32(uint32_t address, uint32_t value);


    /// @brief Advances the system clocks for peripherals, timers, and interrupts.
    /// @param cycles The number of CPU cycles that just executed.
    void advanceClock(int cycles);


    /// @brief Checks if any unmasked hardware interrupts are pending.
    /// @return True if an interrupt needs to be serviced by the CPU.
    bool hasPendingInterrupts() const { return (interruptStatus & interruptMask) != 0; };


    /// @brief Flags a specific hardware interrupt to be processed.
    /// @param interruptBit The bitmask corresponding to the hardware component (e.g., IRQ_VBLANK).
    void triggerHardwareInterrupt(uint16_t interruptBit)  { interruptStatus |= interruptBit; };

private:
    GPU gpu;
    CDROM cdrom;
    SIO sio0;
    SPU spu;
    MDEC mdec;
    DMA dma;
    Timers timers;

    std::vector<uint8_t> ram;
    std::vector<uint8_t> bios;
    uint8_t scratchpad[1024] = {0};

    // Interupts & Timers
    uint32_t interruptStatus = 0;
    uint32_t interruptMask = 0;
    bool lastCDROMInt = false;
    bool lastSio0Int = false;

    uint32_t memoryControlRegisters[9] = {};
    uint32_t cacheControlRegister = 0; // 0x1FFE0130

    uint8_t sio1Registers[16] = {};

    uint64_t totalFrames = 0;

    // Video mode last selected through GP1 (08h) bit 3: false = NTSC, true = PAL. Drives the timers' scanline length.
    bool palMode = false;


    /// @brief Reads from the SIO1 (Serial Port 1) register block.
    /// @param address The 32-bit physical memory address.
    /// @param bytes The size of the read operation (1, 2, or 4 bytes).
    /// @return The value read from the dummy register storage.
    /// @note SIO1 is currently unimplemented; this serves as a stub to prevent memory faults.
    uint32_t sio1Read(uint32_t address, int bytes) const;


    /// @brief Writes to the SIO1 (Serial Port 1) register block.
    /// @param address The 32-bit physical memory address.
    /// @param value The value to write.
    /// @param bytes The size of the write operation (1, 2, or 4 bytes).
    /// @note SIO1 is currently unimplemented; this serves as a stub to prevent memory faults.
    void sio1Write(uint32_t address, uint32_t value, int bytes); 
    
    
    /// @brief Checks if a physical address belongs to an unconnected expansion region.
    /// @param address The 32-bit physical memory address.
    /// @return True if the address is open bus (reads return 0xFF).
    /// @note Real expansion 3 reads back some bytes you wrote, but all-ones is acceptable for most cases.
    static bool isOpenBus(uint32_t address) {
        return (address >= 0x1F000000 && address < 0x1F800000)   // Expansion region 1 (no cartridge)
            || (address >= 0x1F802000 && address < 0x1F803000)   // Expansion region 2
            || (address >= 0x1FA00000 && address < 0x1FC00000);  // Expansion region 3
    }

    /// @brief Checks if a physical address belongs to a read-only or unconnected region.
    /// @param address The 32-bit physical memory address.
    /// @return True if writes to this address should be safely ignored by the hardware.
    static bool isIgnoredWrite(uint32_t address) {
        return isOpenBus(address)
            || (address >= 0x1FC00000 && address < 0x1FC00000 + 512 * 1024); // BIOS ROM
    }
};