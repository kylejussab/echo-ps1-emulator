#include "CDROM.h"
#include <iostream>

CDROM::CDROM() {
    index = 0;
    interruptEnable = 0;
    interruptFlag = 0;
}


uint8_t CDROM::read8(uint32_t address) {
    switch (address & 0x03) { 
        case 0x00: { // 0x1F801800 - Status
            // Bit 3: Parameter FIFO empty (1)
            // Bit 4: Parameter FIFO not full (1)
            uint8_t status = 0x18; 
            
            // Bit 5: Response FIFO not empty (1)
            if (!responseFIFO.empty()) {
                status |= 0x20; 
            }
            return status;
        }
        case 0x01: { // 0x1F801801 - Response FIFO
            if (responseFIFO.empty()) {
                return 0x00; 
            }
            uint8_t response = responseFIFO.front();
            responseFIFO.pop();
            return response;
        }
        case 0x02: { // 0x1F801802 - Data FIFO
            // TODO: Wire up actual sector data reading from ISO
            return 0x00; 
        }
        case 0x03: { // 0x1F801803 - Interrupt Flag / Enable
            if (index == 0 || index == 2) {
                return interruptEnable | 0xE0; // The top 3 bits are almost always 1
            } 
            else if (index == 1 || index == 3) {
                return interruptFlag | 0xE0;
            }
            return 0;
        }
    }
    return 0;
}


void CDROM::write8(uint32_t address, uint8_t value) {
    switch (address & 0x03) {
        case 0x00: { // 0x1F801800 - Index
            index = value & 0x03;
            break;
        }   
        case 0x01: { // 0x1F801801 - Command
            if (index == 0) {
                executeCommand(value);
            }
            break;
        }
        case 0x02: { // 0x1F801802 
            if (index == 0) { // Parameter FIFO
                parameterFIFO.push(value);
            } 
            else if (index == 1) { // Interrupt Enable Register
                interruptEnable = value & 0x1F;
            }
            break;
        }
        case 0x03: { // 0x1F801803 
            if (index == 1) { // Interrupt Flag Register
                // Acknowledge interrupt (writing a 1 clears the bit)
                interruptFlag &= ~(value & 0x1F);
            }
            break;
        }
    }
}


bool CDROM::checkInterrupt() {
    // IRQ2 fires if any matching bit between Flag and Enable is set
    return (interruptFlag & interruptEnable & 0x1F) != 0;
}



void CDROM::pushResponse(uint8_t value) {
    responseFIFO.push(value);
}


void CDROM::executeCommand(uint8_t command) {
    // Clear response FIFO before processing a new command
    while (!responseFIFO.empty()) responseFIFO.pop();

    switch (command) {
        case 0x01: { // GetStat
            pushResponse(0x02); // Return 0x02 (Motor On, Drive Idle)
            interruptFlag = (interruptFlag & ~0x07) | 0x03; // INT3 (First Response)
            break;
        }
        case 0x19: { // Test
            if (parameterFIFO.empty()) break;
            
            uint8_t subfunction = parameterFIFO.front();
            parameterFIFO.pop();

            if (subfunction == 0x20) { // Get BIOS Date/Version
                pushResponse(0x94);
                pushResponse(0x09);
                pushResponse(0x19);
                pushResponse(0xC0);
                interruptFlag = (interruptFlag & ~0x07) | 0x03; // INT3
            } 
            else {
                std::cout << "CDROM: Ignored Test subfunction 0x" << std::hex << (int)subfunction << std::endl;
                interruptFlag = (interruptFlag & ~0x07) | 0x03; // Return INT3 just to free the BIOS
            }
            break;
        }
        // TODO: Implement Setloc, ReadN, Pause, and other essential ISO commands
        default: {
            std::cout << "CDROM: Unhandled command 0x" << std::hex << (int)command << std::endl;
            // Unrecognized commands usually return INT5 (Error)
            interruptFlag = (interruptFlag & ~0x07) | 0x05; 
            break;
        }
    }
    
    // The hardware discards unread parameters once a command executes
    while (!parameterFIFO.empty()) parameterFIFO.pop();
}
