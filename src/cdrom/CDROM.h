#pragma once
#include <cstdint>
#include <queue>
#include <vector>
#include "Disc.h"

struct PendingInterrupt {
    int cyclesRemaining;
    uint8_t flag; // INT1 (Data), INT2 (Complete), INT3 (Ack), INT5 (Error)
    std::vector<uint8_t> responsePayload;
};

class CDROM {
public:
    CDROM();
    
    bool mount(const std::string& cuePath);

    uint8_t read8(uint32_t address);
    void write8(uint32_t address, uint8_t value);
    bool checkInterrupt();

    /// @brief Advances the CD-ROM internal timers and fires pending interrupts.
    void tick(int cycles);

    // BCD (Binary Coded Decimal) helpers for MSF timestamps
    static uint8_t bcdToDec(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }
    static uint8_t decToBcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

private:
     uint8_t mode = 0;


    // Hardware Registers
    uint8_t index = 0; 
    uint8_t interruptEnable = 0; 
    uint8_t interruptFlag = 0; 
    
    std::queue<uint8_t> parameterFIFO;
    std::queue<uint8_t> responseFIFO;

    // Async State Machine
    std::queue<PendingInterrupt> interruptQueue;
    Disc disc;
    
    bool isReading = false;
    int readCycleTimer = 0;
    uint32_t seekTargetLBA = 0;
    uint32_t currentReadLBA = 0;

    // 2352 raw sector + 2048 data buffer
    uint8_t sectorBuffer[2352];
    std::queue<uint8_t> dataFIFO; 

    void queueInterrupt(uint8_t flag, int delayCycles, std::vector<uint8_t> response);
    void executeCommand(uint8_t command);











    uint64_t totalCycles = 0;
};
