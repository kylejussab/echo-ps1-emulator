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
	/// Called once per CPU instruction, so the common "nothing to do" case is decided inline: tickSlow() only has an effect when a
	/// completion is pending or channel 1, 3 or 4 has its start bit set (everything else in it is gated on exactly those).
	void tick(int cycles) {
		if (pendingCompletions.empty() && ((channels[1].channelControl | channels[3].channelControl | channels[4].channelControl) & START_BIT) == 0) return;
		tickSlow(cycles);
	}

private:
	static constexpr uint32_t CHANNEL_COUNT = 7;
	static constexpr uint32_t START_BIT = 0x01000000;   // CHCR bit 24: enable / busy
	static constexpr uint32_t TRIGGER_BIT = 0x10000000; // CHCR bit 28: manual start trigger (needed in sync mode 0)

	struct Channel {
		uint32_t memoryAddress = 0;
		uint32_t blockControl = 0;
		uint32_t channelControl = 0;
		bool transferActive = false; // started and not yet completed (or stalled forever)
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

	// A looping linked list never terminates on real hardware, so the transfer just stays busy. Detected by a node limit.
	bool linkedListStalled = false;

	// A linked list that ends on anything other than exactly 0xFFFFFF sets DICR bit 15 once the transfer completes
	// (pcsx-redux dma test: fffff0 / 800000 / 800001 terminators)
	bool linkedListBadTerminator = false;

	void tickSlow(int cycles);
	void writeChannelControl(uint32_t channelIndex, uint32_t value);
	void tryStartChannel(uint32_t channelIndex);
	void cancelTransfer(uint32_t channelIndex);
	bool isChannelEnabledInDPCR(uint32_t channelIndex) const { return (controlRegister >> (channelIndex * 4 + 3)) & 1; }
	uint32_t performGPUTransfer(bool directionToRAM);
	uint32_t performOrderingTableClearTransfer();
	void performCDROMTransfer();
	uint32_t performSPUTransfer();
    uint32_t performMDECInTransfer();
	void setInterruptFlag(uint8_t channel);
	void updateInterruptLine();
	void completeTransfer(uint32_t channelIndex);
	void beginCompletion(uint32_t channelIndex, uint32_t wordsTransferred, uint32_t syncMode);
	void scheduleCompletion(uint8_t channel, uint32_t wordsTransferred);
	uint32_t performMDECOutTransfer();
};