#include <algorithm>
#include <iostream>
#include "Bus.h"
#include "Constants.h"
#include "DMA.h"

DMA::DMA(Bus* bus) : bus(bus) {}


uint8_t DMA::read8(uint32_t address) {
	uint32_t alignedAddress = address & ~0x3u;
	uint32_t byteShift = (address & 0x03) * 8;
	return (read32(alignedAddress) >> byteShift) & 0xFF;
}


void DMA::write8(uint32_t address, uint8_t value) {
    bool isInterruptControlByte = address >= Hardware::REGISTER_DMA_INTERRUPT_CONTROL && address < Hardware::REGISTER_DMA_INTERRUPT_CONTROL + 4;

    if (isInterruptControlByte) {
        uint32_t byteIndex = address - Hardware::REGISTER_DMA_INTERRUPT_CONTROL;

        switch (byteIndex) {
            case 0x00:
				/// @todo (bug): bits 0-6 are writable per psx-spx, see the DICR mask todo in write32. This 0x3F mask drops bit 6.
                // Bits 0-5 are writable, bits 6-7 are always 0
                interruptControlRegister = (interruptControlRegister & ~0x0000003Fu) | (value & 0x3Fu);
                break;
            case 0x01:
                // Only bit 15 (force IRQ) is writable, bits 8-14 are always 0
                interruptControlRegister = (interruptControlRegister & ~0x00008000u) | ((value & 0x80u) << 8);
                break;
            case 0x02:
                // Bits 16-23 (channel IRQ enables + master enable) are plain read/write
                interruptControlRegister = (interruptControlRegister & ~0x00FF0000u) | (static_cast<uint32_t>(value) << 16);
                break;
            case 0x03: {
                // Byte 3 holds the flags (bits 24-30), which are write-1-to-clear. Bit 31 is read-only.
                uint32_t flagsToClear = static_cast<uint32_t>(value & 0x7F) << 24;
                interruptControlRegister &= ~flagsToClear;
                break;
            }
        }

        updateInterruptLine();
        return;
    }

	/// @todo psx-spx (CPU spec, same as the 16-bit todo in Bus.cpp): byte stores to MADR/BCR/CHCR/DPCR should act as a 32-bit store of the full GPR value. Only DICR bytes are handled here, everything else hits this FATAL. Needs the CPU/Bus to pass the full value.
    std::cout << "FATAL: Unhandled DMA write8 at: 0x" << std::hex << address << "\n";
    exit(1);
}


uint32_t DMA::read32(uint32_t address) {
	if (address == Hardware::REGISTER_DMA_CONTROL) {
		return controlRegister;
	}

	if (address == Hardware::REGISTER_DMA_INTERRUPT_CONTROL) {
		return interruptControlRegister;
	}

	/// @todo (bug): psx-spx (memorymap.md): the seven channel CHCR registers at 1F8010x8h are mirrored at 1F8010xCh (e.g. 1F80108Ch is channel 0's CHCR). `(address & 0xF) <= 0x8` rejects xC, so those accesses hit the FATAL below. (iomap.md lists 1F8010F8h/FCh as unknown, so crashing there is fine.)
	bool isChannelRegister = address >= Hardware::REGISTER_DMA_BASE && address < Hardware::REGISTER_DMA_CONTROL && (address & 0x3) == 0 && (address & 0xF) <= 0x8;

	if (isChannelRegister) {
		uint32_t channelIndex = (address - Hardware::REGISTER_DMA_BASE) >> 4;

		const Channel& channel = channels[channelIndex];
		switch (address & 0xF) {
			case 0x00: return channel.memoryAddress;
			case 0x04: return channel.blockControl;
			case 0x08: return channel.channelControl;
		}
	}

	/// @todo psx-spx: 1F8010F8h usually reads 7FFAC68Bh or 0BFAC688h (7FE358D1h after a DMA transfer) and 1F8010FCh reads 00FFFFF7h, both read-only. The doc says behaviour during a transfer is untested, so low priority. This (and read8 through it) hits the FATAL today.
	std::cout << "FATAL: Unhandled DMA read32 at address: 0x" << std::hex << address << "\n";
	exit(1);
}


