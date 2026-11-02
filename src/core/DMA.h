#pragma once

#include <cstdint>
#include <vector>


class Bus;

/// @brief Emulates the PS1 Direct Memory Access (DMA) controller, handling fast data transfers across 7 hardware channels.
class DMA {
public:
	/// @brief Initializes the DMA controller and links it to the main memory bus.
    /// @param bus Pointer to the main Bus for memory and peripheral access.
	explicit DMA(Bus* bus);


	/// @brief Reads an 8-bit value from a DMA register.
    /// @param address The physical memory address of the register.
    /// @return The 8-bit value.
    uint8_t read8(uint32_t address);


	/// @brief Writes an 8-bit value to a DMA register.
    /// @param address The physical memory address of the register.
    /// @param value The 8-bit value to write.
	void write8(uint32_t address, uint8_t value);


	/// @brief Reads a 32-bit value from a DMA register.
    /// @param address The physical memory address of the register.
    /// @return The 32-bit value.
	uint32_t read32(uint32_t address);
    

	/// @brief Writes a 32-bit value to a DMA register.
    /// @param address The physical memory address of the register.
    /// @param value The 32-bit value to write.
	void write32(uint32_t address, uint32_t value);


	/// @brief Advances delayed completions and services the CD-ROM channel.
    /// @details Called once per CPU instruction, so the common "nothing to do" case is decided inline: 
    /// advanceClockSlow() only has an effect when a completion is pending or channel 1, 3 or 4 has its start bit set 
    /// (everything else in it is gated on exactly those).
    /// @param cycles The number of CPU cycles that have elapsed.
	void advanceClock(int cycles);

private:
	struct Channel {
		uint32_t memoryAddress = 0;
		uint32_t blockControl = 0;
		uint32_t channelControl = 0;
		bool transferActive = false; // started and not yet completed (or stalled forever)
	};

	/// @brief Real hardware finishes some cycles after the transfer is triggered, not instantly.
	struct PendingCompletion {
		int cyclesRemaining;
		uint8_t channel;
	};

	static constexpr uint32_t CHANNEL_COUNT = 7;
	static constexpr uint32_t START_BIT = 0x01000000;   // CHCR bit 24: enable / busy
	static constexpr uint32_t TRIGGER_BIT = 0x10000000; // CHCR bit 28: manual start trigger (needed in sync mode 0)

	Bus* bus;
	Channel channels[CHANNEL_COUNT];

    /// @todo (bug): psx-spx (dmachannels.md): DPCR resets to 07654321h (channel N has priority N+1, all channels disabled, CPU priority 0), not 07777777h. Only visible if something reads DPCR before the BIOS writes it, or once priority arbitration exists (see the arbitration todo in write32).
	uint32_t controlRegister = 0x07777777; // Default PS1 startup value
	uint32_t interruptControlRegister = 0;
	std::vector<PendingCompletion> pendingCompletions;

	/// @brief A looping linked list never terminates on real hardware, so the transfer just stays busy. Detected by a node limit.
	bool linkedListStalled = false;

	/// @brief A linked list that ends on anything other than exactly 0xFFFFFF sets DICR bit 15 once the transfer completes.
    /// @note Verified via pcsx-redux dma test: fffff0 / 800000 / 800001 terminators.
	bool linkedListBadTerminator = false;



	/// @brief The slow-path for advanceClock that processes actual DMA transfers and delayed completions.
    /// @param cycles CPU cycles elapsed.
    void advanceClockSlow(int cycles);


    /// @brief Handles a write to a specific channel's CHCR (Channel Control Register).
    /// @param channelIndex The DMA channel (0-6).
    /// @param value The 32-bit control value.
    void writeChannelControl(uint32_t channelIndex, uint32_t value);


    /// @brief Evaluates if a channel is ready to begin a transfer and triggers it if conditions are met.
    /// @param channelIndex The DMA channel (0-6).
    void tryStartChannel(uint32_t channelIndex);


    /// @brief Forcibly aborts an active transfer on a channel.
    /// @param channelIndex The DMA channel (0-6).
    void cancelTransfer(uint32_t channelIndex);


    /// @brief Checks if a channel is globally enabled in the DMA Control Register (DPCR).
    /// @param channelIndex The DMA channel (0-6).
    bool isChannelEnabledInDPCR(uint32_t channelIndex) const { return (controlRegister >> (channelIndex * 4 + 3)) & 1; }
	
	
	//Transfer Handlers
    /// @todo psx-spx: DICR bit 15 (bus error, forces bit 31) is raised when a transfer reads/writes an address outside RAM. Only the linked-list terminator case sets it. Every handler masks with 0x001FFFFC, so addresses in 800000h-FFFFFFh mirror into RAM silently. "Outside RAM" is 8MB only with default memory settings, so this also depends on the RAM size register todo in Bus.cpp.
	uint32_t performGPUTransfer(bool directionToRAM);
	uint32_t performOrderingTableClearTransfer();
	void performCDROMTransfer();
	uint32_t performSPUTransfer();
    uint32_t performMDECInTransfer();
	uint32_t performMDECOutTransfer();


	//Interrupts and Completions

	/// @brief Asserts the completion interrupt flag for a specific channel in the DICR.
    /// @param channel The DMA channel (0-6).
    void setInterruptFlag(uint8_t channel);
    

    /// @brief Re-evaluates the master DMA interrupt status based on channel flags and enables.
    void updateInterruptLine();
    

    /// @brief Finalizes a transfer, clearing active flags and triggering interrupts if requested.
    /// @param channelIndex The DMA channel (0-6).
    void completeTransfer(uint32_t channelIndex);
    

    /// @brief Initiates the delayed completion process for a transfer that just finished its work.
    /// @param channelIndex The DMA channel (0-6).
    /// @param wordsTransferred The total number of 32-bit words moved.
    /// @param syncMode The synchronization mode (0-2) used for the transfer.
    void beginCompletion(uint32_t channelIndex, uint32_t wordsTransferred, uint32_t syncMode);
    

    /// @brief Queues a completion event to resolve after calculated hardware delay cycles.
    /// @param channel The DMA channel (0-6).
    /// @param wordsTransferred The total number of 32-bit words moved.
    void scheduleCompletion(uint8_t channel, uint32_t wordsTransferred);
};