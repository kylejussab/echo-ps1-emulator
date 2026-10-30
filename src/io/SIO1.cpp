#include "SIO1.h"
#include <algorithm>

namespace {
    constexpr uint32_t SIO1_DR   = 0x1F801050; // 4 (data, 1 byte used)
    constexpr uint32_t SIO1_SR   = 0x1F801054; // 4
    constexpr uint32_t SIO1_MR   = 0x1F801058; // 2
    constexpr uint32_t SIO1_CR   = 0x1F80105A; // 2
    constexpr uint32_t SIO1_MISC = 0x1F80105C; // 2
    constexpr uint32_t SIO1_BR   = 0x1F80105E; // 2

    // Control register bits
    constexpr uint16_t CR_TXEN       = 1 << 0;
    constexpr uint16_t CR_RXEN       = 1 << 2;
    constexpr uint16_t CR_ACK        = 1 << 4;  // write-only
    constexpr uint16_t CR_RESET      = 1 << 6;  // write-only
    constexpr uint16_t CR_TX_IRQ_EN  = 1 << 10;
    constexpr uint16_t CR_RX_IRQ_EN  = 1 << 11;
    constexpr uint16_t CR_DSR_IRQ_EN = 1 << 12;

    constexpr uint16_t SR_ERROR_OVERRUN = 1 << 4;
    constexpr uint16_t SR_ERROR_MASK    = (1 << 3) | (1 << 4) | (1 << 5);
}


uint8_t SIO1::read8(uint32_t address) {
    if (address == SIO1_DR) return popReceived();
    if (address >= SIO1_DR + 1 && address < SIO1_SR) return 0; // upper bytes of the data register

    const uint16_t half = read16(address & ~1u);
    return (address & 1) ? (half >> 8) : (half & 0xFF);
}


uint16_t SIO1::read16(uint32_t address) {
    switch (address & ~1u) {
        case SIO1_DR:       return popReceived();
        case SIO1_DR + 2:   return 0;
        case SIO1_SR:       return getStatus() & 0xFFFF;
        case SIO1_SR + 2:   return getStatus() >> 16;
        case SIO1_MR:       return modeRegister;
        case SIO1_CR:       return controlRegister;
        case SIO1_MISC:     return miscRegister;
        case SIO1_BR:       return baudRegister;
        default:            return 0;
    }
}


uint32_t SIO1::read32(uint32_t address) {
    switch (address & ~3u) {
        case SIO1_DR:   return popReceived();
        case SIO1_SR:   return getStatus();
        case SIO1_MR:   return modeRegister | (static_cast<uint32_t>(controlRegister) << 16);
        case SIO1_MISC: return miscRegister | (static_cast<uint32_t>(baudRegister) << 16);
        default:        return 0;
    }
}


void SIO1::write8(uint32_t address, uint8_t value) {
    if (address == SIO1_DR) {
        writeData(value);
        return;
    }
    if (address < SIO1_MR) return; // upper bytes of DR, read-only SR

    // Byte store into a 16-bit register: merge into the existing halfword
    uint16_t half = read16(address & ~1u);
    if (address & 1) half = (half & 0x00FF) | (static_cast<uint16_t>(value) << 8);
    else             half = (half & 0xFF00) | value;
    write16(address & ~1u, half);
}


void SIO1::write16(uint32_t address, uint16_t value) {
    switch (address & ~1u) {
        case SIO1_DR:   writeData(value & 0xFF); break;
        case SIO1_MR:   modeRegister = value; break;
        case SIO1_CR:   writeControl(value); break;
        case SIO1_MISC: miscRegister = value; break;
        case SIO1_BR:
            baudRegister = value;
            baudTimerBase = cycleCounter; // writing the reload value restarts the timer
            break;
        default: break; // SR is read-only, DR upper half unused
    }
}


void SIO1::write32(uint32_t address, uint32_t value) {
    switch (address & ~3u) {
        case SIO1_DR:   writeData(value & 0xFF); break;
        case SIO1_MR:
            write16(SIO1_MR, value & 0xFFFF);
            write16(SIO1_CR, value >> 16);
            break;
        case SIO1_MISC:
            write16(SIO1_MISC, value & 0xFFFF);
            write16(SIO1_BR, value >> 16);
            break;
        default: break; // SR is read-only
    }
}


void SIO1::advanceClock(int cycles) {
    cycleCounter += cycles;
    if (!txShifting) return;

    txCyclesRemaining -= cycles;
    while (txShifting && txCyclesRemaining <= 0) {
        const int overshoot = txCyclesRemaining; // <= 0, carried into the next byte

        // The byte has finished shifting out of the TX pin
        txShifting = false;
        if (transmitCallback) transmitCallback(txShift);

        // Back-to-back: the holding register feeds the shifter immediately
        startTransmitIfPossible();
        if (txShifting) {
            txCyclesRemaining += overshoot;
        }
        else if (controlRegister & CR_TX_IRQ_EN) {
            raiseInterrupt(); // TX idle (Status bit 2 just became set)
        }
    }
}