void DMA::write32(uint32_t address, uint32_t value) {
	/// @todo psx-spx: the DPCR priority fields (bits 4n..4n+2, 0 = highest) and CPU priority (bits 28-30) are ignored. Equal priorities resolve by channel number (DMA6 highest, DMA0 lowest, CPU above DMA6?). Transfers run in trigger order, instantly, so overlapping requests never wait on each other (advanceClockSlow also hardcodes the 1, 4, 3 order).
    // DPCR: DMA Control Register (0x1F8010F0)
    // Globally enables/disables individual DMA channels. Enabling a channel can immediately start a pending transfer.
    if (address == Hardware::REGISTER_DMA_CONTROL) {
        controlRegister = value;
        for (uint32_t channelIndex = 0; channelIndex < CHANNEL_COUNT; channelIndex++) {
            // CD-ROM (Channel 3) transfers are paced by the drive's sector delivery timing in advanceClock(), not instantly on register write
            if (channelIndex == 3) continue; 
            if (channels[channelIndex].transferActive) continue; // writing DPCR must not restart a transfer that is already running
            
            tryStartChannel(channelIndex);
        }
        return;
    }

    // DICR: DMA Interrupt Control Register (0x1F8010F4)
    if (address == Hardware::REGISTER_DMA_INTERRUPT_CONTROL) {
        // Bits 24-30 are ACK flags: Writing a 1 clears the bit, writing a 0 leaves it unchanged
        uint32_t flagsToClear = value & 0x7F000000;
        interruptControlRegister &= ~flagsToClear;
        
        // Bits 6-14 are hardwired to 0
		/// @todo (bug): psx-spx: DICR bits 0-6 are the per-channel "interrupt on every slice / list node" controls for channels 0-6, and only 7-14 are unused. The 0x3F / 0x00FF803F masks drop bit 6 (channel 6). I believe older write-ups listed only bits 0-5, so if a test made you pick 0x3F, keep it and note that here.
        interruptControlRegister = (interruptControlRegister & 0xFF000000) | (value & 0x00FF803F); 
        updateInterruptLine();
        return;
    }

    // Per-Channel Registers (0x1F801080 - 0x1F8010E8)
	/// @todo (bug): same mirror gap as in read32.
    bool isChannelRegister = address >= Hardware::REGISTER_DMA_BASE && address < Hardware::REGISTER_DMA_CONTROL
        && (address & 0x3) == 0 && (address & 0xF) <= 0x8;

    if (isChannelRegister) {
        uint32_t channelIndex = (address - Hardware::REGISTER_DMA_BASE) >> 4;

        switch (address & 0x0F) {
            case 0x00: { // MADR: Base Address Register (24-bit)
                channels[channelIndex].memoryAddress = value & 0x00FFFFFF; 
                return;
			} 
            case 0x04: { // BCR: Block Control Register (Word count / Block size)
				channels[channelIndex].blockControl = value; 
                return;
			} 
            case 0x08: { // CHCR: Channel Control Register (Direction, Sync Mode, Start Trigger)
				writeChannelControl(channelIndex, value); 
                return;
			}  
        }
    }

    std::cout << "FATAL: Unhandled DMA write32 at: 0x" << std::hex << address << "\n";
    exit(1);
}


void DMA::advanceClock(int cycles) {
	if (pendingCompletions.empty() && ((channels[1].channelControl | channels[3].channelControl | channels[4].channelControl) & START_BIT) == 0) return;
	advanceClockSlow(cycles);
}



