#include "DMA.h"
#include "Bus.h"
#include "Constants.h"
#include <iostream>
#include <algorithm>

DMA::DMA(Bus* bus) : bus(bus) {}


uint8_t DMA::read8(uint32_t address) {
	uint32_t alignedAddress = address & ~0x3u;
	uint32_t byteShift = (address & 0x3) * 8;
	return (read32(alignedAddress) >> byteShift) & 0xFF;
}


uint32_t DMA::read32(uint32_t address) {
	if (address == Hardware::REGISTER_DMA_CONTROL) {
		return controlRegister;
	}

	if (address == Hardware::REGISTER_DMA_INTERRUPT_CONTROL) {
		static int deadLockCounter = 0;
		static uint32_t lastInterruptControlValue = 0xFFFFFFFF;

		if (interruptControlRegister == lastInterruptControlValue) {
			deadLockCounter++;
			if (deadLockCounter == 50000) {
				std::cout << "[TEST] TRAPPED: Executable has polled DICR 50,000 times in a row. It is deadlocked waiting for a flag.\n"
					<< "Current DICR Value: 0x" << std::hex << interruptControlRegister << std::dec << "\n";
				deadLockCounter = 0; // Reset so it pulses a warning instead of spamming
			}
		}
		else {
			lastInterruptControlValue = interruptControlRegister;
			deadLockCounter = 0;
		}

		return interruptControlRegister;
	}

	bool isChannelRegister = address >= Hardware::REGISTER_DMA_BASE && address < Hardware::REGISTER_DMA_CONTROL && (address & 0x3) == 0 && (address & 0xF) <= 0x8;

	if (isChannelRegister) {
		uint32_t channelIndex = (address - Hardware::REGISTER_DMA_BASE) >> 4;

		if (channelIndex == 5) {
			std::cout << "FATAL: PIO DMA (Channel 5) Read Unimplemented at 0x" << std::hex << address << "\n";
			exit(1);
		}

		const Channel& channel = channels[channelIndex];
		switch (address & 0xF) {
			case 0x0: return channel.memoryAddress;
			case 0x4: return channel.blockControl;
			case 0x8: return channel.channelControl;
		}
	}

	std::cout << "FATAL: Unhandled DMA read32 at address: 0x" << std::hex << address << "\n";
	exit(1);
}


void DMA::write8(uint32_t address, uint8_t value) {
	bool isInterruptControlByte = address >= Hardware::REGISTER_DMA_INTERRUPT_CONTROL && address < Hardware::REGISTER_DMA_INTERRUPT_CONTROL + 4;

	if (isInterruptControlByte) {
		uint32_t byteIndex = address - Hardware::REGISTER_DMA_INTERRUPT_CONTROL;

		if (byteIndex == 0x03) {
			// Byte 3 holds the flags (bits 24-30), which are write-1-to-clear. Bit 31 is read-only.
			uint32_t flagsToClear = static_cast<uint32_t>(value & 0x7F) << 24;
			interruptControlRegister &= ~flagsToClear;
		}
		else {
			// Bytes 0-2 are plain read/write (bits 0-23, including the enables and master enable)
			uint32_t byteShift = byteIndex * 8;
			interruptControlRegister = (interruptControlRegister & ~(0xFFu << byteShift)) | (static_cast<uint32_t>(value) << byteShift);
		}

		updateInterruptLine();
		return;
	}

	std::cout << "FATAL: Unhandled DMA write8 at: 0x" << std::hex << address << "\n";
	exit(1);
}


void DMA::write32(uint32_t address, uint32_t value) {
	if (address == Hardware::REGISTER_DMA_CONTROL) {
		controlRegister = value;
		return;
	}

	if (address == Hardware::REGISTER_DMA_INTERRUPT_CONTROL) {
		uint32_t flagsToClear = value & 0x7F000000;
		interruptControlRegister &= ~flagsToClear;
		interruptControlRegister = (interruptControlRegister & 0xFF000000) | (value & 0x00FFFFFF);
		updateInterruptLine();
		return;
	}

	bool isChannelRegister = address >= Hardware::REGISTER_DMA_BASE && address < Hardware::REGISTER_DMA_CONTROL
		&& (address & 0x3) == 0 && (address & 0xF) <= 0x8;

	if (isChannelRegister) {
		uint32_t channelIndex = (address - Hardware::REGISTER_DMA_BASE) >> 4;

		if (channelIndex == 5) {
			std::cout << "FATAL: PIO DMA (Channel 5) Unimplemented at 0x" << std::hex << address << "\n";
			exit(1);
		}

		switch (address & 0xF) {
			case 0x0: channels[channelIndex].memoryAddress = value; return;
			case 0x4: channels[channelIndex].blockControl = value; return;
			case 0x8: writeChannelControl(channelIndex, value); return;
		}
	}

	std::cout << "FATAL: Unhandled DMA write32 at: 0x" << std::hex << address << "\n";
	exit(1);
}


