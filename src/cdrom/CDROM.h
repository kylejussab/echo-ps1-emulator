#pragma once

#include <cstdint>
#include <deque>
#include <queue>
#include <vector>
#include "Disc.h"
#include "../core/Constants.h"


/// @brief Emulates the PlayStation CD-ROM drive, managing asynchronous sector reading, hardware command execution, and optical media audio decoding.
class CDROM {
public:
    /// @brief Initializes the CD-ROM drive and resets its internal registers and queues.
    CDROM();


    /// @brief Mounts a disc image into the virtual drive using the provided cue sheet.
    /// @param cuePath The file path to the .cue sheet representing the disc.
    /// @return True if the disc was successfully loaded, false otherwise.
    bool mount(const std::string& cuePath);


    /// @brief Reads a single byte from the CD-ROM hardware registers.
    /// @param address The memory address being read.
    /// @return The value of the requested register.
    uint8_t read8(uint32_t address);


    /// @brief Writes a single byte to the CD-ROM hardware registers.
    /// @param address The memory address being written to.
    /// @param value The byte value to write.
    void write8(uint32_t address, uint8_t value);


    /// @brief Advances the CD-ROM internal timers and state machine.
    /// @param cycles The number of system clock cycles to advance.
    void advanceClock(int cycles);


    /// @brief Reads the next audio sample for the SPU, handling both digital audio and extended architecture formats.
    /// @param leftChannel Reference to store the left audio channel output.
    /// @param rightChannel Reference to store the right audio channel output.
    /// @return True if a sample was available and read, false otherwise.
    bool readAudioSample(int16_t& leftChannel, int16_t& rightChannel);
    

    /// @brief Checks if a hardware interrupt has been triggered and is enabled.
    /// @return True if an active interrupt requires CPU attention.
    bool checkInterrupt() { return (interruptFlag & interruptEnable & 0x1F) != 0; }

    
    /// @brief Gets the number of bytes currently waiting in the data queue.
    /// @return The size of the data queue.
    uint32_t getDataQueueSize() const { return static_cast<uint32_t>(dataQueue.size()); }


    /// @brief Converts a Binary Coded Decimal value to a standard integer.
    /// @param binaryCodedDecimalValue The encoded value.
    /// @return The decoded integer.
    static uint8_t binaryCodedDecimalToDecimal(uint8_t binaryCodedDecimalValue) { return ((binaryCodedDecimalValue >> 4) * 10) + (binaryCodedDecimalValue & 0x0F); }
    

    /// @brief Converts a standard integer to a Binary Coded Decimal value.
    /// @param decimalValue The standard integer.
    /// @return The encoded value.
    static uint8_t decimalToBinaryCodedDecimal(uint8_t decimalValue) { return ((decimalValue / 10) << 4) | (decimalValue % 10); }


    /// @brief Acknowledges that the CPU has received the interrupt and clears the delivery flag.
    /// @return True if an interrupt was pending delivery.
    bool consumeDeliveredInterrupt();

private:
    struct PendingInterrupt {
        int cyclesRemaining;
        uint8_t interruptFlag;
        std::vector<uint8_t> responsePayload;
    };

    // Hardware Registers & Core State
    uint8_t operatingMode = 0;
    uint8_t registerIndex = 0;
    uint8_t interruptEnable = 0;
    uint8_t interruptFlag = 0;

    
    // Communication Queues
    std::queue<uint8_t> parameterQueue;
    std::queue<uint8_t> responseQueue;
    std::queue<uint8_t> dataQueue;


    // Interrupt Handling
    std::queue<PendingInterrupt> interruptQueue;
    bool interruptWasDelivered = false;


    // Drive & Media State
    Disc compactDisc;
    bool isReading = false;
    bool hasSectorInBuffer = false;
    bool setLocationPending = false;
    int readCycleTimer = 0;
    uint32_t seekTargetLogicalBlockAddress = 0;
    uint32_t currentReadLogicalBlockAddress = 0;
    uint8_t sectorBuffer[2352];
    bool speedChangePending = false;
    int pendingInitializationCycles = 0;
    uint8_t playingTrackNumber = 0;


    // Audio Playback State
    struct ExtendedArchitectureFrame { 
        int16_t leftChannel; 
        int16_t rightChannel; 
    };
    