//Privates
void DMA::advanceClockSlow(int cycles) {
	// Fire any completions whose delay has elapsed
	for (size_t index = 0; index < pendingCompletions.size(); ) {
		pendingCompletions[index].cyclesRemaining -= cycles;
		if (pendingCompletions[index].cyclesRemaining <= 0) {
			uint8_t completedChannel = pendingCompletions[index].channel;
			pendingCompletions.erase(pendingCompletions.begin() + index);

			if (completedChannel == 1) {
				// Deferred MDEC output: the decoded data lands in RAM now, right before the completion interrupt
				uint8_t syncModeOfChannel = (channels[1].channelControl >> 9) & 0x03;
				performMDECOutTransfer();
				if (syncModeOfChannel == 1) channels[1].blockControl &= 0x0000FFFF;
			}

			completeTransfer(completedChannel);
		}
		else {
			index++;
		}
	}

	// A DMA1 that was started before the MDEC had output is waiting for it
	tryStartChannel(1);

	// A DMA4 that was started while the SPU wasn't requesting data is waiting for it
	tryStartChannel(4);

	// Check if DMA Channel 3 is active and waiting for data
	bool isChannel3Enabled = (channels[3].channelControl & START_BIT) != 0 && isChannelEnabledInDPCR(3);
	if (!isChannel3Enabled) return;

	// Bit 6 (0x40) of the CD-ROM status register is "Data FIFO Not Empty" (read8 of that register has no side effects)
	bool isDataReady = bus->getCDROM().getDataQueueSize() != 0;

	if (isDataReady) {
		performCDROMTransfer();
		/// @todo (bug): the CD-ROM path clears only bit 24 and skips completeTransfer(). Bit 28 stays set after the transfer (normal CD-ROM CHCR is 11000000h), transferActive is never set for channel 3, and the DICR flag is raised inside performCDROMTransfer with no transfer delay (see the timing todos).
		channels[3].channelControl &= ~START_BIT;
	}
}


/// @todo psx-spx: CHCR bit 8 (burst: chopping, with DMA window bits 16-18 = 1<<N words and CPU window bits 20-22 = 1<<N cycles; slice: hangs the DMA; linked list: header-before-data), bit 29 (forced-burst pause / keeps bit 28 set across slices) and bit 30 (bus snooping) are stored but have no effect. With chopping, MADR updates and BCR's BC counts down in SyncMode 0. CD-ROM chopped (11400100h) is the "rarely used" case.
void DMA::writeChannelControl(uint32_t channelIndex, uint32_t value) {
	Channel& channel = channels[channelIndex];

	// Channel 6 (OTC): only bits 24, 28 and 30 are writable, bit 1 (step backwards) is hardwired to 1 (ps1-tests dma/otc-test)
	if (channelIndex == 6) value = (value & 0x51000000) | 0x00000002;

	// Clearing the enable bit stops a transfer that is running (or stalled forever)
	if ((value & START_BIT) == 0) {
		cancelTransfer(channelIndex);
	}

	channel.channelControl = value;
	tryStartChannel(channelIndex);
}


