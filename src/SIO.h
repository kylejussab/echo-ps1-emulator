#pragma once
#include <cstdint>
#include "DualShock.h"

class SIO {
public:
    SIO();
    ~SIO();


    /// @brief Reads an 8-bit value from an SIO hardware register (e.g., RX FIFO).
    /// @param address The absolute memory address being read.
    uint8_t read8(uint32_t address);


    /// @brief Reads a 16-bit value from an SIO hardware register.
    /// @param address The absolute memory address being read.
    uint16_t read16(uint32_t address);


    /// @brief Reads a 32-bit value from an SIO hardware register.
    /// @param address The absolute memory address being read.
    uint32_t read32(uint32_t address);


    /// @brief Writes an 8-bit value to an SIO hardware register (e.g., TX FIFO).
    /// @param address The absolute memory address being written to.
    /// @param value The byte to write.
    void write8(uint32_t address, uint8_t value);


    /// @brief Writes a 16-bit value to an SIO hardware register.
    /// @param address The absolute memory address being written to.
    /// @param value The half-word to write.
    void write16(uint32_t address, uint16_t value);


    /// @brief Writes a 32-bit value to an SIO hardware register.
    /// @param address The absolute memory address being written to.
    /// @param value The word to write.
    void write32(uint32_t address, uint32_t value);


    /// @brief Steps the internal SIO clock to handle ACK interrupt (IRQ7) delays.
    /// @return True if the ACK delay has finished and IRQ7 should be triggered.
    bool tick(); 


    DualShock& getController() { return controller; }

private:
    // TODO: Instantiate a second DualShock object for Player 2 multiplexing
    DualShock controller;


    // Hardware Registers (0x1F801040 - 0x1F80104E)
    uint8_t  receiveFIFO = 0xFF;
    uint8_t  transmitFIFO = 0xFF;
    uint16_t statusRegister = 0x0005; // Default: Transmit Ready, Transmit Empty
    uint16_t modeRegister = 0;
    uint16_t controlRegister = 0;
    uint16_t baudRegister = 0;


    // Communication State Machine
    enum class State {
        Idle,
        Addressing, // Expecting 0x01 (Controller) or 0x81 (Memory Card)
        Command,    // Expecting 0x42 (Read) or 0x43 (Config)
        Ready,      // Sending 0x5A
        Payload,    // Sending 2 to 6 bytes of controller state
        Done
    };

    State currentState = State::Idle;
    int payloadIndex = 0;
    int expectedPayloadSize = 0;


    // ACK Interrupt (IRQ7) Timing
    int ackDelay = 0;
    bool ackPending = false;

    /// @brief Processes a byte written to the TX FIFO and advances the state machine.
    /// @param data The byte sent by the CPU.
    void processTransmit(uint8_t data);


    // TODO: Add Memory Card state machine logic parsing
    // TODO: Add Rumble configuration commands (0x43, 0x4D) to state machine
};