void DMA::writeChannelControl(uint32_t channelIndex, uint32_t value) {
	Channel& channel = channels[channelIndex];
	channel.channelControl = value;

	if ((value & START_BIT) == 0) return;

	switch (channelIndex) {
		case 0x00: {
			uint32_t wordsTransferred = performMDECInTransfer();
			scheduleCompletion(0, wordsTransferred);
			break;
		}
		case 0x01: {
			std::cout << "FATAL: MDEC Out DMA (Channel 1) transfer started. CHCR 0x" << std::hex << value
				<< " address 0x" << channel.memoryAddress
				<< " block control 0x" << channel.blockControl << "\n";
			exit(1);
		}
		case 0x02: {
			performGPUTransfer((value & 0x00000001) == 0);

			channel.channelControl &= ~START_BIT;

			interruptControlRegister &= ~(1 << 26);
			updateInterruptLine();
			break;
		}
		case 0x03: {
			// Channel 3 waits for the CD-ROM to have data, so tick() starts it
			break;
		}
		case 0x04: {
			uint32_t wordsTransferred = performSPUTransfer();
			scheduleCompletion(4, wordsTransferred);
			break;
		}
		case 0x06: {
			performOrderingTableClearTransfer();
			channel.channelControl &= ~START_BIT;
			break;
		}
	}
}


void DMA::tick(int cycles) {
	// Fire any completions whose delay has elapsed
	for (size_t index = 0; index < pendingCompletions.size(); ) {
		pendingCompletions[index].cyclesRemaining -= cycles;
		if (pendingCompletions[index].cyclesRemaining <= 0) {
			channels[pendingCompletions[index].channel].channelControl &= ~0x11000000; // Clear the start and manual trigger bits
			setInterruptFlag(pendingCompletions[index].channel);
			pendingCompletions.erase(pendingCompletions.begin() + index);
		}
		else {
			index++;
		}
	}

	// Check if DMA Channel 3 is active and waiting for data
	bool isChannel3Enabled = (channels[3].channelControl & START_BIT) != 0;

	// Bit 6 (0x40) of the CD-ROM status register is "Data FIFO Not Empty"
	bool isDataReady = (bus->getCDROM().read8(Hardware::REG_CDROM_BASE) & 0x40) != 0;

	if (isChannel3Enabled && isDataReady) {
		performCDROMTransfer();
		channels[3].channelControl &= ~START_BIT;
	}
}


uint32_t DMA::performGPUTransfer(bool directionToRAM) {
	const Channel& channel = channels[2];

	uint8_t syncMode = (channel.channelControl >> 9) & 0x03;
	uint32_t blockSize = channel.blockControl & 0xFFFF;
	uint32_t blockCount = (channel.blockControl >> 16) & 0xFFFF;

	if (blockSize == 0) { blockSize = 0x10000; }

	uint32_t totalWords = (syncMode == 0) ? blockSize : (blockSize * blockCount);

	// Linked List Mode (Sync Mode 2)
	if (syncMode == 2) {
		uint32_t currentAddress = channel.memoryAddress & 0x001FFFFF;
		uint32_t wordsTransferred = 0;

		while (true) {
			uint32_t header = bus->read32(currentAddress);
			uint8_t wordCount = header >> 24;
			uint32_t nextAddress = header & 0x001FFFFF;

			uint32_t payloadAddress = currentAddress + 4;
			for (uint8_t wordIndex = 0; wordIndex < wordCount; wordIndex++) {
				bus->getGPU().writeGP0(bus->read32(payloadAddress));
				payloadAddress += 4;
			}
			wordsTransferred += wordCount;

			if ((header & 0x00FFFFFF) == 0x00FFFFFF) break; // End-of-list marker
			if (nextAddress == 0x000000) break;
			if (nextAddress == currentAddress) break;

			currentAddress = nextAddress;
		}
		return wordsTransferred;
	}

	// Block Transfer Mode (Sync Modes 0 and 1)
	uint32_t currentAddress = channel.memoryAddress;

	for (uint32_t wordIndex = 0; wordIndex < totalWords; wordIndex++) {
		if (directionToRAM) {
			bus->write32(currentAddress, bus->getGPU().readGP0());
		}
		else {
			bus->getGPU().writeGP0(bus->read32(currentAddress));
		}
		currentAddress += 4;
	}
	return totalWords;
}