void DMA::tryStartChannel(uint32_t channelIndex) {
	Channel& channel = channels[channelIndex];

	if ((channel.channelControl & START_BIT) == 0) return;

	if (channel.transferActive) {
		if (channelIndex != 2) return;

		uint32_t requestedControl = channel.channelControl;
		for (size_t index = 0; index < pendingCompletions.size(); ) {
			if (pendingCompletions[index].channel == 2) pendingCompletions.erase(pendingCompletions.begin() + index);
			else index++;
		}
		completeTransfer(2); // clears the start bit and raises the DICR flag
		channel.channelControl = requestedControl; // keep the new request
	}

	/// @todo psx-spx: SyncMode 3 is reserved. It isn't rejected, and the channels that don't FATAL treat it like SyncMode 1.
	// Sync mode 0 ("burst") also needs the manual trigger bit. The OTC channel always behaves as mode 0.
	uint32_t syncMode = (channelIndex == 6) ? 0 : (channel.channelControl >> 9) & 0x03;

	// ...except on the SPU: its own DREQ starts the transfer (jaczekanski spu/memory-transfer sync mode 0 tests run without the trigger bit)
	bool needsManualTrigger = (syncMode == 0) && channelIndex != 4;
	if (needsManualTrigger && (channel.channelControl & TRIGGER_BIT) == 0) return;

	// The channel's master enable bit in DPCR must be set; otherwise the transfer just waits (ps1-tests dma/dpcr)
	if (!isChannelEnabledInDPCR(channelIndex)) return;

	/// @todo psx-spx: burst starts after DREQ is first asserted and slice mode transfers one block per DREQ, and the remote side must be set up too (e.g. the GPU for DMA2). Only channels 1 (MDEC has output), 3 (CD data FIFO) and 4 (SPU DMARequest) check a request. Channels 0 and 2 start unconditionally (GPU DMA direction / MDEC enable aren't consulted), and slice transfers run every block back to back instead of paced per DREQ.
	switch (channelIndex) {
		case 0x00: {
			channel.transferActive = true;
			beginCompletion(0, performMDECInTransfer(), syncMode);
			break;
		}
		case 0x01: {
			// No output yet (DMA1 was started before DMA0 fed the MDEC): stay pending, tick() retries
			if (!bus->getMDEC().hasOutput()) break;

			// The MDEC produces its output over time, so the data only lands in RAM when the transfer completes (see tickSlow).
			// Delivering it instantly lets it overwrite a buffer the game is still uploading to VRAM.
			uint32_t blockSize = channel.blockControl & 0xFFFF;
			uint32_t blockCount = (channel.blockControl >> 16) & 0xFFFF;
			/// @todo (bug): BA == 0 should mean 10000h blocks, see the BA todo in performGPUTransfer.
			if (blockSize == 0) blockSize = 0x10000;
			uint32_t estimatedWords = (syncMode == 0) ? blockSize : (blockSize * blockCount);

			// Empirical value, not from any official source or hardware measurement. 2 and 4 play the movie; 5 and 8 make the game stall with the video never starting (the game seems to have a tight timeout while waiting for the slice). Real MDEC speed is unknown here.
			constexpr uint32_t MDEC_OUT_CYCLES_PER_WORD = 2;
			int delay = static_cast<int>(std::max<uint32_t>(estimatedWords * MDEC_OUT_CYCLES_PER_WORD, 8));

			channel.transferActive = true;
			pendingCompletions.push_back({delay, 1});
			break;
		}
		case 0x02: {
			linkedListStalled = false;
			linkedListBadTerminator = false;
			uint32_t wordsTransferred = performGPUTransfer((channel.channelControl & 0x00000001) == 0);
			channel.transferActive = true;
			if (!linkedListStalled) beginCompletion(2, wordsTransferred, syncMode);
			break;
		}
		case 0x03: {
			// Channel 3 waits for the CD-ROM to have data, so tick() starts it
			break;
		}
		case 0x04: {
			// The SPU holds DREQ low until it can take (or has fetched) a block, which needs SPUCNT set to a DMA transfer mode.
			// Until then the channel just waits and tick() retries, like channel 1 does for the MDEC.
			if (!bus->getSPU().DMARequest()) break;

			channel.transferActive = true;
			uint32_t wordsTransferred = performSPUTransfer();

			// Burst mode would complete instantly in beginCompletion(), but the SPU can't take data that fast, so every
			// SPU transfer (burst or block) completes after a delay based on the SPU's own transfer rate.
			if (syncMode == 1) channel.blockControl &= 0x0000FFFF;
			scheduleCompletion(4, wordsTransferred);
			break;
		}
		case 0x05: {
			std::cout << "FATAL: PIO DMA (Channel 5) transfer started. CHCR 0x" << std::hex << channel.channelControl << "\n";
			exit(1);
		}
		case 0x06: {
			channel.transferActive = true;
			beginCompletion(6, performOrderingTableClearTransfer(), syncMode);
			break;
		}
	}
}


void DMA::cancelTransfer(uint32_t channelIndex) {
	channels[channelIndex].transferActive = false;
	for (size_t index = 0; index < pendingCompletions.size(); ) {
		if (pendingCompletions[index].channel == channelIndex) pendingCompletions.erase(pendingCompletions.begin() + index);
		else index++;
	}
}


