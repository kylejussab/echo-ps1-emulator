#include <cstdlib>
#include <iomanip>
#include <iostream>
#include "CDROM.h"
#include "../core/Constants.h"


CDROM::CDROM() {
    registerIndex = 0;
    interruptEnable = 0;
    interruptFlag = 0;
    operatingMode = 0;
}


bool CDROM::mount(const std::string& cuePath) {
    return compactDisc.loadCue(cuePath);
}


uint8_t CDROM::read8(uint32_t address) {
    switch (address & 0x03) {
        case 0x00: { // Status Register
            uint8_t hardwareStatus = 0x18 | registerIndex;
            if (!responseQueue.empty()) hardwareStatus |= 0x20; 
            if (!dataQueue.empty()) hardwareStatus |= 0x40;     
            return hardwareStatus;
        }
        case 0x01: { // Response Queue
            if (responseQueue.empty()) return 0x00;
            uint8_t responseValue = responseQueue.front();
            responseQueue.pop();
            return responseValue;
        }
        case 0x02: { // Data Queue (Used by Direct Memory Access Channel 3)
            if (dataQueue.empty()) return 0x00;
            uint8_t dataValue = dataQueue.front();
            dataQueue.pop();
            return dataValue;
        }
        case 0x03: { // Interrupt Flag / Interrupt Enable Registers
            if (registerIndex == 0 || registerIndex == 2) return interruptEnable | 0xE0;
            if (registerIndex == 1 || registerIndex == 3) return interruptFlag | 0xE0;
            return 0;
        }
    }
    return 0;
}


void CDROM::write8(uint32_t address, uint8_t value) {
    switch (address & 0x03) {
        case 0x00: { // Index Register
            registerIndex = value & 0x03; 
            break;
        }
        case 0x01: { // Command Register (or Sound Volume based on Index)
            if (registerIndex == 0) executeCommand(value);
            else if (registerIndex == 3) pendingCompactDiscVolume[2] = value; 
            break;
        }
        case 0x02: { // Parameter Queue / Interrupt Enable (or Sound Volume based on Index)
            if (registerIndex == 0) parameterQueue.push(value);
            else if (registerIndex == 1) interruptEnable = value & 0x1F;
            else if (registerIndex == 2) pendingCompactDiscVolume[0] = value; 
            else if (registerIndex == 3) pendingCompactDiscVolume[3] = value; 
            break;
        }
        case 0x03: { // Request Register / Interrupt Flag (or Audio Apply based on Index)
            if (registerIndex == 0) {
                if (value & 0x80) {
                    if (dataQueue.empty() && hasSectorInBuffer) {
                        bool isSectorSize2340Bytes = (operatingMode & 0x20) != 0;
                        int bytesToRead = isSectorSize2340Bytes ? 2340 : 2048;
                        int startingOffset = isSectorSize2340Bytes ? 12 : 24;

                        for (int byteIndex = 0; byteIndex < bytesToRead; byteIndex++) {
                            dataQueue.push(sectorBuffer[startingOffset + byteIndex]);
                        }
                    }
                } 
                else {
                    while (!dataQueue.empty()) {
                        dataQueue.pop();
                    }
                }
            } 
            else if (registerIndex == 1) {
                interruptFlag &= ~(value & 0x1F);
                if (value & 0x40) {
                    while (!parameterQueue.empty()) parameterQueue.pop();
                }
            } 
            else if (registerIndex == 2) {
                pendingCompactDiscVolume[1] = value; 
            } 
            else if (registerIndex == 3) {
                if (value & 0x20) {
                    for (int volumeIndex = 0; volumeIndex < 4; volumeIndex++) {
                        activeCompactDiscVolume[volumeIndex] = pendingCompactDiscVolume[volumeIndex];
                    }
                }
            }
            break;
        }
    }
}


void CDROM::advanceClock(int cycles) {
    if (interruptQueue.empty() && !isReading) {
        return;
    }
    advanceClockSlowly(cycles);
}