void DMA::performOrderingTableClearTransfer() {
	const Channel& channel = channels[6];

	uint32_t currentAddress = channel.memoryAddress & 0x1FFFFFFF;
	uint32_t blockCount = channel.blockControl & 0xFFFF;

	if (blockCount == 0) blockCount = 0x10000;

	for (uint32_t blockIndex = 0; blockIndex < blockCount; blockIndex++) {
		uint32_t nextAddress = (blockIndex == blockCount - 1) ? 0x00FFFFFF : (currentAddress - 4);
		bus->write32(currentAddress, nextAddress);
		currentAddress -= 4;
	}

	setInterruptFlag(6);
}


void DMA::performCDROMTransfer() {
	const Channel& channel = channels[3];

	uint32_t currentAddress = channel.memoryAddress & 0x001FFFFF;

	uint32_t blockSize = channel.blockControl & 0xFFFF;
	uint32_t blockCount = (channel.blockControl >> 16) & 0xFFFF;
	uint8_t syncMode = (channel.channelControl >> 9) & 0x03;

	if (blockSize == 0) blockSize = 0x10000;

	// In Sync Mode 0 (Burst), blockCount is ignored.
	uint32_t totalWords = (syncMode == 0) ? blockSize : (blockSize * blockCount);

	uint32_t bytesAvailable = bus->getCDROM().getDataFIFOSize();
	uint32_t bytesRequested = totalWords * 4;

	if (bytesRequested > bytesAvailable) {
		std::cout << "FATAL: CD-ROM DMA requested 0x" << std::hex << bytesRequested
		          << " bytes but the data FIFO only holds 0x" << bytesAvailable << "\n";
		exit(1);
	}

	for (uint32_t wordIndex = 0; wordIndex < totalWords; wordIndex++) {
		uint8_t byte0 = bus->getCDROM().read8(0x1F801802);
		uint8_t byte1 = bus->getCDROM().read8(0x1F801802);
		uint8_t byte2 = bus->getCDROM().read8(0x1F801802);
		uint8_t byte3 = bus->getCDROM().read8(0x1F801802);

		uint32_t word = byte0 | (byte1 << 8) | (byte2 << 16) | (byte3 << 24);

		bus->write32(currentAddress, word);
		currentAddress += 4;
	}

	setInterruptFlag(3);
}


uint32_t DMA::performSPUTransfer() {
	const Channel& channel = channels[4];

	uint32_t currentAddress = channel.memoryAddress & 0x001FFFFC;

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
			bus->getSPU().writeDMAWord(bus->read32(currentAddress));
		}
		else {
			bus->write32(currentAddress, bus->getSPU().readDMAWord());
		}
		currentAddress += 4;
	}

	return totalWords;
}


uint32_t DMA::performMDECInTransfer() {
	const Channel& channel = channels[0];

	uint32_t currentAddress = channel.memoryAddress & 0x001FFFFC;

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
		bus->getMDEC().writeCommand(bus->read32(currentAddress));
		currentAddress += 4;
	}

	return totalWords;
}


void DMA::setInterruptFlag(uint8_t channel) {
	interruptControlRegister |= (1 << (24 + channel));
	updateInterruptLine();
}


void DMA::updateInterruptLine() {
	bool masterEnable = (interruptControlRegister & 0x00800000) != 0; // Bit 23
	uint32_t enables = (interruptControlRegister >> 16) & 0x7F;
	uint32_t flags = (interruptControlRegister >> 24) & 0x7F;

	// If the master enable is set and any unmasked flag is active, pull the IRQ3 line high
	if (masterEnable && (enables & flags) > 0) {
		interruptControlRegister |= 0x80000000; // Set Master IRQ Flag (Bit 31)
		bus->triggerHardwareInterrupt(Hardware::IRQ_DMA);
	}
	else {
		interruptControlRegister &= ~0x80000000;
	}
}


void DMA::scheduleCompletion(uint8_t channel, uint32_t wordsTransferred) {
	for (const auto& pending : pendingCompletions) {
		if (pending.channel == channel) {
			std::cout << "[WARN] BIOS triggered DMA channel " << static_cast<int>(channel) << " while a delayed completion was ALREADY in flight! Desync caught."
				<< " Cycles remaining on the earlier completion: " << pending.cyclesRemaining << "\n";
		}
	}

	// Rough approximation of real DMA bus timing: a couple of cycles per word
	constexpr int MIN_DELAY_CYCLES = 8;

	// MDEC In moves roughly one word per cycle (from memory, may need tuning); the SPU keeps the slower approximation
	int cyclesPerWord = (channel == 0x00) ? 1 : 2;

	int delay = std::max<int>(static_cast<int>(wordsTransferred) * cyclesPerWord, MIN_DELAY_CYCLES);
	pendingCompletions.push_back({delay, channel});
}