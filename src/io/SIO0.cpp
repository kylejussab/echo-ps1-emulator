#include "SIO0.h"


uint8_t SIO0::read8(uint32_t address) {
    if (address == 0x1F801040) { // SIO_DATA
        statusRegister &= ~(1 << 1); // RX FIFO not empty (Receive Not Empty)
        return receiveFIFO;
    }
    return 0;
}


uint16_t SIO0::read16(uint32_t address) {
    switch (address) {
        case 0x1F801044: { // SIO_STAT (Status)
            return statusRegister;
        }
        case 0x1F801048: { // SIO_MODE
            return modeRegister;
        }
        case 0x1F80104A: { // SIO_CTRL (Control)
            return controlRegister;
        }
        case 0x1F80104E: { // SIO_BAUD
            return baudRegister;
        }
        default: {
            return 0;
        }
    }
}


uint32_t SIO0::read32(uint32_t address) {
    switch (address) {
		case 0x1F801040: { // SIO_DATA
			return read8(address);
        }	
		case 0x1F801044: { // SIO_STAT (Status)
			return statusRegister;
        }	
		case 0x1F801048: { // SIO_MODE (lower 16) & SIO_CTRL (upper 16)
			return modeRegister | (static_cast<uint32_t>(controlRegister) << 16);
        }	
		case 0x1F80104C: { // SIO_BAUD (upper 16 bits, lower 16 unused/zero)
			return static_cast<uint32_t>(baudRegister) << 16;
        }	
		default: {
			return 0;
        }
	}
}


void SIO0::write8(uint32_t address, uint8_t value) {
    if (address == 0x1F801040) { // SIO_DATA
        transmitFIFO = value;
        if (controlRegister & (1 << 0)) { // Transmit Enable
            processTransmit(value);
        }
    }
}


void SIO0::write16(uint32_t address, uint16_t value) {
    switch (address) {
        case 0x1F801048: { // SIO_MODE
            modeRegister = value;
            break;
        }
        case 0x1F80104A: { // SIO_CTRL (Control)
             // Bit 6: Reset
            if (value & (1 << 6)) { // Reset (Write-only, self-clearing)
                softReset();
                return;
            }

            // Bits 4 (ACK) and 6 (RESET) are write-only and never read back as set
            controlRegister = value & ~((1 << 4) | (1 << 6));

            // Bit 4: Acknowledge, clears the IRQ flag (and error flags)
            if (value & (1 << 4)) {
                statusRegister &= ~0x0238; // Acknowledge clears bits 3, 4, 5, 9
            }

            // Bit 1: DTR. If this goes low, the pad is deselected and any packet is aborted.
            if ((value & (1 << 1)) == 0) { // Data Terminal Ready (Chip Select)
                deselect();
            }
            break;
        }
        case 0x1F80104E: { // SIO_BAUD
            baudRegister = value;
            break;
        }
    }
}


void SIO0::write32(uint32_t address, uint32_t value) {
    switch (address) {
        case 0x1F801040: { // SIO_DATA
            write8(address, value & 0xFF);
            break;
        }
        case 0x1F801048: { // SIO_MODE
            // 32-bit store covers MODE (low half) and CTRL (high half)
            write16(0x1F801048, value & 0xFFFF);
            write16(0x1F80104A, value >> 16);
            break;
        }
        case 0x1F80104C: {
            write16(0x1F80104E, value >> 16);
            break;
        }
        default: {
            write16(address, value & 0xFFFF);
            break;
        }
    }
}


bool SIO0::advanceClock() {
    if (transferCyclesRemaining <= 0 && !acknowledgePending && dataSetReadyPulseCycles <= 0) return false;
    return advanceClockSlowly();
}



// Privates
bool SIO0::advanceClockSlowly() {
    bool fireInterruptRequest = false;

    // The byte has finished shifting: the reply becomes readable and the pad may now pull /ACK
    if (transferCyclesRemaining > 0 && --transferCyclesRemaining == 0) {
        receiveFIFO = pendingReply;
        statusRegister |= ((1 << 2) | (1 << 1));
        if (pendingAcknowledge) {
            pendingAcknowledge = false;
            acknowledgePending = true;
            acknowledgeDelayCycles = kAcknowledgeDelayCycles;
        }
    }

    if (acknowledgePending && --acknowledgeDelayCycles <= 0) {
        acknowledgePending = false;

        // The controller pulls /ACK low: DSR goes active...
        statusRegister |= (1 << 7);
        dataSetReadyPulseCycles = kDataSetReadyPulseCycles;

        // ...and that edge raises IRQ7, but only if the game enabled DSR interrupts
        if (controlRegister & (1 << 12)) {
            statusRegister |= (1 << 9);
            fireInterruptRequest = true;
        }
    }

    if (dataSetReadyPulseCycles > 0 && --dataSetReadyPulseCycles == 0) {
        statusRegister &= ~(1 << 7);
    }

    return fireInterruptRequest;
}


void SIO0::processTransmit(uint8_t data) {
    uint8_t reply = 0xFF; // High-Z / nothing connected
    bool acknowledgeSignalPulled = false;

    const bool selected = (controlRegister & (1 << 1)) != 0;
    const bool port2    = (controlRegister & (1 << 13)) != 0;

    // Only a selected device on port 1 can answer (port 2 has no pad yet)
    if (selected && !port2) {
        if (activeDevice == Device::None) {
            if (data == 0x01) {
                // CPU is addressing a controller
                activeDevice = Device::Pad;
                controller.resetTransfer();
                acknowledgeSignalPulled = true;
            }
            /// @todo 0x81 (Memory Card addressing). No device answers, so no ACK.
        }
        else if (activeDevice == Device::Pad) {
            acknowledgeSignalPulled = controller.transfer(data, reply);
        }
    }

    receiveFIFO = reply;

    pendingReply = reply;
    pendingAcknowledge = acknowledgeSignalPulled;
    transferCyclesRemaining = calculateByteCycles();
    statusRegister |= (1 << 0);
    statusRegister &= ~((1 << 2) | (1 << 1));
}


void SIO0::deselect() {
    activeDevice = Device::None;
    controller.resetTransfer();
    acknowledgePending = false;
    acknowledgeDelayCycles = 0;
    transferCyclesRemaining = 0;
    pendingAcknowledge = false;
}


void SIO0::softReset() {
    statusRegister = (1 << 0) | (1 << 2);
    modeRegister = 0;
    controlRegister = 0;
    receiveFIFO = 0xFF;
    dataSetReadyPulseCycles = 0;
    deselect();
}


int SIO0::calculateByteCycles() const {
    int baudMultiplier = 1; 

    // The baud rate factor is defined by the lowest 2 bits of the mode register.
    // 01 = x1 multiplier, 10 = x16 multiplier, 11 = x64 multiplier.
    const uint16_t baudRateFactor = modeRegister & 0x03;
    if (baudRateFactor == 2) {
        baudMultiplier = 16;
    } 
    else if (baudRateFactor == 3) {
        baudMultiplier = 64;
    }

    // If the baud register has not been explicitly programmed yet, fall back to the default Basic Input/Output System value of 0x88.
    const int baudReload = (baudRegister != 0) ? baudRegister : 0x88;
    
    // 8 bits per byte * reload factor * baud multiplier
    return 8 * baudReload * baudMultiplier;
}
