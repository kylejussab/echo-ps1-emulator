#pragma once
#include <cstdint>
#include <vector>

class Bus;

class DMA {
public:
	explicit DMA(Bus* bus);

    uint8_t read8(uint32_t address);
	uint32_t read32(uint32_t address);
    void write8(uint32_t address, uint8_t value);
	void write32(uint32_t address, uint32_t value);

	/// @brief Advances delayed completions and services the CD-ROM channel.
	void tick(int cycles);

private:
	static constexpr uint32_t CHANNEL_COUNT = 7;
	static constexpr uint32_t START_BIT = 0x01000000;

	struct Channel {
		uint32_t memoryAddress = 0;
		uint32_t blockControl = 0;
		uint32_t channelControl = 0;
	};

	// Real hardware finishes some cycles after the transfer is triggered, not instantly
	struct PendingCompletion {
		int cyclesRemaining;
		uint8_t channel;
	};

	Bus* bus;
	Channel channels[CHANNEL_COUNT];
	uint32_t controlRegister = 0x07777777; // Default PS1 startup value
	uint32_t interruptControlRegister = 0;
	std::vector<PendingCompletion> pendingCompletions;

	void writeChannelControl(uint32_t channelIndex, uint32_t value);
	uint32_t performGPUTransfer(bool directionToRAM);
	void performOrderingTableClearTransfer();
	void performCDROMTransfer();
	uint32_t performSPUTransfer();
    uint32_t performMDECInTransfer();
	void setInterruptFlag(uint8_t channel);
	void updateInterruptLine();
	void scheduleCompletion(uint8_t channel, uint32_t wordsTransferred);
};