#include "CDROM.h"
#include <iostream>
#include "../Constants.h"

CDROM::CDROM() {
    index = 0;
    interruptEnable = 0;
    interruptFlag = 0;
}

bool CDROM::mount(const std::string& cuePath) {
    return disc.loadCue(cuePath);
}

void CDROM::tick(int cycles) {
    // 1. Process pending interrupts
    if (!interruptQueue.empty()) {
        // The hardware can only hold one active interrupt at a time.
        // If the CPU hasn't acknowledged the current one, pause the queue.
        if ((interruptFlag & 0x07) == 0) {
            interruptQueue.front().cyclesRemaining -= cycles;
            
            if (interruptQueue.front().cyclesRemaining <= 0) {
                PendingInterrupt active = interruptQueue.front();
                interruptQueue.pop();

                while (!responseFIFO.empty()) responseFIFO.pop();
                for (uint8_t byte : active.responsePayload) {
                    responseFIFO.push(byte);
                }
                
                interruptFlag = (interruptFlag & ~0x07) | active.flag;
            }
        }
    }

    // 2. Process continuous reading
    if (isReading) {
        readCycleTimer -= cycles;
        if (readCycleTimer <= 0) {
            readCycleTimer += Hardware::DELAY_CDROM_READ;

            if (disc.readSector(currentReadLBA, sectorBuffer)) {
                while (!dataFIFO.empty()) dataFIFO.pop();

                // Bit 5 of mode register determines the sector size
                bool isSectorSize2340 = (mode & 0x20) != 0;
                int bytesToRead = isSectorSize2340 ? 2340 : 2048;
                int startOffset = isSectorSize2340 ? 12 : 24;

                for (int i = 0; i < bytesToRead; i++) {
                    dataFIFO.push(sectorBuffer[startOffset + i]);
                }
                
                currentReadLBA++;
                
                // Trigger INT1 (Data Ready)
                queueInterrupt(0x01, 0, {0x22}); 
            }
            else {
                // Out of bounds / Read Error
                queueInterrupt(0x05, 0, {0x22}); // INT5 Error
                isReading = false;
            }
        }
    }
}

void CDROM::queueInterrupt(uint8_t flag, int delayCycles, std::vector<uint8_t> response) {
    interruptQueue.push({delayCycles, flag, response});
}

// read8 and write8 remain mostly identical, with one crucial addition to Data FIFO:
uint8_t CDROM::read8(uint32_t address) {
    switch (address & 0x03) { 
        case 0x00: { // 0x1F801800 - Status
            uint8_t status = 0x18; 
            if (!responseFIFO.empty()) status |= 0x20; // Response FIFO not empty
            if (!dataFIFO.empty()) status |= 0x40;     // Data FIFO not empty
            return status;
        }
        case 0x01: { // 0x1F801801 - Response FIFO
            if (responseFIFO.empty()) return 0x00; 
            uint8_t response = responseFIFO.front();
            responseFIFO.pop();
            return response;
        }
        case 0x02: { // 0x1F801802 - Data FIFO (Used by DMA Channel 3)
            if (dataFIFO.empty()) return 0x00;
            uint8_t data = dataFIFO.front();
            dataFIFO.pop();
            return data;
        }
        case 0x03: { // 0x1F801803 - Interrupt Flag / Enable
            if (index == 0 || index == 2) return interruptEnable | 0xE0;
            if (index == 1 || index == 3) return interruptFlag | 0xE0;
            return 0;
        }
    }
    return 0;
}

void CDROM::write8(uint32_t address, uint8_t value) {
    switch (address & 0x03) {
        case 0x00: index = value & 0x03; break;
        case 0x01: if (index == 0) executeCommand(value); break;
        case 0x02: 
            if (index == 0) parameterFIFO.push(value);
            else if (index == 1) interruptEnable = value & 0x1F;
            break;
        case 0x03: 
            if (index == 1) {
                // Writing 1 clears the corresponding interrupt flag bit
                interruptFlag &= ~(value & 0x1F);
                // If bit 6 is written, clear the parameter FIFO
                if (value & 0x40) while (!parameterFIFO.empty()) parameterFIFO.pop();
            }
            break;
    }
}

