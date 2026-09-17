#include "SIO.h"
#include <iostream>

SIO::SIO() {}

SIO::~SIO() {}


uint8_t SIO::read8(uint32_t address) {
    if (address == 0x1F801040) {
        // CPU is reading the received byte. Clear the "Receive Not Empty" flag.
        statusRegister &= ~0x0002; 
        return receiveFIFO;
    }
    return 0; // Unhandled 8-bit read
}


uint16_t SIO::read16(uint32_t address) {
    if (address == 0x1F801044) return statusRegister;
    if (address == 0x1F801048) return modeRegister;
    if (address == 0x1F80104A) return controlRegister;
    if (address == 0x1F80104E) return baudRegister;
    return 0;
}


uint32_t SIO::read32(uint32_t address) {
    if (address == 0x1F801040) return read8(address);
    if (address == 0x1F801044) return read16(address);
    return 0;
}


void SIO::write8(uint32_t address, uint8_t value) {
    if (address == 0x1F801040) {
        transmitFIFO = value;
        // Transmit is no longer empty, Receive is not ready yet
        statusRegister &= ~0x0005; 
        processTransmit(value);
    }
}


void SIO::write16(uint32_t address, uint16_t value) {
    if (address == 0x1F801048) {
        modeRegister = value;
    } 
    else if (address == 0x1F80104A) {
        controlRegister = value;
        
        // Bit 4: Acknowledge SIO Interrupt
        if (value & 0x0010) {
            statusRegister &= ~0x0200; // Clear the SIO Interrupt Request flag (Status Register Bit 9)
            controlRegister &= ~0x0010; // Clear the ACK bit itself
        }
        
        // Bit 1: DTR (Data Terminal Ready). If this goes low, controller is deselected.
        if ((value & 0x0002) == 0) {
            currentState = State::Idle;
        }
    } 
    else if (address == 0x1F80104E) {
        baudRegister = value;
    }
}


void SIO::write32(uint32_t address, uint32_t value) {
    if (address == 0x1F801040) write8(address, value & 0xFF);
    else write16(address, value & 0xFFFF);
}


bool SIO::tick() {
    if (ackPending && ackDelay > 0) {
        ackDelay--;
        if (ackDelay == 0) {
            ackPending = false;
            // Set bit 9 (Interrupt Request) in the Status Register
            statusRegister |= 0x0200; 
            return true; // Tells the Bus to fire the actual CPU IRQ
        }
    }
    return false;
}




void SIO::processTransmit(uint8_t data) {
    // Controller must be selected (DTR bit 1 must be high)
    if ((controlRegister & 0x0002) == 0) {
        receiveFIFO = 0xFF; // No connection
        return;
    }

    bool queueAck = false;

    switch (currentState) {
        case State::Idle:
            if (data == 0x01) { // CPU is addressing a controller
                currentState = State::Command;
                receiveFIFO = 0xFF;
                queueAck = true;
            }
            // TODO: Handle 0x81 (Memory Card Addressing)
            break;
            
        case State::Addressing:
            // Unused in our simplified flow, included to silence -Wswitch
            break;

        case State::Command:
            if (data == 0x42) { // Read Controller Data
                currentState = State::Ready;
                receiveFIFO = controller.isAnalogMode() ? 0x73 : 0x41;
                expectedPayloadSize = controller.isAnalogMode() ? 6 : 2;
                payloadIndex = 0;
                
                // Force a live read of SDL state exactly when the CPU asks for it
                controller.updateState(); 
                
                queueAck = true;
            } else {
                // TODO: Handle Rumble Configuration commands (0x43, 0x4D)
                // Not a recognized command, abort
                currentState = State::Idle;
                receiveFIFO = 0xFF;
            }
            break;

        case State::Ready:
            currentState = State::Payload;
            receiveFIFO = 0x5A; // Controller says "Ready!"
            queueAck = true;
            break;

        case State::Payload: { 
            uint16_t controllerState = controller.getButtons();
            
            if (payloadIndex == 0) receiveFIFO = controllerState & 0xFF;
            else if (payloadIndex == 1) receiveFIFO = (controllerState >> 8) & 0xFF;
            else if (payloadIndex == 2) receiveFIFO = controller.getRightX();
            else if (payloadIndex == 3) receiveFIFO = controller.getRightY();
            else if (payloadIndex == 4) receiveFIFO = controller.getLeftX();
            else if (payloadIndex == 5) receiveFIFO = controller.getLeftY();

            payloadIndex++;

            // The PS1 controller does NOT send an ACK on the final byte of the payload
            if (payloadIndex < expectedPayloadSize) {
                queueAck = true;
            } 
            else {
                currentState = State::Done;
            }
            break;
        }

        case State::Done:
            receiveFIFO = 0xFF;
            break;
    }

    // Set Transmit Idle and Receive Not Empty flags in Status
    statusRegister |= 0x0005; 
    statusRegister |= 0x0002; 

    if (queueAck) {
        ackPending = true;
        // ~250-300 CPU cycles gives the hardware a realistic timeframe to pull the DSR line low
        ackDelay = 300; 
    }
}