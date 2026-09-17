#pragma once
#include <cstdint>
#include <queue>

class CDROM {
public:
    CDROM();
    

    /// @brief Reads an 8-bit value from a CD-ROM hardware register (0x1F801800 - 0x1F801803).
    /// @param address The absolute memory address being read.
    /// @return The requested byte (e.g., Status, Response FIFO).
    uint8_t read8(uint32_t address);


    /// @brief Writes an 8-bit value to a CD-ROM hardware register.
    /// @param address The absolute memory address being written to.
    /// @param value The byte to write (e.g., Command, Parameter FIFO)
    void write8(uint32_t address, uint8_t value);
    

    /// @brief Checks if the CD-ROM controller is currently requesting a hardware interrupt.
    /// @return True if IRQ2 should be triggered on the main Bus.
    bool checkInterrupt();

    // TODO: Add a public method to mount a game ISO/CUE file from the host filesystem
private:
    // Hardware Registers & State
    uint8_t index = 0; // Port 0x1F801800: Selects which hidden registers the other ports access
    uint8_t interruptEnable = 0; // Port 0x1F801803 (Index 1)
    uint8_t interruptFlag = 0; // Port 0x1F801803 (Index 0)
    
    std::queue<uint8_t> parameterFIFO;
    std::queue<uint8_t> responseFIFO;


    // TODO: Add state variables for drive mechanics (tray open/closed, motor spinning, current sector)


    /// @brief Queues a byte into the response FIFO for the CPU to read on subsequent cycles.
    /// @param value The byte to send back to the CPU.
    void pushResponse(uint8_t value);


    /// @brief Decodes and executes a CD-ROM controller command (e.g., GetStat, ReadN, Setloc).
    /// @param command The command byte issued by the CPU via Port 0x1F801801.
    void executeCommand(uint8_t command);
};