bool CDROM::readAudioSample(int16_t& leftChannel, int16_t& rightChannel) {
    leftChannel = 0;
    rightChannel = 0;

    bool hasPendingSample = isPlayingAudio ? readCompactDiscDigitalAudioSample(leftChannel, rightChannel) : readExtendedArchitectureSample(leftChannel, rightChannel);
    if (!hasPendingSample) return false;

    if (isAudioMuted) {
        leftChannel = 0;
        rightChannel = 0;
        return true;
    }

    const int32_t inputLeftChannel = leftChannel;
    const int32_t inputRightChannel = rightChannel;
    
    leftChannel = clampAudioSample((inputLeftChannel * activeCompactDiscVolume[0] + inputRightChannel * activeCompactDiscVolume[3]) >> 7);
    rightChannel = clampAudioSample((inputRightChannel * activeCompactDiscVolume[2] + inputLeftChannel * activeCompactDiscVolume[1]) >> 7);
    
    return true;
}


bool CDROM::consumeDeliveredInterrupt() { 
    bool wasDelivered = interruptWasDelivered; 
    interruptWasDelivered = false; 
    return wasDelivered; 
}



// Privates
void CDROM::decodeExtendedArchitectureSector() {
    const uint8_t fileIndex = sectorBuffer[16];
    const uint8_t channelIndex = sectorBuffer[17];
    const uint8_t codingInformation = sectorBuffer[19];

    if ((operatingMode & 0x08) != 0 && (fileIndex != extendedArchitectureFilterFile || channelIndex != extendedArchitectureFilterChannel)) {
        return;
    }

    const bool isStereoFormat = (codingInformation & 0x01) != 0;
    const bool isEightBitFormat = (codingInformation & 0x10) != 0;
    extendedArchitectureHalfRate = (codingInformation & 0x04) != 0;

    const int blocksPerAudioGroup = isEightBitFormat ? 4 : 8;

    for (int groupIndex = 0; groupIndex < 18; groupIndex++) {
        const uint8_t* currentGroupData = &sectorBuffer[24 + groupIndex * 128];
        const uint8_t* audioHeaders = currentGroupData + 4; 
        const uint8_t* packedWords = currentGroupData + 16;

        int16_t decodedSamplesBuffer[2][28 * 8];
        int sampleCount[2] = { 0, 0 };

        for (int blockIndex = 0; blockIndex < blocksPerAudioGroup; blockIndex++) {
            const uint8_t blockHeader = audioHeaders[blockIndex];
            uint32_t shiftAmount = blockHeader & 0x0F;
            if (shiftAmount > 12) shiftAmount = 9;
            
            const uint32_t filterIndex = (blockHeader >> 4) & 0x03;
            const int outputChannelIndex = isStereoFormat ? (blockIndex & 1) : 0;
            int32_t* historyReference = extendedArchitectureHistory[outputChannelIndex];

            for (int wordIndex = 0; wordIndex < 28; wordIndex++) {
                const uint32_t packedWord = static_cast<uint32_t>(packedWords[wordIndex * 4])
                    | (static_cast<uint32_t>(packedWords[wordIndex * 4 + 1]) << 8)
                    | (static_cast<uint32_t>(packedWords[wordIndex * 4 + 2]) << 16)
                    | (static_cast<uint32_t>(packedWords[wordIndex * 4 + 3]) << 24);

                int32_t decodedSample;

                if (isEightBitFormat) {
                    decodedSample = static_cast<int16_t>(((packedWord >> (blockIndex * 8)) & 0xFF) << 8) >> shiftAmount;
                } 
                else {
                    decodedSample = static_cast<int16_t>(((packedWord >> (blockIndex * 4)) & 0xF) << 12) >> shiftAmount;
                }

                decodedSample += (historyReference[0] * EXTENDED_ARCHITECTURE_FILTER_POSITIVE[filterIndex] + historyReference[1] * EXTENDED_ARCHITECTURE_FILTER_NEGATIVE[filterIndex] + 32) >> 6;
                decodedSample = clampAudioSample(decodedSample);

                historyReference[1] = historyReference[0];
                historyReference[0] = decodedSample;
                decodedSamplesBuffer[outputChannelIndex][sampleCount[outputChannelIndex]++] = static_cast<int16_t>(decodedSample);
            }
        }

        for (int frameIndex = 0; frameIndex < sampleCount[0]; frameIndex++) {
            ExtendedArchitectureFrame processedFrame;
            processedFrame.leftChannel = decodedSamplesBuffer[0][frameIndex];
            processedFrame.rightChannel = isStereoFormat ? decodedSamplesBuffer[1][frameIndex] : decodedSamplesBuffer[0][frameIndex];
            extendedArchitectureFrames.push_back(processedFrame);
        }
    }

    constexpr size_t MAXIMUM_EXTENDED_ARCHITECTURE_FRAMES = 18900;

    while (extendedArchitectureFrames.size() > MAXIMUM_EXTENDED_ARCHITECTURE_FRAMES) {
        extendedArchitectureFrames.pop_front();
    }
}