bool CDROM::checkInterrupt() {
    return (interruptFlag & interruptEnable & 0x1F) != 0;
}

void CDROM::executeCommand(uint8_t command) {
    // Basic status: 0x02 = Motor On, 0x22 = Motor On + Reading
    uint8_t defaultStat = isReading ? 0x22 : 0x02;

    switch (command) {
        case 0x01: { // GetStat
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultStat});
            break;
        }
        case 0x02: { // Setloc
            if (parameterFIFO.size() >= 3) {
                uint8_t mm = parameterFIFO.front(); parameterFIFO.pop();
                uint8_t ss = parameterFIFO.front(); parameterFIFO.pop();
                uint8_t ff = parameterFIFO.front(); parameterFIFO.pop();
                
                // Convert BCD to LBA
                int minutes = bcdToDec(mm);
                int seconds = bcdToDec(ss);
                int frames = bcdToDec(ff);
                
                // Standard 2-second offset applied by the PS1 hardware
                seekTargetLBA = (minutes * 60 + seconds) * 75 + frames - 150;
                
                queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultStat});
            }
            break;
        }
        case 0x06: { // ReadN
            currentReadLBA = seekTargetLBA;
            isReading = true;
            readCycleTimer = Hardware::DELAY_CDROM_READ;
            
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x22}); // Acknowledge with reading flag
            break;
        }
        case 0x07: { // MotorOn
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultStat});
            queueInterrupt(0x02, Hardware::DELAY_CDROM_ACK * 2, {0x02});
            break;
        }
        case 0x09: { // Pause
            isReading = false;
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x02}); 
            // Pause also queues a second INT2 after the motor stops
            queueInterrupt(0x02, Hardware::DELAY_CDROM_ACK * 2, {0x02});
            break;
        }
        case 0x0A: { // Init
            isReading = false;
            // INT3 acknowledges the command
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultStat});
            
            // INT2 fires when the drive initialization is complete
            queueInterrupt(0x02, Hardware::DELAY_CDROM_ACK * 3, {0x02});
            break;
        }
        case 0x0B: // Mute
        case 0x0C: // Demute
        case 0x0D: { // Setfilter (ignored for now)
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultStat});
            break;
        }
        case 0x0E: { // Setmode
            if (!parameterFIFO.empty()) {
                mode = parameterFIFO.front(); // Save it to the class member
                parameterFIFO.pop();
            }
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultStat});
            break;
        }
        case 0x1A: { // GetID (Disc Authentication)
            if (!disc.isLoaded()) {
                queueInterrupt(0x05, Hardware::DELAY_CDROM_ACK, {0x08, 0x40, 0, 0}); // No Disc error
            } 
            else {
                queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultStat});
                // INT2 follows immediately with the 8-byte license string identifying it as a legit PS1 disc
                queueInterrupt(0x02, Hardware::DELAY_CDROM_ACK + 1000, {0x02, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'A'});
            }
            break;
        }
        case 0x15: { // SeekL
            // Set the state to Seeking (0x42) during the INT3 acknowledgment
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x42}); 
            
            // INT2 fires when the laser head arrives. The state returns to Motor On (0x02)
            queueInterrupt(0x02, Hardware::DELAY_CDROM_ACK * 2, {0x02});
            break;
        }
        case 0x19: { // Test
            if (parameterFIFO.empty()) break;
            uint8_t sub = parameterFIFO.front(); parameterFIFO.pop();
            if (sub == 0x20) {
                queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x94, 0x09, 0x19, 0xC0}); // BIOS Date
            } else {
                queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultStat}); 
            }
            break;
        }
        default: {
            std::cout << "CDROM: Unhandled command 0x" << std::hex << (int)command << std::endl;
            queueInterrupt(0x05, Hardware::DELAY_CDROM_ACK, {0x11}); // INT5 Error
            break;
        }
    }
    
    while (!parameterFIFO.empty()) parameterFIFO.pop();
}
