#pragma once

#include <cstdint>
#include <functional>
#include <utility>

/// @brief Emulates Serial I/O port 1 (1F801050h-1F80105Fh, IRQ8): the 8-pin external serial port (link cable / debug TTY).
/// @details Nothing is plugged into the port by default. Transmitted bytes are handed to an optional callback, and the host can
///          feed received bytes and the DSR/CTS input levels in through receiveByte(), setDSR() and setCTS().
class SIO1 {
public:
    using TransmitCallback = std::function<void(uint8_t)>;

    uint8_t  read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);
    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);


    /// @brief Advances the transmit shifter and the baud timer.
    /// @param cycles CPU cycles that just executed. Cheap when idle (one add and one branch).
    void advanceClock(int cycles);


    /// @brief Returns true once if IRQ8 should be raised in I_STAT (edge of Status bit 9), then clears the request.
    bool consumeInterruptRequest() { bool r = irqRequested; irqRequested = false; return r; }


    /// @brief Registers the sink for transmitted bytes (called when a byte has finished shifting out).
    void setTransmitCallback(TransmitCallback callback) { transmitCallback = std::move(callback); }


    /// @brief Pushes a byte into the RX FIFO as if it arrived on the RX pin.
    /// @return False if the byte was dropped (receiver disabled, or FIFO full which sets the overrun flag).
    bool receiveByte(uint8_t byte);


    /// @brief Sets the DSR input level (Status bit 7). A rising edge raises IRQ8 if CTRL bit 12 is set.
    void setDSR(bool active);


    /// @brief Sets the CTS input level (Status bit 8).
    void setCTS(bool active) { ctsLevel = active; }

private:
    static constexpr int kRxFifoSize = 8;

    // Registers
    uint16_t modeRegister = 0;
    uint16_t controlRegister = 0;
    uint16_t miscRegister = 0;   // SIO1_? at 1F80105Ch, purpose unknown, plain R/W storage
    uint16_t baudRegister = 0;

    // Status bits that are stored (the rest of SR is derived in getStatus())
    uint16_t errorFlags = 0;     // bits 3 (parity), 4 (overrun), 5 (bad stop bit)
    bool dsrLevel = false;
    bool ctsLevel = false;
    bool irqFlag = false;        // Status bit 9
    bool irqRequested = false;   // pending edge for the Bus

    // Receive side: 8 byte FIFO
    uint8_t rxFifo[kRxFifoSize] = {0};
    int rxHead = 0;
    int rxCount = 0;
    uint8_t lastReceived = 0;

    // Transmit side: 1 byte holding register feeding a shift register
    uint8_t txHolding = 0;
    bool txHoldingFull = false;
    uint8_t txShift = 0;
    bool txShifting = false;
    int txCyclesRemaining = 0;

    // Baud timer (Status bits 11-31), evaluated lazily from a free-running counter
    uint64_t cycleCounter = 0;
    uint64_t baudTimerBase = 0;

    TransmitCallback transmitCallback;


    uint32_t getStatus() const;
    uint32_t baudReloadValue() const;
    uint32_t baudTimerValue() const;
    int cyclesPerByte() const;
    uint8_t popReceived();
    void writeData(uint8_t value);
    void writeControl(uint16_t value);
    void startTransmitIfPossible();
    void raiseInterrupt();
    void reset();
};