bool CDROM::readExtendedArchitectureSample(int16_t& leftChannel, int16_t& rightChannel) {
    if (extendedArchitectureFrames.size() < 2) return false;

    const ExtendedArchitectureFrame& previousFrame = extendedArchitectureFrames[0];
    const ExtendedArchitectureFrame& nextFrame = extendedArchitectureFrames[1];
    
    leftChannel = static_cast<int16_t>(previousFrame.leftChannel + (nextFrame.leftChannel - previousFrame.leftChannel) * extendedArchitecturePhase / 7);
    rightChannel = static_cast<int16_t>(previousFrame.rightChannel + (nextFrame.rightChannel - previousFrame.rightChannel) * extendedArchitecturePhase / 7);

    extendedArchitecturePhase += extendedArchitectureHalfRate ? 3 : 6;

    while (extendedArchitecturePhase >= 7 && !extendedArchitectureFrames.empty()) {
        extendedArchitecturePhase -= 7;
        extendedArchitectureFrames.pop_front();
    }
    
    return true;
}


bool CDROM::readCompactDiscDigitalAudioSample(int16_t& leftChannel, int16_t& rightChannel) {
    if (compactDiscDigitalAudioFrame >= 588) {
        // AutoPause (Setmode bit 1): playback stops when it runs into the next track, and the drive reports end of data (INT4).
        // Behaviour written from memory of the hardware docs, unverified.
        if ((operatingMode & 0x02) != 0 && findTrackNumber(currentReadLogicalBlockAddress) != playingTrackNumber) {
            isPlayingAudio = false;
            queueInterrupt(0x04, 0, {0x02});
            return false;
        }

        if (!compactDisc.readSector(currentReadLogicalBlockAddress, compactDiscDigitalAudioBuffer)) {
            isPlayingAudio = false;
            queueInterrupt(0x04, 0, {0x02}); 
            return false;
        }

        uint32_t playedSector = currentReadLogicalBlockAddress;
        currentReadLogicalBlockAddress++;
        compactDiscDigitalAudioFrame = 0;

        // Report (Setmode bit 2): about 8 INT1 reports per second while playing, alternating between the absolute position
        // and the position relative to the track (seconds byte has bit 7 set). Format from memory, unverified.
        // Skipped while an earlier interrupt is still unacknowledged so reports never pile up in the queue.
        if ((operatingMode & 0x04) != 0) {
            uint32_t absoluteTotalFrames = playedSector + 150;
            uint32_t absoluteFrameInSecond = absoluteTotalFrames % 75;

            if (absoluteFrameInSecond % 10 == 0 && interruptQueue.empty() && (interruptFlag & 0x07) == 0) {
                bool sendRelativePosition = ((absoluteFrameInSecond / 10) & 1) != 0;

                uint8_t minutes;
                uint8_t seconds;
                uint8_t frames;

                if (sendRelativePosition) {
                    const auto* track = compactDisc.getTrack(playingTrackNumber);
                    uint32_t trackStart = (track != nullptr) ? static_cast<uint32_t>(track->startLogicalBlockAddress) : 0;
                    uint32_t relativeTotalFrames = (playedSector >= trackStart) ? (playedSector - trackStart) : 0;

                    minutes = decimalToBinaryCodedDecimal(relativeTotalFrames / (60 * 75));
                    seconds = decimalToBinaryCodedDecimal((relativeTotalFrames / 75) % 60) | 0x80;
                    frames = decimalToBinaryCodedDecimal(relativeTotalFrames % 75);
                }
                else {
                    minutes = decimalToBinaryCodedDecimal(absoluteTotalFrames / (60 * 75));
                    seconds = decimalToBinaryCodedDecimal((absoluteTotalFrames / 75) % 60);
                    frames = decimalToBinaryCodedDecimal(absoluteTotalFrames % 75);
                }

                // Status, track, index, position (3 bytes), peak level (left unreported)
                queueInterrupt(0x01, 0, {0x82, decimalToBinaryCodedDecimal(playingTrackNumber), 0x01, minutes, seconds, frames, 0x00, 0x00});
            }
        }
    }

    const uint8_t* currentAudioFrame = &compactDiscDigitalAudioBuffer[compactDiscDigitalAudioFrame * 4];
    leftChannel = static_cast<int16_t>(currentAudioFrame[0] | (currentAudioFrame[1] << 8));
    rightChannel = static_cast<int16_t>(currentAudioFrame[2] | (currentAudioFrame[3] << 8));
    
    compactDiscDigitalAudioFrame++;
    return true;
}