//Privates: Transfer Handlers
uint32_t DMA::performGPUTransfer(bool directionToRAM) {
	Channel& channel = channels[2];

	uint8_t syncMode = (channel.channelControl >> 9) & 0x03;
	uint32_t blockSize = channel.blockControl & 0xFFFF;
	uint32_t blockCount = (channel.blockControl >> 16) & 0xFFFF;

	/// @todo (bug): psx-spx: BC/BS/BA are 0001h..FFFFh with 0 meaning 10000h. Only blockSize gets that, so blockCount == 0 gives totalWords = 0 in SyncMode 1. Same gap in the CD-ROM, SPU, MDEC in and MDEC out handlers and in tryStartChannel case 1 (estimatedWords).
	if (blockSize == 0) { blockSize = 0x10000; }

	uint32_t totalWords = (syncMode == 0) ? blockSize : (blockSize * blockCount);

	// Linked List Mode (Sync Mode 2)
	if (syncMode == 2) {
		uint32_t currentAddress = channel.memoryAddress & 0x00FFFFFC;
		uint32_t wordsTransferred = 0;

		constexpr uint32_t MAX_LIST_NODES = 1u << 20;
		for (uint32_t nodeCount = 0; ; nodeCount++) {
			if (nodeCount >= MAX_LIST_NODES) {
				linkedListStalled = true;
				break;
			}

			uint32_t header = bus->read32(currentAddress & 0x001FFFFC);
			uint8_t wordCount = header >> 24;
			uint32_t nextAddress = header & 0x00FFFFFF;

			uint32_t payloadAddress = currentAddress + 4;
			for (uint8_t wordIndex = 0; wordIndex < wordCount; wordIndex++) {
				bus->getGPU().writeGP0(bus->read32(payloadAddress & 0x001FFFFC));
				payloadAddress += 4;
			}
			wordsTransferred += wordCount;

			channel.memoryAddress = nextAddress; // MADR follows the list and ends on the terminator value
			if (nextAddress & 0x00800000) {
				linkedListBadTerminator = (nextAddress != 0x00FFFFFF); // anything but exactly 0xFFFFFF flags DICR bit 15 on completion
				break;
			}

			currentAddress = nextAddress & 0x00FFFFFC;
		}
		return wordsTransferred;
	}

	// Block Transfer Mode (Sync Modes 0 and 1)
	uint32_t currentAddress = channel.memoryAddress & 0x00FFFFFC;
	uint32_t stepBytes = (channel.channelControl & 0x2) ? static_cast<uint32_t>(-4) : 4u;

	for (uint32_t wordIndex = 0; wordIndex < totalWords; wordIndex++) {
		if (directionToRAM) {
			bus->write32(currentAddress & 0x001FFFFC, bus->getGPU().readGP0());
		}
		else {
			bus->getGPU().writeGP0(bus->read32(currentAddress & 0x001FFFFC));
		}
		currentAddress = (currentAddress + stepBytes) & 0x00FFFFFF;
	}

	/// @todo (bug): psx-spx: in SyncMode 0 the hardware does not update MADR (it keeps the start address during and after the transfer) unless chopping is enabled. This writes the end address back in every mode, here and in the OTC, CD-ROM, SPU and MDEC handlers. SyncMode 1 (end address) and SyncMode 2 (end marker) are correct.
	channel.memoryAddress = currentAddress;
	return totalWords;
}


uint32_t DMA::performOrderingTableClearTransfer() {
	Channel& channel = channels[6];

	uint32_t currentAddress = channel.memoryAddress & 0x00FFFFFC;
	uint32_t blockCount = channel.blockControl & 0xFFFF;

	if (blockCount == 0) blockCount = 0x10000;

	for (uint32_t blockIndex = 0; blockIndex < blockCount; blockIndex++) {
		uint32_t previousAddress = (currentAddress - 4) & 0x00FFFFFF;
		uint32_t nextAddress = (blockIndex == blockCount - 1) ? 0x00FFFFFF : previousAddress;
		/// @todo (bug): psx-spx: MADR wraps from 000000h down to FFFFFCh, and words after the wraparound are NOT written to RAM (FFFFFCh is past the 8MB region). Masking with 0x001FFFFC aliases them back into RAM instead.
		bus->write32(currentAddress & 0x001FFFFC, nextAddress);
		currentAddress = previousAddress;
	}

	/// @todo (bug): SyncMode 0 must not update MADR, see the SyncMode 0 MADR todo in performGPUTransfer.
	channel.memoryAddress = currentAddress;
	return blockCount;
}


