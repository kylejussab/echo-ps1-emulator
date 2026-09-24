#include <cstdlib>
#include <iostream>
#include <iomanip>
#include "MDEC.h"


uint32_t MDEC::readStatus() const {
	return status;
}


void MDEC::writeControl(uint32_t value) {
	if ((value & 0x80000000) != 0) {
		control = 0;
		status = STATUS_AFTER_RESET;
		state = State::Idle;
		parameterWordsRemaining = 0;
		parameterIndex = 0;
		return;
	}

	// Bits 30 and 29 enable the data-in and data-out DMA requests.
	control = value;
	refreshStatus();
}


void MDEC::writeCommand(uint32_t value) {
	// If a command is waiting on parameters, this word is data, not a command
	if (parameterWordsRemaining > 0) {
		receiveParameterWord(value);
		return;
	}

	uint32_t command = value >> 29;

	switch (command) {
	case 0x02: {
		// Set Quantization Table: bit 0 = 0 -> luminance only (16 words), bit 0 = 1 -> luminance + chrominance (32 words)
		bool includesChrominance = (value & 1) != 0;
		state = State::ReceivingQuantizationTables;
		parameterWordsRemaining = includesChrominance ? 32 : 16;
		parameterIndex = 0;
		break;
	}
	case 0x03:
		// Set Scale Table: 64 signed halfwords = 32 words
		state = State::ReceivingScaleTable;
		parameterWordsRemaining = 32;
		parameterIndex = 0;
		break;
	default:
		// Command 0x01 (Decode Macroblock) will land here for now, bits 0-15 are its parameter word count
		std::cout << "FATAL: Unimplemented MDEC command 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << command
		          << " (word 0x" << std::setw(8) << value << ")\n";
		exit(1);
	}

	refreshStatus();
}


void MDEC::receiveParameterWord(uint32_t value) {
	switch (state) {
	case State::ReceivingQuantizationTables: {
		// First 16 words are the luminance table, the next 16 the chrominance table
		uint8_t* destinationTable = (parameterIndex < 16) ? luminanceQuantizationTable : chrominanceQuantizationTable;
		uint32_t baseIndex = (parameterIndex % 16) * 4;
		for (uint32_t byteIndex = 0; byteIndex < 4; byteIndex++) {
			destinationTable[baseIndex + byteIndex] = (value >> (8 * byteIndex)) & 0xFF;
		}
		break;
	}
	case State::ReceivingScaleTable:
		scaleTable[parameterIndex * 2 + 0] = static_cast<int16_t>(value & 0xFFFF);
		scaleTable[parameterIndex * 2 + 1] = static_cast<int16_t>(value >> 16);
		break;
	case State::Idle:
		break; // unreachable, parameterWordsRemaining is 0 when idle
	}

	parameterIndex++;
	parameterWordsRemaining--;
	if (parameterWordsRemaining == 0) {
		state = State::Idle;
	}

	refreshStatus();
}


void MDEC::refreshStatus() {
	// Keep bits 26-23 (output depth/signed/bit15), those will belong to the decode command later
	uint32_t updatedStatus = status & 0x07800000;

	updatedStatus |= 0x80000000;           // Data-out FIFO empty (nothing is produced yet)
	updatedStatus |= 4u << 16;             // Current block (from memory, may need tuning)

	bool waitingForParameters = parameterWordsRemaining > 0;
	if (waitingForParameters) {
		updatedStatus |= 1u << 29;         // Command busy
		if (control & (1u << 30)) {
			updatedStatus |= 1u << 28;     // Data-in request (DMA0 enabled)
		}
	}

	// Bits 0-15: remaining parameter words minus 1, 0xFFFF when none
	updatedStatus |= (parameterWordsRemaining - 1) & 0xFFFF;

	status = updatedStatus;
}