void CDROM::advanceClockSlowly(int cycles) {
    // Process pending hardware interrupts
    if (!interruptQueue.empty()) {
        if ((interruptFlag & 0x07) == 0) {
            interruptQueue.front().cyclesRemaining -= cycles;

            if (interruptQueue.front().cyclesRemaining <= 0) {
                PendingInterrupt activeInterrupt = interruptQueue.front();
                interruptQueue.pop();

                while (!responseQueue.empty()) {
                    responseQueue.pop();
                }
                
                for (uint8_t payloadByte : activeInterrupt.responsePayload) {
                    responseQueue.push(payloadByte);
                }
                
                interruptFlag = (interruptFlag & ~0x07) | activeInterrupt.interruptFlag;
                interruptWasDelivered = true; 
            }
        }
    }

    // Process continuous optical drive reading
    if (isReading) {
        readCycleTimer -= cycles;

        if (readCycleTimer <= 0) {
            readCycleTimer += calculateReadDelayCycles();

            if (compactDisc.readSector(currentReadLogicalBlockAddress, sectorBuffer)) {
                bool isExtendedArchitectureAudioSector = (operatingMode & 0x40) != 0 && (sectorBuffer[18] & 0x44) == 0x44;
                
                if (isExtendedArchitectureAudioSector) {
                    currentReadLogicalBlockAddress++;
                    decodeExtendedArchitectureSector();
                    return; 
                }

                bool isSectorSize2340Bytes = (operatingMode & 0x20) != 0;
                int bytesToRead = isSectorSize2340Bytes ? 2340 : 2048;

                int startingOffset = isSectorSize2340Bytes ? 12 : 24;

                while (!dataQueue.empty()) {
                    dataQueue.pop();
                }

                for (int byteIndex = 0; byteIndex < bytesToRead; byteIndex++) {
                    dataQueue.push(sectorBuffer[startingOffset + byteIndex]);
                }

                hasSectorInBuffer = true;
                currentReadLogicalBlockAddress++;

                queueInterrupt(0x01, 0, {0x22});
            } 
            else {
                queueInterrupt(0x05, 0, {0x22}); 
                isReading = false;
            }
        }
    }
}