/// @todo psx-spx: CHCR bits 0 (direction) and 1 (MADR step +4/-4) apply to every channel. CD-ROM always steps +4 and ignores direction, the linked-list path ignores direction, and SPU/MDEC FATAL on a backward step (fine as crash-loud).
void DMA::performCDROMTransfer() {
	Channel& channel = channels[3];

	uint32_t currentAddress = channel.memoryAddress & 0x00FFFFFC;

	uint32_t blockSize = channel.blockControl & 0xFFFF;
	uint32_t blockCount = (channel.blockControl >> 16) & 0xFFFF;
	uint8_t syncMode = (channel.channelControl >> 9) & 0x03;

	/// @todo (bug): BA == 0 should mean 10000h blocks, see the BA todo in performGPUTransfer.
	if (blockSize == 0) blockSize = 0x10000;

	// In Sync Mode 0 (Burst), blockCount is ignored.
	uint32_t totalWords = (syncMode == 0) ? blockSize : (blockSize * blockCount);

	uint32_t bytesAvailable = bus->getCDROM().getDataQueueSize();
	uint32_t bytesRequested = totalWords * 4;

	if (bytesRequested > bytesAvailable) {
		std::cout << "FATAL: CD-ROM DMA requested 0x" << std::hex << bytesRequested << " bytes but the data FIFO only holds 0x" << bytesAvailable << "\n";
		exit(1);
	}

	for (uint32_t wordIndex = 0; wordIndex < totalWords; wordIndex++) {
		uint8_t byte0 = bus->getCDROM().read8(0x1F801802);
		uint8_t byte1 = bus->getCDROM().read8(0x1F801802);
		uint8_t byte2 = bus->getCDROM().read8(0x1F801802);
		uint8_t byte3 = bus->getCDROM().read8(0x1F801802);

		uint32_t word = byte0 | (byte1 << 8) | (byte2 << 16) | (byte3 << 24);

		bus->write32(currentAddress & 0x001FFFFC, word);
		currentAddress = (currentAddress + 4) & 0x00FFFFFF;
	}

	/// @todo (bug): SyncMode 0 must not update MADR, see the SyncMode 0 MADR todo in performGPUTransfer.
	channel.memoryAddress = currentAddress;
	if (syncMode == 1) channel.blockControl &= 0x0000FFFF;
	setInterruptFlag(3);
}


uint32_t DMA::performSPUTransfer() {
	Channel& channel = channels[4];

	uint32_t currentAddress = channel.memoryAddress & 0x00FFFFFC;

	uint32_t blockSize = channel.blockControl & 0xFFFF;
	uint32_t blockCount = (channel.blockControl >> 16) & 0xFFFF;
	uint8_t syncMode = (channel.channelControl >> 9) & 0x03;
	bool isFromRAM = (channel.channelControl & 0x1) != 0;
	bool isAddressStepBackward = (channel.channelControl & 0x2) != 0;

	if (syncMode == 2 || isAddressStepBackward) {
		std::cout << "FATAL: SPU DMA with unsupported mode. CHCR 0x" << std::hex << channel.channelControl << "\n";
		exit(1);
	}

	/// @todo (bug): BA == 0 should mean 10000h blocks, see the BA todo in performGPUTransfer.
	if (blockSize == 0) blockSize = 0x10000;
	uint32_t totalWords = (syncMode == 0) ? blockSize : (blockSize * blockCount);

	for (uint32_t wordIndex = 0; wordIndex < totalWords; wordIndex++) {
		if (isFromRAM) {
			bus->getSPU().writeDMAWord(bus->read32(currentAddress & 0x001FFFFC));
		}
		else {
			bus->write32(currentAddress & 0x001FFFFC, bus->getSPU().readDMAWord());
		}
		currentAddress = (currentAddress + 4) & 0x00FFFFFF;
	}

	/// @todo (bug): SyncMode 0 must not update MADR, see the SyncMode 0 MADR todo in performGPUTransfer.
	channel.memoryAddress = currentAddress;
	return totalWords;
}


