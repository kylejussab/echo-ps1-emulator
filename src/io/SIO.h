#pragma once

#include <cstdint>
#include "io/DualShock.h"

/// @brief Emulates the Serial Input/Output port 0 bus, managing communication between the console, controllers, and memory cards.
class SIO {
public:
    /// @brief Reads an 8-bit value from a hardware register (e.g., the Receive First-In-First-Out buffer).
    /// @param address The absolute memory address being read.
    /// @return The 8-bit data at the specified address.
    uint8_t read8(uint32_t address);


    /// @brief Reads a 16-bit value from a hardware register.
    /// @param address The absolute memory address being read.
    /// @return The 16-bit data at the specified address.
    uint16_t read16(uint32_t address);


    /// @brief Reads a 32-bit value from a hardware register.
    /// @param address The absolute memory address being read.
    /// @return The 32-bit data at the specified address.
    uint32_t read32(uint32_t address);


    /// @brief Writes an 8-bit value to a hardware register (e.g., the Transmit First-In-First-Out buffer).
    /// @param address The absolute memory address being written to.
    /// @param value The byte to write.
    void write8(uint32_t address, uint8_t value);


    /// @brief Writes a 16-bit value to a hardware register.
    /// @param address The absolute memory address being written to.
    /// @param value The half-word to write.
    void write16(uint32_t address, uint16_t value);


    /// @brief Writes a 32-bit value to an SIO hardware register.
    /// @param address The absolute memory address being written to.
    /// @param value The word to write.
    void write32(uint32_t address, uint32_t value);


    /// @brief Steps the internal clock to handle Acknowledge (Data Set Ready) pulse timing.
    /// @details Runs once per Central Processing Unit instruction. The internal advanceClockSlowly() method only executes while an Acknowledge is pending or active.
    /// @return True if the Acknowledge pulse just started and Interrupt Request 7 should be triggered.
    bool advanceClock();


    /// @brief Retrieves the active DualShock controller instance connected to port 1.
    /// @return A reference to the DualShock controller.
    DualShock& getController() { return controller; }

private:
    // ~250-300 CPU cycles gives the hardware a realistic timeframe to pull /ACK low
    static constexpr int kAcknowledgeDelayCycles = 300;
    static constexpr int kDataSetReadyPulseCycles = 100;


    /// @todo Instantiate a second DualShock object for Player 2 (port 2 currently reads as "no controller").
    DualShock controller;


    // Hardware Registers (0x1F801040 - 0x1F80104E)
    uint8_t  receiveFIFO = 0xFF;
    uint8_t  transmitFIFO = 0xFF;
    uint16_t statusRegister = 0x0005; // Default: Transmit Ready, Transmit Idle
    uint16_t modeRegister = 0;
    uint16_t controlRegister = 0;
    uint16_t baudRegister = 0;


    enum class Device {
        None,
        Pad,
    };

    Device activeDevice = Device::None;


    int acknowledgeDelayCycles = 0;
    bool acknowledgePending = false;
    int dataSetReadyPulseCycles = 0;

    int transferCyclesRemaining = 0; // cycles left before the byte has finished shifting
    uint8_t pendingReply = 0xFF;
    bool pendingAcknowledge = false;


    /// @brief Internal slow-path clock logic executed only when an Acknowledge signal is pending or active.
    /// @return True if the Acknowledge pulse just started, triggering an interrupt.
    bool advanceClockSlowly();


    /// @brief Processes a byte written to the Transmit First-In-First-Out buffer.
    /// @param data The byte to transmit over the serial line.
    void processTransmit(uint8_t data);


    /// @brief Drops the selected device and aborts any active communication packet.
    void deselect();


    /// @brief Executes a software reset on the port, clearing buffers and returning to a default hardware state.
    void softReset();


    /// @brief Calculates the number of Central Processing Unit cycles required to transmit or receive a single byte over the serial line.
    /// @return The calculated duration in CPU cycles.
    int calculateByteCycles() const;


    /// @todo Add Memory Card (0x81) handling and state management.
};