void CDROM::executeCommand(uint8_t command) {
    uint8_t defaultHardwareStatus = isPlayingAudio ? 0x82 : (isReading ? 0x22 : 0x02);

    switch (command) {
        case 0x01: { // Getstat (Get Status)
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            break;
        }
        case 0x02: { // Setloc (Set Target Location)
            if (parameterQueue.size() >= 3) {
                uint8_t binaryCodedMinutes = parameterQueue.front(); parameterQueue.pop();
                uint8_t binaryCodedSeconds = parameterQueue.front(); parameterQueue.pop();
                uint8_t binaryCodedFrames = parameterQueue.front(); parameterQueue.pop();

                int decimalMinutes = binaryCodedDecimalToDecimal(binaryCodedMinutes);
                int decimalSeconds = binaryCodedDecimalToDecimal(binaryCodedSeconds);
                int decimalFrames = binaryCodedDecimalToDecimal(binaryCodedFrames);

                seekTargetLogicalBlockAddress = (decimalMinutes * 60 + decimalSeconds) * 75 + decimalFrames - 150;
                setLocationPending = true;

                queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            }
            break;
        }
        case 0x03: { // Play (Play Compact Disc Digital Audio)
            uint8_t trackNumber = 0;
            if (!parameterQueue.empty()) {
                trackNumber = binaryCodedDecimalToDecimal(parameterQueue.front());
            }

            playingTrackNumber = findTrackNumber(currentReadLogicalBlockAddress);

            if (trackNumber != 0 && compactDisc.getTrack(trackNumber) != nullptr) {
                currentReadLogicalBlockAddress = compactDisc.getTrack(trackNumber)->startLogicalBlockAddress;
                setLocationPending = false;
            } 
            else if (setLocationPending) {
                currentReadLogicalBlockAddress = seekTargetLogicalBlockAddress;
                setLocationPending = false;
            }
            
            isReading = false;
            isPlayingAudio = true;
            compactDiscDigitalAudioFrame = 588; 
            
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x82});
            break;
        }
        case 0x04: { // Forward (Fast Forward Audio)
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x82});
            break;
        }
        case 0x05: { // Backward (Fast Backward Audio)
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x82});
            break;
        }
        case 0x06: { // ReadN (Read Data Sectors With Retry)
            while (!dataQueue.empty()) {
                dataQueue.pop();
            }

            int seekCycles = 0;
            if (setLocationPending) {
                seekCycles = calculateSeekCycles(currentReadLogicalBlockAddress, seekTargetLogicalBlockAddress);
                currentReadLogicalBlockAddress = seekTargetLogicalBlockAddress;
                setLocationPending = false;
            }

            isReading = true;
            isPlayingAudio = false;
            readCycleTimer = calculateReadDelayCycles() + seekCycles;

            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x22}); 
            break;
        }
        case 0x07: { // MotorOn (Spin Up Drive Motor)
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            queueInterrupt(0x02, Hardware::DELAY_CDROM_MOTOR_SPINUP, {0x02});
            break;
        }
        case 0x08: { // Stop (Stop Drive Motor)
            bool wasHardwareActive = isReading || isPlayingAudio;
            isReading = false;
            isPlayingAudio = false;
            
            while (!dataQueue.empty()) {
                dataQueue.pop();
            }
            
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            queueInterrupt(0x02, wasHardwareActive ? Hardware::DELAY_CDROM_MOTOR_SPINUP : Hardware::DELAY_CDROM_PAUSE_WHEN_PAUSED, {0x00});
            break;
        }        
        case 0x09: { // Pause (Pause Reading or Playing)
            bool wasReadingPreviously = isReading;
            isPlayingAudio = false;

            while (!dataQueue.empty()) {
                dataQueue.pop();
            }

            isReading = false;

            int secondResponseDelayTime = Hardware::DELAY_CDROM_PAUSE_WHEN_PAUSED;
            
            if (wasReadingPreviously) {
                secondResponseDelayTime = isDoubleSpeed() ? Hardware::DELAY_CDROM_PAUSE_DOUBLE_SPEED : Hardware::DELAY_CDROM_PAUSE_SINGLE_SPEED;
            }

            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x02});
            queueInterrupt(0x02, secondResponseDelayTime, {0x02});
            break;
        }
        case 0x0A: { // Init (Initialize Drive State)
            operatingMode = 0x20;
            isPlayingAudio = false;
            isAudioMuted = false;
            extendedArchitectureFrames.clear();
            extendedArchitecturePhase = 0;
            isReading = false;

            // Init resets the drive: a speed change (0.7 s) plus the head returning to the start of the disc.
            // Charged to the next seek or read, so the BIOS doesn't time out and re-send Init.
            speedChangePending = false;
            int seekBackCycles = calculateSeekCycles(currentReadLogicalBlockAddress, 0);
            pendingInitializationCycles = static_cast<int>(0.70 * Hardware::CPU_CLOCK_SPEED_HERTZ) + seekBackCycles;
            currentReadLogicalBlockAddress = 0;

            queueInterrupt(0x03, Hardware::DELAY_CDROM_INIT_ACK, {defaultHardwareStatus});
            queueInterrupt(0x02, Hardware::DELAY_CDROM_MOTOR_SPINUP, {0x02});
            break;
        }
        case 0x0B: { // Mute (Mute Audio Output)
            isAudioMuted = true;
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            break;
        }
        case 0x0C: { // Demute (Unmute Audio Output)
            isAudioMuted = false;
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            break;
        }
        case 0x0D: { // Setfilter (Set Extended Architecture Audio Filter)
            if (parameterQueue.size() >= 2) {
                extendedArchitectureFilterFile = parameterQueue.front(); parameterQueue.pop();
                extendedArchitectureFilterChannel = parameterQueue.front(); parameterQueue.pop();
            }

            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            break;
        }
        case 0x0E: { // Setmode (Set Drive Operating Mode)
            if (!parameterQueue.empty()) {
                uint8_t newMode = parameterQueue.front();
                parameterQueue.pop();

                // Switching between single and double speed (bit 7) on an idle drive costs a one-time penalty
                if (((newMode ^ operatingMode) & 0x80) && !isReading) speedChangePending = true;

                operatingMode = newMode;
            }

            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            break;
        }
        case 0x0F: { // Getparam (Get Drive Operating Mode Parameters)
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus, operatingMode, 0x00, extendedArchitectureFilterFile, extendedArchitectureFilterChannel});
            break;
        }
        case 0x10: { // GetlocL (Get Logical Location Data Sector Header)
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK , {
                sectorBuffer[12], sectorBuffer[13], sectorBuffer[14], sectorBuffer[15],  // MM SS FF mode
                sectorBuffer[16], sectorBuffer[17], sectorBuffer[18], sectorBuffer[19]   // file channel submode coding
            });
            break;
        }
        case 0x11: { // GetlocP (Get Physical Location Subchannel Q)
            uint32_t absoluteTotalFrames = currentReadLogicalBlockAddress + 150;
            uint8_t absoluteMinutes = decimalToBinaryCodedDecimal(absoluteTotalFrames / (60 * 75));
            uint8_t absoluteSeconds = decimalToBinaryCodedDecimal((absoluteTotalFrames / 75) % 60);
            uint8_t absoluteFrames = decimalToBinaryCodedDecimal(absoluteTotalFrames % 75);

            uint32_t relativeTotalFrames = currentReadLogicalBlockAddress; 
            uint8_t trackMinutes = decimalToBinaryCodedDecimal(relativeTotalFrames / (60 * 75));
            uint8_t trackSeconds = decimalToBinaryCodedDecimal((relativeTotalFrames / 75) % 60);
            uint8_t trackFrames = decimalToBinaryCodedDecimal(relativeTotalFrames % 75);

            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK , {
                decimalToBinaryCodedDecimal(1), 
                decimalToBinaryCodedDecimal(1), 
                trackMinutes, trackSeconds, trackFrames,
                absoluteMinutes, absoluteSeconds, absoluteFrames
            });

            break;
        }
        case 0x12: { // Setsession (Set Active Disc Session)
            if (parameterQueue.empty()) {
                queueInterrupt(0x05, Hardware::DELAY_CDROM_ACK, {static_cast<uint8_t>(defaultHardwareStatus | 0x01), 0x20}); 
                break;
            }
            
            uint8_t targetSession = parameterQueue.front();
            parameterQueue.pop();
            
            if (targetSession == 0) {
                queueInterrupt(0x05, Hardware::DELAY_CDROM_ACK, {static_cast<uint8_t>(defaultHardwareStatus | 0x01), 0x10}); 
                break;
            }
            
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            queueInterrupt(0x02, Hardware::DELAY_CDROM_MOTOR_SPINUP, {defaultHardwareStatus});
            break;
        }
        case 0x13: { // GetTN (Get Track Numbers First and Last)
            if (!compactDisc.isLoaded()) {
                queueInterrupt(0x05, Hardware::DELAY_CDROM_ACK, {0x08, 0x40});
                break;
            }

            uint8_t firstTrackNumber = decimalToBinaryCodedDecimal(1);
            uint8_t lastTrackNumber = decimalToBinaryCodedDecimal(static_cast<uint8_t>(compactDisc.getTrackCount()));
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus, firstTrackNumber, lastTrackNumber});
            break;
        }
        case 0x14: { // GetTD (Get Track Duration or Start Address)
            if (parameterQueue.empty()) break;

            uint8_t targetTrackNumber = binaryCodedDecimalToDecimal(parameterQueue.front());
            parameterQueue.pop();

            uint32_t absoluteTotalFrames = 0;
            if (targetTrackNumber == 0) {
                absoluteTotalFrames = compactDisc.getTotalSectors() + 150;
            } 
            else {
                const Disc::Track* trackReference = compactDisc.getTrack(targetTrackNumber);
                if (trackReference == nullptr) {
                    queueInterrupt(0x05, Hardware::DELAY_CDROM_ACK, {0x03, 0x10});
                    break;
                }
                absoluteTotalFrames = trackReference->startLogicalBlockAddress + 150;
            }

            uint32_t calculatedMinutes = absoluteTotalFrames / (60 * 75);
            uint32_t calculatedSeconds = (absoluteTotalFrames / 75) % 60;
            
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {
                defaultHardwareStatus, 
                decimalToBinaryCodedDecimal(static_cast<uint8_t>(calculatedMinutes)), 
                decimalToBinaryCodedDecimal(static_cast<uint8_t>(calculatedSeconds))
            });
            break;
        }
        case 0x15: { // SeekL (Seek to Logical Data Sector)
            int seekCycles = calculateSeekCycles(currentReadLogicalBlockAddress, seekTargetLogicalBlockAddress);
            currentReadLogicalBlockAddress = seekTargetLogicalBlockAddress;
            setLocationPending = false;
            isReading = false;
            isPlayingAudio = false;

            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x42});
            queueInterrupt(0x02, seekCycles, {0x02});
            break;
        }
        case 0x16: { // SeekP (Seek to Physical Audio Track)
            int seekCycles = calculateSeekCycles(currentReadLogicalBlockAddress, seekTargetLogicalBlockAddress);
            currentReadLogicalBlockAddress = seekTargetLogicalBlockAddress;
            setLocationPending = false;
            isReading = false;
            isPlayingAudio = false;

            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x42});
            queueInterrupt(0x02, seekCycles, {0x02});
            break;
        }
        case 0x19: { // Test (Execute Internal Drive Tests)
            if (parameterQueue.empty()) break;
            uint8_t subCommand = parameterQueue.front(); parameterQueue.pop();
            
            if (subCommand == 0x20) {
                queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x94, 0x09, 0x19, 0xC0}); 
            } 
            else if (subCommand == 0x22) {
                queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus, 'S', 'C', 'E', 'A'});
            } 
            else {
                queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            }
            break;
        }
        case 0x1A: { // GetID (Get Disc Identification and Authenticate)
            if (!compactDisc.isLoaded()) {
                queueInterrupt(0x05, Hardware::DELAY_CDROM_ACK, {0x08, 0x40, 0, 0}); 
            } 
            else {
                queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
                queueInterrupt(0x02, Hardware::DELAY_CDROM_GETID_SECOND_RESPONSE, {0x02, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'A'});
            }
            break;
        }
        case 0x1B: { // ReadS (Read Data Sectors Without Retry)
            while (!dataQueue.empty()) {
                dataQueue.pop();
            }

            int seekCycles = 0;
            if (setLocationPending) {
                seekCycles = calculateSeekCycles(currentReadLogicalBlockAddress, seekTargetLogicalBlockAddress);
                currentReadLogicalBlockAddress = seekTargetLogicalBlockAddress;
                setLocationPending = false;
            }

            isReading = true;
            isPlayingAudio = false;
            readCycleTimer = calculateReadDelayCycles() + seekCycles;

            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x22}); 
            break;
        }
        case 0x1E: { // ReadTOC (Read Table of Contents)
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            queueInterrupt(0x02, Hardware::DELAY_CDROM_MOTOR_SPINUP, {defaultHardwareStatus});
            break;
        }
        default: {
            std::cout << "WARNING: CDROM: Unhandled command 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)command << std::dec << "\n";
            queueInterrupt(0x05, Hardware::DELAY_CDROM_ACK, {static_cast<uint8_t>(defaultHardwareStatus | 0x01), 0x40});
            break;
        }
    }

    while (!parameterQueue.empty()) {
        parameterQueue.pop();
    }
}