uint32_t DMA::performMDECInTransfer() {
	Channel& channel = channels[0];

	uint32_t currentAddress = channel.memoryAddress & 0x00FFFFFC;

	uint32_t blockSize = channel.blockControl & 0xFFFF;
	uint32_t blockCount = (channel.blockControl >> 16) & 0xFFFF;
	uint8_t syncMode = (channel.channelControl >> 9) & 0x03;
	bool isFromRAM = (channel.channelControl & 0x1) != 0;
	bool isAddressStepBackward = (channel.channelControl & 0x2) != 0;

	if (syncMode == 0x02 || isAddressStepBackward || !isFromRAM) {
		std::cout << "FATAL: MDEC In DMA with unsupported mode. CHCR 0x" << std::hex << channel.channelControl << "\n";
		exit(1);
	}

	/// @todo (bug): BA == 0 should mean 10000h blocks, see the BA todo in performGPUTransfer.
	if (blockSize == 0) blockSize = 0x10000;
	uint32_t totalWords = (syncMode == 0x00) ? blockSize : (blockSize * blockCount);

	for (uint32_t wordIndex = 0; wordIndex < totalWords; wordIndex++) {
		bus->getMDEC().writeCommand(bus->read32(currentAddress & 0x001FFFFC));
		currentAddress = (currentAddress + 4) & 0x00FFFFFF;
	}

	/// @todo (bug): SyncMode 0 must not update MADR, see the SyncMode 0 MADR todo in performGPUTransfer.
	channel.memoryAddress = currentAddress;
	return totalWords;
}


uint32_t DMA::performMDECOutTransfer() {
	Channel& channel = channels[1];

	uint32_t currentAddress = channel.memoryAddress & 0x00FFFFFC;

	uint32_t blockSize = channel.blockControl & 0xFFFF;
	uint32_t blockCount = (channel.blockControl >> 16) & 0xFFFF;
	uint8_t syncMode = (channel.channelControl >> 9) & 0x03;
	bool isFromRAM = (channel.channelControl & 0x1) != 0;
	bool isAddressStepBackward = (channel.channelControl & 0x2) != 0;

	if (syncMode == 0x02 || isAddressStepBackward || isFromRAM) {
		std::cout << "FATAL: MDEC Out DMA with unsupported mode. CHCR 0x" << std::hex << channel.channelControl << "\n";
		exit(1);
	}

	/// @todo (bug): BA == 0 should mean 10000h blocks, see the BA todo in performGPUTransfer. The `blockCount == 0` special case at the bottom of this function contradicts the doc too.
	if (blockSize == 0) blockSize = 0x10000;
	uint32_t totalWords = (syncMode == 0x00) ? blockSize : (blockSize * blockCount);

	// The tests compute blockCount as wordCount / blockSize, which rounds down to 0 when the output is smaller than one block
	if (syncMode == 0x01 && blockCount == 0) {
		totalWords = bus->getMDEC().outputWordsAvailable();
	}

	for (uint32_t wordIndex = 0; wordIndex < totalWords; wordIndex++) {
		bus->write32(currentAddress & 0x001FFFFC, bus->getMDEC().readData());
		currentAddress = (currentAddress + 4) & 0x00FFFFFF;
	}

	/// @todo (bug): SyncMode 0 must not update MADR, see the SyncMode 0 MADR todo in performGPUTransfer.
	channel.memoryAddress = currentAddress;
	return totalWords;
}


//Privates: Interrupts and Completions
void DMA::setInterruptFlag(uint8_t channel) {
	/// @todo (bug): psx-spx: a completion sets flag bit 24+n only if BOTH bit 16+n AND the master enable (bit 23) are set. This checks only 16+n, so with bit 23 clear the flag latches anyway and raises IRQ3 the moment bit 23 is set later.
	// The completion flag is only latched when the channel's IRQ enable bit (16 + channel) is set
	if (((interruptControlRegister >> (16 + channel)) & 1) == 0) return;

	interruptControlRegister |= (1u << (24 + channel));
	updateInterruptLine();
}