    std::deque<ExtendedArchitectureFrame> extendedArchitectureFrames;
    int32_t extendedArchitecturePhase = 0;              
    bool extendedArchitectureHalfRate = false;          
    int32_t extendedArchitectureHistory[2][2] = {};     
    uint8_t extendedArchitectureFilterFile = 0;         
    uint8_t extendedArchitectureFilterChannel = 0;
    
    bool isPlayingAudio = false;      
    bool isAudioMuted = false;          
    uint8_t compactDiscDigitalAudioBuffer[2352] = {};
    uint32_t compactDiscDigitalAudioFrame = 588;         
    
    uint8_t pendingCompactDiscVolume[4] = { 0x80, 0x00, 0x80, 0x00 };
    uint8_t activeCompactDiscVolume[4] = { 0x80, 0x00, 0x80, 0x00 };


    // Constants
    static constexpr int32_t EXTENDED_ARCHITECTURE_FILTER_POSITIVE[4] = { 0, 60, 115, 98 };
    static constexpr int32_t EXTENDED_ARCHITECTURE_FILTER_NEGATIVE[4] = { 0, 0, -52, -55 };


    /// @brief Decodes and begins execution of a hardware command sent to the drive.
    /// @param command The hexadecimal command identifier.
    void executeCommand(uint8_t command);


    /// @brief Handles the heavy lifting of processing pending interrupts and continuous disc reading.
    /// @param cycles The number of system clock cycles to advance.
    void advanceClockSlowly(int cycles);


    /// @brief Decodes a raw audio sector into Extended Architecture Adaptive Differential Pulse Code Modulation frames.
    void decodeExtendedArchitectureSector();


    /// @brief Interpolates and outputs the next Extended Architecture audio sample.
    /// @param leftChannel Reference to store the left audio channel output.
    /// @param rightChannel Reference to store the right audio channel output.
    /// @return True if a sample was successfully generated.
    bool readExtendedArchitectureSample(int16_t& leftChannel, int16_t& rightChannel);


    /// @brief Reads and outputs the next standard CD Digital Audio sample.
    /// @param leftChannel Reference to store the left audio channel output.
    /// @param rightChannel Reference to store the right audio channel output.
    /// @return True if a sample was successfully read.
    bool readCompactDiscDigitalAudioSample(int16_t& leftChannel, int16_t& rightChannel);


    /// @brief Clamps a 32-bit audio sample to standard 16-bit bounds to prevent peaking or overflow.
    /// @param value The calculated audio value.
    /// @return The clamped 16-bit audio sample.
    static int16_t clampAudioSample(int32_t value) { return static_cast<int16_t>(value < -0x8000 ? -0x8000 : (value > 0x7FFF ? 0x7FFF : value)); };
    

    /// @brief Schedules a hardware interrupt to fire after a specified delay.
    /// @param flag The interrupt identifier.
    /// @param delayCycles The number of cycles to wait before firing.
    /// @param response The payload of bytes to push to the response queue.
    void queueInterrupt(uint8_t flag, int delayCycles, std::vector<uint8_t> response) { interruptQueue.push({delayCycles, flag, response}); };
    

    /// @brief Approximates the mechanical delay required for the optical drive laser to travel between two data sectors.
    /// This is a heuristic estimation factoring in physical distance, pending drive initializations, and motor speed changes.
    /// @param fromLogicalBlockAddress The starting sector position of the laser head.
    /// @param toLogicalBlockAddress The target destination sector.
    /// @return The estimated seek time converted into Central Processing Unit clock cycles.
    int calculateSeekCycles(uint32_t fromLogicalBlockAddress, uint32_t toLogicalBlockAddress);


    /// @brief Checks the current operating mode to determine if the drive is running at double speed.
    /// @return True if double speed mode is active.
    bool isDoubleSpeed() const { return (operatingMode & 0x80) != 0; };
    

    /// @brief Calculates the correct cycle delay between sector reads based on the current drive speed.
    /// @return The number of delay cycles.
    int calculateReadDelayCycles() const { return isDoubleSpeed() ? Hardware::DELAY_CDROM_READ_DOUBLE_SPEED : Hardware::DELAY_CDROM_READ_SINGLE_SPEED; };


	/// @brief Determines the track number containing a specific sector based on its Logical Block Address.
    /// @param logicalBlockAddress The absolute Logical Block Address of the target sector.
    /// @return The corresponding track number (1 through 99), or 0 if the disc contains no tracks.
    uint8_t findTrackNumber(uint32_t logicalBlockAddress);
};
