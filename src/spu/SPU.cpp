#include "SPU.h"
#include <iomanip>

SPU::SPU() {
	soundRam.resize(SOUND_RAM_SIZE, 0);
}

uint32_t SPU::registerIndex(uint32_t address) {
	return ((address & ~1u) - REGISTER_BASE_ADDRESS) >> 1;
}

uint16_t SPU::read16(uint32_t address) {
	// SPUSTAT mirrors the low bits of SPUCNT (from memory, so this may need tuning)
	if ((address & ~1u) == REGISTER_STATUS) {
		return registers[registerIndex(REGISTER_CONTROL)] & 0x003F;
	}
	return registers[registerIndex(address)];
}


void SPU::write16(uint32_t address, uint16_t value) {
	address &= ~1u;
	registers[registerIndex(address)] = value;

	if (address == REGISTER_TRANSFER_ADDRESS) {
		// The register counts in 8-byte units
		transferAddress = (static_cast<uint32_t>(value) * 8) % SOUND_RAM_SIZE;
	}
	else if (address == REGISTER_TRANSFER_FIFO) {
		writeTransferFifo(value);
	}
}


uint32_t SPU::read32(uint32_t address) {
	uint32_t lowHalf = read16(address);
	uint32_t highHalf = read16(address + 2);
	return lowHalf | (highHalf << 16);
}


void SPU::write32(uint32_t address, uint32_t value) {
	write16(address, value & 0xFFFF);
	write16(address + 2, value >> 16);
}


void SPU::writeDMAWord(uint32_t word) {
	for (uint32_t byteIndex = 0; byteIndex < 4; byteIndex++) {
		soundRam[(transferAddress + byteIndex) % SOUND_RAM_SIZE] = (word >> (byteIndex * 8)) & 0xFF;
	}
	transferAddress = (transferAddress + 4) % SOUND_RAM_SIZE;
}


uint32_t SPU::readDMAWord() {
	uint32_t word = 0;
	for (uint32_t byteIndex = 0; byteIndex < 4; byteIndex++) {
		word |= static_cast<uint32_t>(soundRam[(transferAddress + byteIndex) % SOUND_RAM_SIZE]) << (byteIndex * 8);
	}
	transferAddress = (transferAddress + 4) % SOUND_RAM_SIZE;
	return word;
}



void SPU::writeTransferFifo(uint16_t value) {
	soundRam[transferAddress] = value & 0xFF;
	soundRam[transferAddress + 1] = value >> 8;
	transferAddress = (transferAddress + 2) % SOUND_RAM_SIZE;
}