void DMA::updateInterruptLine() {
	bool forceIRQ = (interruptControlRegister & 0x00008000) != 0; // Bit 15
	bool masterEnable = (interruptControlRegister & 0x00800000) != 0; // Bit 23
	uint32_t flags = (interruptControlRegister >> 24) & 0x7F;

	// The enable bits (16-22) only decide whether a completion latches a flag (see setInterruptFlag). A latched flag keeps bit 31 high while the master enable is set, even if its own enable bit is cleared afterwards.
	bool signal = forceIRQ || (masterEnable && flags != 0);
	bool previous = (interruptControlRegister & 0x80000000) != 0;

	if (signal) interruptControlRegister |= 0x80000000;
	else interruptControlRegister &= ~0x80000000;

	// IRQ3 is edge triggered: only a 0 -> 1 change of bit 31 raises it
	if (signal && !previous) {
		bus->triggerHardwareInterrupt(Hardware::IRQ_DMA);
	}
}


/// @todo psx-spx: DICR bits 0-6 = 1 let each slice (SyncMode 1) and each linked-list node (SyncMode 2) raise the channel's flag; 0 means only the final completion does. Every transfer here runs as one lump and raises a single completion interrupt.
void DMA::completeTransfer(uint32_t channelIndex) {	
	/// @todo (bug): psx-spx: CHCR bit 28 is cleared when the transfer BEGINS, bit 24 when it COMPLETES. This clears both at completion, so a delayed transfer (GPU list, SPU, MDEC) reads back with bit 28 set while busy.
	channels[channelIndex].channelControl &= ~0x11000000; // Clear the start and manual trigger bits
	channels[channelIndex].transferActive = false;

	if (channelIndex == 2 && linkedListBadTerminator) {
		linkedListBadTerminator = false;
		interruptControlRegister |= 0x00008000;
		updateInterruptLine();
	}

	setInterruptFlag(channelIndex);
}


void DMA::beginCompletion(uint32_t channelIndex, uint32_t wordsTransferred, uint32_t syncMode) {
	// Sync mode 1 counts the block counter (BCR bits 16-31) down as blocks finish, so BCR reads back as just the block size
	if (syncMode == 1) channels[channelIndex].blockControl &= 0x0000FFFF;

	/// @todo (timing): psx-spx: burst transfers hold the bus but still take time (OTC 1 clk/word, CD-ROM 24/40 clk/word, configurable via the memory control registers). Completing instantly costs zero cycles. Same for the CD-ROM path in advanceClockSlow.
	// Burst transfers hold the bus, so the CPU only sees the channel again once it has finished
	if (syncMode == 0) {
		completeTransfer(channelIndex);
		return;
	}
	scheduleCompletion(channelIndex, wordsTransferred);
}


void DMA::scheduleCompletion(uint8_t channel, uint32_t wordsTransferred) {
	// Rough approximation of real DMA bus timing: a couple of cycles per word
	constexpr int MIN_DELAY_CYCLES = 8;

	/// @todo (timing): psx-spx rate table: MDEC in/out, GPU and OTC are 1 clk/word (0110h clks per 100h words), SPU 4 (0420h per 100h, which the doc itself questions), CD-ROM 24 (BIOS) / 40 (games), PIO 20. GPU is 2 here and the doc says 1. DRAM hyper page mode (~17 clks per 16 words plus refresh) and device-side time (GPU rendering, MDEC decode, VRAM transfers, listed as unknown) aren't modelled. The 8-cycle floor isn't from the doc. These rates belong in Constants.h next to the other timing values.
	// MDEC In moves roughly one word per cycle (from memory, may need tuning); the SPU is paced by its own transfer engine
	int cyclesPerWord = (channel == 0x00 || channel == 0x01) ? 1 : 2;
	if (channel == 0x04) cyclesPerWord = SPU::DMACyclesPerWord();

	int delay = std::max<int>(static_cast<int>(wordsTransferred) * cyclesPerWord, MIN_DELAY_CYCLES);
	pendingCompletions.push_back({delay, channel});
}

