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

	std::cout << "FATAL: Unhandled DMA read32 at address: 0x" << std::hex << address << "\n";
	exit(1);
}


void DMA::write32(uint32_t address, uint32_t value) {
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
		channels[3].channelControl &= ~START_BIT;
	}
}


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

	// Sync mode 0 ("burst") also needs the manual trigger bit. The OTC channel always behaves as mode 0.
	uint32_t syncMode = (channelIndex == 6) ? 0 : (channel.channelControl >> 9) & 0x03;

	// ...except on the SPU: its own DREQ starts the transfer (jaczekanski spu/memory-transfer sync mode 0 tests run without the trigger bit)
	bool needsManualTrigger = (syncMode == 0) && channelIndex != 4;
	if (needsManualTrigger && (channel.channelControl & TRIGGER_BIT) == 0) return;

	// The channel's master enable bit in DPCR must be set; otherwise the transfer just waits (ps1-tests dma/dpcr)
	if (!isChannelEnabledInDPCR(channelIndex)) return;

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
		bus->write32(currentAddress & 0x001FFFFC, nextAddress);
		currentAddress = previousAddress;
	}

	channel.memoryAddress = currentAddress;
	return blockCount;
}


void DMA::performCDROMTransfer() {
	Channel& channel = channels[3];

	uint32_t currentAddress = channel.memoryAddress & 0x00FFFFFC;

	uint32_t blockSize = channel.blockControl & 0xFFFF;
	uint32_t blockCount = (channel.blockControl >> 16) & 0xFFFF;
	uint8_t syncMode = (channel.channelControl >> 9) & 0x03;

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

	if (blockSize == 0) blockSize = 0x10000;
	uint32_t totalWords = (syncMode == 0x00) ? blockSize : (blockSize * blockCount);

	for (uint32_t wordIndex = 0; wordIndex < totalWords; wordIndex++) {
		bus->getMDEC().writeCommand(bus->read32(currentAddress & 0x001FFFFC));
		currentAddress = (currentAddress + 4) & 0x00FFFFFF;
	}

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

	channel.memoryAddress = currentAddress;
	return totalWords;
}


//Privates: Interrupts and Completions
void DMA::setInterruptFlag(uint8_t channel) {
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


void DMA::completeTransfer(uint32_t channelIndex) {	
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

	// MDEC In moves roughly one word per cycle (from memory, may need tuning); the SPU is paced by its own transfer engine
	int cyclesPerWord = (channel == 0x00 || channel == 0x01) ? 1 : 2;
	if (channel == 0x04) cyclesPerWord = SPU::DMACyclesPerWord();

	int delay = std::max<int>(static_cast<int>(wordsTransferred) * cyclesPerWord, MIN_DELAY_CYCLES);
	pendingCompletions.push_back({delay, channel});
}

