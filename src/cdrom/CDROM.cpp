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
            /// @todo (accuracy): HSTS bits are mostly hardcoded. PRMEMPT (bit 3) must be 1 only while the parameter FIFO is empty and PRMWRDY (bit 4) 1 while it holds fewer than 16 bytes; both are always 1 here. ADPBUSY (bit 2, 1 = XA-ADPCM playing) and BUSYSTS (bit 7, HC05 busy accepting a command) are never set. See "HSTS".
            uint8_t hardwareStatus = 0x18 | registerIndex;
            if (!responseQueue.empty()) hardwareStatus |= 0x20; 
            if (!dataQueue.empty()) hardwareStatus |= 0x40;     
            return hardwareStatus;
        }
        case 0x01: { // Response Queue
            /// @todo (accuracy): the result FIFO is 16 bytes. Reading past the last response byte returns 00h padding up to 16 bytes, then wraps back to the first response byte (same bytes until a new response arrives). Here the queue pops to empty and returns 0.
            if (responseQueue.empty()) return 0x00;
            uint8_t responseValue = responseQueue.front();
            responseQueue.pop();
            return responseValue;
        }
        case 0x02: { // Data Queue (Used by Direct Memory Access Channel 3)
            /// @todo (accuracy): reading past the end of the sector must repeat the byte at index [0x800-8] (data-only) or [0x924-4] (whole sector); this returns 0x00 once the queue is empty. See "RDDATA".
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
            /// @todo (accuracy): commands execute the instant they are written. On hardware BUSYSTS goes high while the command is accepted, and the HC05 only executes a command when no INT is pending; a command written while an INT is unacknowledged waits until it is acknowledged. See "BUSYSTS flag" and "First Response (INT3)".
            if (registerIndex == 0) executeCommand(value);
            /// @todo (unimplemented): bank 1 WRDATA (sound map upload) and bank 2 CI (sound map coding info) writes to 1F801801h are ignored. See "Sound Map Flowchart".
            else if (registerIndex == 3) pendingCompactDiscVolume[2] = value; 
            break;
        }
        case 0x02: { // Parameter Queue / Interrupt Enable (or Sound Volume based on Index)
            /// @todo (accuracy): the parameter FIFO holds 16 bytes; this queue is unbounded.
            if (registerIndex == 0) parameterQueue.push(value);
            else if (registerIndex == 1) interruptEnable = value & 0x1F;
            else if (registerIndex == 2) pendingCompactDiscVolume[0] = value; 
            else if (registerIndex == 3) pendingCompactDiscVolume[3] = value; 
            break;
        }
        case 0x03: { // Request Register / Interrupt Flag (or Audio Apply based on Index)
            /// @todo (unimplemented): HCHPCTL SMEN (bit 5, sound map enable) and BFWR (bit 6, buffer write request) are ignored. Sound map would also need BFEMPT/BFWRDY (HINTSTS bits 3-4).
            if (registerIndex == 0) {
                if (value & 0x80) {
                    /// @todo (accuracy): sector data is pushed into dataQueue as soon as the sector arrives (L355), so DRQSTS (HSTS bit 6) is set before software sets BFRD. The doc's flow is INT1 -> BFRD=1 -> DRQSTS set -> read. Also, cdromdrive.md doesn't document BFRD=0 flushing the data FIFO (L82-86), so that behaviour is unverified against this source.
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
                /// @todo (accuracy): acknowledging INT flags should also drain the result FIFO and then release a command that was waiting (see BUSYSTS). Bit 5 SMADPCLR (clear sound map buffer / stop playback) and bit 7 CHPRST (reset decoder) are unimplemented.
                interruptFlag &= ~(value & 0x1F);
                if (value & 0x40) {
                    while (!parameterQueue.empty()) parameterQueue.pop();
                }
            } 
            else if (registerIndex == 2) {
                pendingCompactDiscVolume[1] = value; 
            } 
            else if (registerIndex == 3) {
                /// @todo (unimplemented): ADPCTL bit 0 ADPMUTE (mute XA-ADPCM) is ignored; only CHNGATV (bit 5) is handled. isAudioMuted is only driven by the Mute/Demute commands.
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
    
    /// @todo (accuracy): hardware saturation only works up to double volume (e.g. FFh,0,FFh,0 or 80h,80h,80h,80h); beyond that (e.g. FFh,FFh,FFh,FFh mono) it does not work properly. This clamps every case.
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

    /// @todo (unimplemented): coding-info bit 6 (emphasis) is ignored. cdromdrive.md only lists the bit, not the filter itself.
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


/// @todo (unimplemented): CD-DA ignores Setmode bit 7. During Play, double speed gives a fast-forward effect with audible output.
bool CDROM::readCompactDiscDigitalAudioSample(int16_t& leftChannel, int16_t& rightChannel) {
    if (compactDiscDigitalAudioFrame >= 588) {
        // AutoPause (Setmode bit 1): playback stops when it runs into the next track, and the drive reports end of data (INT4).
        // Behaviour written from memory of the hardware docs, unverified.
        /// @todo (accuracy): after AutoPause the head should stay at the END of the old track (a Play with no new Seek/Setloc must instantly pause again). Here currentReadLogicalBlockAddress already points at the next track. Real detection uses SubQ track numbers, which switch inside the next track's INDEX 00 pregap; Disc doesn't parse INDEX 00/PREGAP (Disc.cpp L78).
        if ((operatingMode & 0x02) != 0 && findTrackNumber(currentReadLogicalBlockAddress) != playingTrackNumber) {
            isPlayingAudio = false;
            queueInterrupt(0x04, 0, {0x02});
            return false;
        }

        /// @todo (accuracy): Play on a data track must output silence ("all sectors from data tracks are treated as 00"); this plays raw Mode2 bytes as PCM. Check Disc::Track::type.
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
                /// @todo (unimplemented): the last two Report bytes (peaklo/peakhi) are the CXD2510Q peak value (15-bit level + L/R flag); always 0 here.
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


/// @todo (accuracy): the doc has no INT queue. Delivery is flag-based: one pending INT2 flag and one INT1 flag (at most one undelivered INT1), and INT3 is generated immediately when a command executes (which needs no INT pending). This unbounded FIFO lets several stale INT1s pile up in front of a later command's INT3. See "Mainloop/Responses".
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
                /// @todo (accuracy): sector routing differs from "Data/ADPCM Sector Filtering/Delivery": (1) a sector must be MODE2 and not CD-DA to go to the ADPCM decoder (only submode bits 2+6 are checked here); (2) with XA-Filter (Setmode bit 3) on and XA (bit 6) off, audio+realtime sectors are rejected from data delivery too, here they get INT1; (3) the two data-delivery attempts (first ignoring file/channel, second applying the filter) aren't modelled.
                bool isExtendedArchitectureAudioSector = (operatingMode & 0x40) != 0 && (sectorBuffer[18] & 0x44) == 0x44;
                
                if (isExtendedArchitectureAudioSector) {
                    currentReadLogicalBlockAddress++;
                    decodeExtendedArchitectureSector();
                    return; 
                }

                /// @todo (unimplemented): ReadN/ReadS on an Audio track needs Setmode bit 0 (CDDA), otherwise INT5 error 40h. Region-mismatched discs give 40h too unless CDDA is set, and unlicensed discs need the drive unlocked (commands 50h-57h).
                bool isSectorSize2340Bytes = (operatingMode & 0x20) != 0;
                int bytesToRead = isSectorSize2340Bytes ? 2340 : 2048;

                int startingOffset = isSectorSize2340Bytes ? 12 : 24;

                /// @todo (accuracy): INT1 + data may only be delivered when no other INT is pending, otherwise the sector is skipped. The hardware keeps just the oldest undelivered sector plus the newest (see "Sector Buffer" test cases: INT1 jumps from sector 1 to 6, 9, ...). Here every sector queues an INT1 and overwrites dataQueue.
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
                /// @todo (accuracy): INT5 has the shape (stat with error bit set, error byte); this sends a single byte 22h. The doc marks the read-error response as unknown, but one byte matches no other INT5.
                queueInterrupt(0x05, 0, {0x22}); 
                isReading = false;
            }
        }
    }
}


/// @todo (accuracy): error handling is mostly absent. A wrong parameter count answers INT5(stat|1, 20h) (Setloc, Setfilter, Setmode, GetTD, SetSession, Test...); here several silently ignore it or send no response at all (Setloc L382, GetTD L600, Test L651). With no disc, commands 02h-09h, 0Bh-0Dh, 10h-16h, 1Ah, 1Bh and 1Dh answer INT5(stat|1, 80h); only GetID and GetTN check isLoaded().
void CDROM::executeCommand(uint8_t command) {
    /// @todo (unimplemented): stat only reflects isPlayingAudio/isReading. Not tracked: Spindle Motor bit 1 (stays 1 after Stop and with no disc), Seek bit 6 (only ONE of Seek/Play/Read may be set; Read/Play appear after the seek completes, which Gran Turismo 1 relies on), ShellOpen bit 4 (cleared by Nop), IdError bit 3, SeekError bit 2, and the INT5 08h lid-open event.
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

                /// @todo (accuracy): amm:ass:asect below 00:02:00 underflows the unsigned LBA (minus 150). Also validate BCD: ass < 60h, asect < 75h, valid packed BCD, else INT5(stat|1, 10h).
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

            /// @todo (accuracy): BUG: playingTrackNumber is computed from the position BEFORE the Play target is applied (L407/L411). Playing a track from elsewhere leaves the old track number, so AutoPause (L246) fires instantly and Report (L292) shows the wrong track. Compute it after currentReadLogicalBlockAddress is final.
            playingTrackNumber = findTrackNumber(currentReadLogicalBlockAddress);

            /// @todo (accuracy): track parameters N+1..99h must restart the beginning of the CURRENT track; here they fall through to Setloc/current position. Play also adds no seek time (ReadN uses calculateSeekCycles), and the first response is hardcoded 82h although only the Seek bit (bit 6) may be set until the seek completes.
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
        /// @todo (unimplemented): Forward/Backward are stubs. Needed: skipping sectors, only valid once Play has started (otherwise INT5(stat+1, 80h)), Report INT1s, Backward switching to Play at the start of track 1, and Forward stopping with INT4 at the end of the last track.
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

            /// @todo (accuracy): ReadN/ReadS with no unprocessed Setloc after a Pause must resume at the most recently received sector (returned once more). Here reading continues at currentReadLogicalBlockAddress, which is already the next sector.
            int seekCycles = 0;
            if (setLocationPending) {
                seekCycles = calculateSeekCycles(currentReadLogicalBlockAddress, seekTargetLogicalBlockAddress);
                currentReadLogicalBlockAddress = seekTargetLogicalBlockAddress;
                setLocationPending = false;
            }

            isReading = true;
            isPlayingAudio = false;
            readCycleTimer = calculateReadDelayCycles() + seekCycles;

            /// @todo (accuracy): the first response is hardcoded 22h, but during the seek only the Seek bit may be set (42h); Read gets set after the seek completes. No disc must give INT5(stat|1, 80h).
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x22}); 
            break;
        }
        case 0x07: { // MotorOn (Spin Up Drive Motor)
            /// @todo (accuracy): MotorOn only works if the motor was off; otherwise INT5(stat, 20h) with no second response (and it does NOT pause reading/playing). No disc: INT5(stat, 80h). Needs a motor-on flag. The INT2 delay (DELAY_CDROM_MOTOR_SPINUP) isn't documented in psx-spx.
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
            
            /// @todo (accuracy): (1) the first response must already have stat bit 5 cleared; defaultHardwareStatus was captured before isReading was cleared (L374), so it still shows 22h/82h. (2) INT2 should arrive after ~0xD38ACA cycles (single speed) / ~0x18A6076 (double) when running, ~0x1D7B when already stopped; this uses MOTOR_SPINUP (5 ms) / PAUSE_WHEN_PAUSED. (3) Stop moves the head to the start of the first track (currentReadLogicalBlockAddress isn't reset) and leaves the motor off (stat bit 1 = 0; not tracked).
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

            /// @todo (accuracy): the first response is hardcoded 02h. The doc (and its Pause test cases) show it keeps bit 5 set if a Read was active (22h); only the second response is 02h. Pause also fails with INT5(stat|1, 80h) during a seek phase and with no disc. No timing is documented for Pause while playing audio.
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {0x02});
            queueInterrupt(0x02, secondResponseDelayTime, {0x02});
            break;
        }
        /// @todo (accuracy): an Init sent while a previous Init's second response is pending is silently dropped (no INT3, no INT5). Init also aborts all commands, but pending second responses in interruptQueue aren't cleared. The 0.70 s speed-change + seek-back penalty (L495-500) and Init clearing mute (L490) are not in psx-spx (it only says mode=20h, motor on, Standby, commands aborted).
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
        /// @todo (unimplemented): Setmode bit 4 (Ignore: keeps previous sector size, ignores exact Setloc position, INT1 stat gets bit 3) and bit 0 (CDDA, needed for L346) aren't modelled.
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
            /// @todo (accuracy): GetlocL fails with INT5(stat|1, 80h) while playing audio (CD-DA has no header/subheader), while seeking (e.g. right after ReadN/ReadS) and with no disc; here it always answers from sectorBuffer. psx-spx says it reports the NEWEST buffered sector (see "Sector Buffer VS GetlocL" tests).
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK , {
                sectorBuffer[12], sectorBuffer[13], sectorBuffer[14], sectorBuffer[15],  // MM SS FF mode
                sectorBuffer[16], sectorBuffer[17], sectorBuffer[18], sectorBuffer[19]   // file channel submode coding
            });
            break;
        }
        case 0x11: { // GetlocP (Get Physical Location Subchannel Q)
            /// @todo (accuracy): GetlocP hardcodes track 01 / index 01 (L562-563), and the "relative" time (L556) is measured from the start of the disc, not of the track. It should use the track containing the position (findTrackNumber / Disc::getTrack), position minus that track's start, AAh in the lead-out, and index 00 in pregaps (needs Disc to parse INDEX 00). currentReadLogicalBlockAddress is the NEXT sector to read, so it's one ahead. Fails with 80h with no disc; works during a seek.
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
            
            /// @todo (accuracy): only session 0 and "OK" are handled. Missing: on a non-multisession disc, session 02h+ must answer INT3(stat) then INT5(06h,40h) twice (drive then spins down); during active read/play the command returns error 80h; Play spin-up is aborted.
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            queueInterrupt(0x02, Hardware::DELAY_CDROM_MOTOR_SPINUP, {defaultHardwareStatus});
            break;
        }
        case 0x13: { // GetTN (Get Track Numbers First and Last)
            /// @todo (accuracy): the no-disc response should be INT5(stat|1, 80h) ("cannot respond yet"). {08h,40h} is GetID's second-response format.
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
            /// @todo (accuracy): a missing parameter sends no response (should be INT5 20h). A non-BCD track parameter must return INT5(stat|1, 10h); binaryCodedDecimalToDecimal accepts anything.
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
        /// @todo (unimplemented): SeekL on an Audio track passes INT3 but the second response is an error INT5(stat+4, 04h) and the motor stops; here it always succeeds. SeekL (data header position) and SeekP (SubQ position) share identical code.
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
            
            /// @todo (accuracy): sub 22h must return the ASCII region string with no status byte ("for U/C" for NTSC-U), not stat + "SCEA". Sub 21h (drive switches: bit0 POS0, bit1 DOOR) hits the generic else, and every unknown sub must answer INT5(11h, 10h) (11h,20h if params were given) instead of INT3(stat). Also, 20h reports vC0 (94h,09h,19h,C0h), but vC0 doesn't support 22h, ReadTOC or the Unlock commands; vC2 (97h,01h,10h,C2h, PU-18 US/EUR) would be consistent. Unimplemented: 00h-05h/10h-18h motor/lens tests, 23h-25h chip IDs, 50h/51h, 60h, 71h-76h.
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
                /// @todo (accuracy): no disc must answer INT3(stat) first, then INT5(08h,40h,00h,00h,00h,00h,00h,00h) as the second response. This sends a single 4-byte INT5 with no INT3.
                queueInterrupt(0x05, Hardware::DELAY_CDROM_ACK, {0x08, 0x40, 0, 0}); 
            } 
            else {
                queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
                /// @todo (unimplemented): only "licensed Mode2 disc, SCEA" is produced. Missing: Audio disc INT5(0Ah,90h,...), unlicensed discs INT5(0Ah,80h,...), SCEE/SCEI regions, door-open/spin-up INT5(...,80h), and stat bit 3 (IdError) mirroring the denied flag. Disc::Track::type is available for the audio case.
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
            /// @todo (timing): ReadTOC's first response is "late" (similar to Init, ~DELAY_CDROM_INIT_ACK) and INT2 arrives after ~1 second; this uses DELAY_CDROM_ACK and MOTOR_SPINUP (5 ms). Not supported on vC0.
            queueInterrupt(0x03, Hardware::DELAY_CDROM_ACK, {defaultHardwareStatus});
            queueInterrupt(0x02, Hardware::DELAY_CDROM_MOTOR_SPINUP, {defaultHardwareStatus});
            break;
        }
        default: {
            /// @todo (accuracy): unused/invalid commands (00h, 17h-18h, 20h-4Fh, 60h-FFh) answer INT5(11h, 40h), not stat|1. Unimplemented: 00h Sync, 1Ch Reset (INT3, then software needs ~1/8 s), 1Dh GetQ, 1Fh VideoCD (SCPH-5903 only), 50h-57h Unlock/Lock (still INT5 11h,40h but change lock state), 58h-5Fh (crash the HC05, no response). For unsupported commands the parameter FIFO is NOT cleared, but L706-708 always clears it.
            std::cout << "WARNING: CDROM: Unhandled command 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)command << std::dec << "\n";
            queueInterrupt(0x05, Hardware::DELAY_CDROM_ACK, {static_cast<uint8_t>(defaultHardwareStatus | 0x01), 0x40});
            break;
        }
    }

    while (!parameterQueue.empty()) {
        parameterQueue.pop();
    }
}

/// @todo (timing): psx-spx says seek timings are unknown, so these breakpoints and the 0.6 s speed-change / 0.7 s Init penalties are heuristics, not doc values.
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