bool SIO1::receiveByte(uint8_t byte) {
    if (!(controlRegister & CR_RXEN)) return false; // receiver disabled, line is ignored

    if (rxCount >= kRxFifoSize) {
        errorFlags |= SR_ERROR_OVERRUN;
        return false;
    }

    rxFifo[(rxHead + rxCount) & (kRxFifoSize - 1)] = byte;
    rxCount++;

    // CTRL bits 8-9 select the RX IRQ threshold: 1, 2, 4 or 8 bytes
    const int threshold = 1 << ((controlRegister >> 8) & 3);
    if ((controlRegister & CR_RX_IRQ_EN) && rxCount >= threshold) {
        raiseInterrupt();
    }
    return true;
}


void SIO1::setDSR(bool active) {
    const bool risingEdge = active && !dsrLevel;
    dsrLevel = active;
    if (risingEdge && (controlRegister & CR_DSR_IRQ_EN)) {
        raiseInterrupt();
    }
}


// Privates
uint32_t SIO1::getStatus() const {
    uint32_t status = 0;
    if (!txHoldingFull)                 status |= 1 << 0; // TX Ready 1 (TX FIFO can accept a byte)
    if (rxCount > 0)                    status |= 1 << 1; // RX FIFO not empty
    if (!txHoldingFull && !txShifting)  status |= 1 << 2; // TX Ready 2 (TX idle)
    status |= errorFlags & SR_ERROR_MASK;
    if (dsrLevel)                       status |= 1 << 7;
    if (ctsLevel)                       status |= 1 << 8;
    if (irqFlag)                        status |= 1 << 9;
    status |= (baudTimerValue() & 0x1FFFFF) << 11;
    return status;
}


uint32_t SIO1::baudReloadValue() const {
    // Mode bits 0-1: 1 = x1, 2 = x16, 3 = x64 (0 is treated as x1)
    const uint32_t factor = ((modeRegister & 3) == 2) ? 16 : ((modeRegister & 3) == 3) ? 64 : 1;
    // psx-spx: baudrate = 33868800 / max(1, (BR * factor) & ~1), so one bit lasts that many CPU cycles
    return std::max<uint32_t>(1, (static_cast<uint32_t>(baudRegister) * factor) & ~1u);
}


uint32_t SIO1::baudTimerValue() const {
    const uint32_t reload = baudReloadValue();
    const uint32_t elapsed = static_cast<uint32_t>((cycleCounter - baudTimerBase) % reload);
    return reload - elapsed; // counts down from the reload value
}


int SIO1::cyclesPerByte() const {
    // Frame length in half-bit units: start bit + data bits + optional parity + stop bits
    const int dataBits = 5 + ((modeRegister >> 2) & 3);
    int halfBits = 2 + dataBits * 2;
    if (modeRegister & (1 << 4)) halfBits += 2; // parity enabled
    switch ((modeRegister >> 6) & 3) {
        case 2:  halfBits += 3; break; // 1.5 stop bits
        case 3:  halfBits += 4; break; // 2 stop bits
        default: halfBits += 2; break; // 1 stop bit
    }
    return static_cast<int>(std::max<int64_t>(1, static_cast<int64_t>(halfBits) * baudReloadValue() / 2));
}


uint8_t SIO1::popReceived() {
    if (rxCount > 0) {
        lastReceived = rxFifo[rxHead];
        rxHead = (rxHead + 1) & (kRxFifoSize - 1);
        rxCount--;
    }
    return lastReceived; // an empty FIFO keeps returning the last byte
}


void SIO1::writeData(uint8_t value) {
    if (txHoldingFull) return; // TX FIFO full, the byte is lost
    txHolding = value;
    txHoldingFull = true;
    startTransmitIfPossible();
}


void SIO1::writeControl(uint16_t value) {
    if (value & CR_RESET) {
        reset();
        return;
    }

    // ACK (bit 4) and RESET (bit 6) are write-only and never read back
    controlRegister = value & ~(CR_ACK | CR_RESET);

    // ACK clears the error flags (bits 3, 4, 5) and the IRQ flag (bit 9)
    if (value & CR_ACK) {
        errorFlags = 0;
        irqFlag = false;
    }

    // Bytes written while TXEN was off sit in the holding register until it is enabled
    startTransmitIfPossible();
}


void SIO1::startTransmitIfPossible() {
    if (txShifting || !txHoldingFull || !(controlRegister & CR_TXEN)) return;

    txShift = txHolding;
    txHoldingFull = false;
    txShifting = true;
    txCyclesRemaining = cyclesPerByte();

    if (controlRegister & CR_TX_IRQ_EN) {
        raiseInterrupt(); // TX Ready 1 just became set
    }
}


void SIO1::raiseInterrupt() {
    if (!irqFlag) { // IRQ8 is edge triggered on Status bit 9
        irqFlag = true;
        irqRequested = true;
    }
}


void SIO1::reset() {
    modeRegister = 0;
    controlRegister = 0;
    errorFlags = 0;
    irqFlag = false;
    irqRequested = false;
    rxHead = 0;
    rxCount = 0;
    txHoldingFull = false;
    txShifting = false;
    txCyclesRemaining = 0;
    // The baud reload value and SIO1_? survive a reset
}