int CDROM::calculateSeekCycles(uint32_t fromLogicalBlockAddress, uint32_t toLogicalBlockAddress) {
    const int initializationExtraCycles = pendingInitializationCycles;
    pendingInitializationCycles = 0;

    uint32_t distance = fromLogicalBlockAddress > toLogicalBlockAddress 
        ? fromLogicalBlockAddress - toLogicalBlockAddress 
        : toLogicalBlockAddress - fromLogicalBlockAddress;
        
    double seconds;

    if (distance < 32) {
        seconds = 0.0139 + (distance > 2 ? (distance - 2) / 150.0 : 0.0);
    }     
    else if (distance < 300) {
        seconds = 0.051;
    }
    else if (distance < 3000) {
        seconds = 0.100;
    }
    else {
        seconds = 0.328 + distance * 0.00000179;
    } 

    int cycles = static_cast<int>(seconds * Hardware::CPU_CLOCK_SPEED_HERTZ) + initializationExtraCycles;
    
    if (speedChangePending) { 
        cycles += static_cast<int>(0.6 * Hardware::CPU_CLOCK_SPEED_HERTZ); 
        speedChangePending = false; 
    }
    
    return cycles;
}


uint8_t CDROM::findTrackNumber(uint32_t logicalBlockAddress) {
    uint8_t foundTrackNumber = 0;
    for (uint8_t trackNumber = 1; trackNumber <= 99; trackNumber++) {
        const auto* track = compactDisc.getTrack(trackNumber);
        if (track == nullptr) break;
        if (static_cast<uint32_t>(track->startLogicalBlockAddress) <= logicalBlockAddress) foundTrackNumber = trackNumber;
    }
    return foundTrackNumber;
}
