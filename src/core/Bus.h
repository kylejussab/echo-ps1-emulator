#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Constants.h"
#include "DMA.h"
#include "Timers.h"
#include "../gpu/GPU.h"
#include "../cdrom/CDROM.h"
#include "../io/SIO.h"
#include "../spu/SPU.h"
#include "../mdec/MDEC.h"


/// @brief Emulates the MIPS R3000A CPU, managing instruction execution, the delay slot pipeline, and Coprocessor state.
class Bus {
public:
    Bus();
    ~Bus();

    /// @brief Loads the PlayStation BIOS ROM into memory.
    /// @param filepath The path to the BIOS file (e.g., "SCPH1001.BIN").
    /// @return True if the BIOS was loaded successfully, false otherwise.
    bool loadBIOS(const std::string& filepath);

    /// @brief Copies raw bytes straight into main RAM (used by the PS-EXE sideloader).
    void loadRAMBlock(uint32_t address, const uint8_t* data, size_t size);

    GPU& getGPU() { return gpu; }
    CDROM& getCDROM() { return cdrom; }
    SIO& getSIO() { return sio0; }
    SPU& getSPU() { return spu; }
    MDEC& getMDEC() { return mdec; }


    // Memory-Mapped I/O Interface

    uint8_t  read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);

    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);



    /// @brief Advances the system clocks for peripherals, timers, and interrupts.
    /// @param cycles The number of CPU cycles that just executed.
    /// Defined here (not in Bus.cpp) so the CPU can inline it: it runs once per emulated instruction.
    void tickHardware(int cycles) {
		// Feed cycles to the CD-ROM state machine
		cdrom.advanceClock(cycles);

		dma.tick(cycles);

		bool currentCDROMInt = cdrom.checkInterrupt();
		bool newInterruptDelivered = cdrom.consumeDeliveredInterrupt();
		if (currentCDROMInt && (!lastCDROMInt || newInterruptDelivered)) {
			interruptStatus |= Hardware::IRQ_CDROM;
		}
		lastCDROMInt = currentCDROMInt;

		// GPU Interrupt (IRQ1) - Edge Triggered
		if (gpu.consumeInterruptRequest()) {
			interruptStatus |= Hardware::IRQ_GPU;
		}

        gpu.advanceClock(cycles);



		for (int i = 0; i < cycles; i++) {
			bool currentSio0Int = sio0.advanceClock();
			if (currentSio0Int && !lastSio0Int) {
				interruptStatus |= 0x0080;
			}
			lastSio0Int = currentSio0Int;
		}

		timers.tick(cycles);
		spu.advanceClock(cycles);

		// VBlank (60Hz / NTSC Pacing)
		vblankCounter += cycles;
		if (vblankCounter >= Hardware::CYCLES_PER_FRAME) {
			interruptStatus |= Hardware::IRQ_VBLANK; // Trigger IRQ0
			vblankCounter -= Hardware::CYCLES_PER_FRAME; // Subtract to maintain sub-cycle accuracy

			totalFrames++;
			gpu.toggleFrameBit();
		}
	};

    /// @brief Checks if any unmasked hardware interrupts are pending.
    /// @return True if an interrupt needs to be serviced by the CPU.
    bool hasPendingInterrupts() const { return (interruptStatus & interruptMask) != 0; };


    /// @brief Flags a specific hardware interrupt to be processed.
    /// @param interruptBit The bitmask corresponding to the hardware component (e.g., IRQ_VBLANK).
    void triggerHardwareInterrupt(uint16_t interruptBit)  { interruptStatus |= interruptBit; };
private:
    // Memory & Devices
    std::vector<uint8_t> ram;
    std::vector<uint8_t> bios;
    uint8_t scratchpad[1024] = {0};
    GPU gpu;
    CDROM cdrom;
    SIO sio0;
    SPU spu;
    MDEC mdec;
    DMA dma;


    // Interupts & Timers
    uint32_t interruptStatus = 0;
    uint32_t interruptMask = 0;
    uint32_t vblankCounter = 0;

    Timers timers;

    // Video mode last selected through GP1 (08h) bit 3: false = NTSC, true = PAL. Drives the timers' scanline length.
    bool palMode = false;

    // SIO1 (serial port, 0x1F801050-0x1F80105F): nothing is attached, so it's just register storage for now
    uint8_t sio1Registers[16] = {};
    uint32_t sio1Read(uint32_t address, int bytes) const;
    void sio1Write(uint32_t address, uint32_t value, int bytes);

    bool lastCDROMInt = false;
    bool lastSio0Int = false;

    uint32_t memoryControlRegisters[9] = {};
    uint32_t cacheControlRegister = 0; // 0x1FFE0130






    uint64_t totalFrames = 0;
};