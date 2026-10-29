#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include "MDEC.h"


uint32_t MDEC::readData() {
	uint32_t word = 0;
	for (uint32_t byteIndex = 0; byteIndex < 4; byteIndex++) {
		if (outputReadPosition < outputBytes.size()) {
			word |= static_cast<uint32_t>(outputBytes[outputReadPosition++]) << (8 * byteIndex);
		}
	}

	refreshStatus();
	return word;
}


void MDEC::writeCommand(uint32_t value) {
	// If a command is waiting on parameters, this word is data, not a command
	if (parameterWordsRemaining > 0) {
		receiveParameterWord(value);
		return;
	}

	uint32_t command = value >> 29;

	switch (command) {
		case 0x00: {
			// No function: accepted and ignored. (ps1-tests cpu/code-in-io pushes arbitrary words at this port.)
			break;
		}
		case 0x01: {
			// Decode Macroblock(s)
			// Bits 28-27 = output depth, bit 26 = output signed, bit 25 = output bit15 (15-bit only), bits 0-15 = parameter word count
			outputDepth = (value >> 27) & 0x03;
			outputSigned = (value & (1u << 26)) != 0;
			outputBit15 = (value & (1u << 25)) != 0;

			// Status bits 26-23 mirror command bits 28-25
			status = (status & ~0x07800000u) | (((value >> 25) & 0x0F) << 23);

			// A new command can arrive while the previous one's output is still unread. Hardware doesn't throw that away,
			// so keep the unread part and let the new output append after it.
			outputBytes.erase(outputBytes.begin(), outputBytes.begin() + outputReadPosition);
			outputReadPosition = 0;
			inputHalfwords.clear();
			inputReadPosition = 0;
			decodeStep = 0;

			parameterWordsRemaining = value & 0xFFFF;
			parameterIndex = 0;

			if (parameterWordsRemaining > 0) {
				state = State::ReceivingMacroblockData;
				inputHalfwords.reserve(parameterWordsRemaining * 2);
			}

			break;
	}
		case 0x02: {
			// Set Quantization Table: bit 0 = 0 -> luminance only (16 words), bit 0 = 1 -> luminance + chrominance (32 words)
			bool includesChrominance = (value & 1) != 0;
			state = State::ReceivingQuantizationTables;
			parameterWordsRemaining = includesChrominance ? 32 : 16;
			parameterIndex = 0;
			break;
		}
		case 0x03: {
			// Set Scale Table: 64 signed halfwords = 32 words
			state = State::ReceivingScaleTable;
			parameterWordsRemaining = 32;
			parameterIndex = 0;
			break;
		}
		default: {
			std::cout << "FATAL: Unimplemented MDEC command 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << command << " (word 0x" << std::setw(8) << value << ")\n";
			exit(1);
		}	
	}

	refreshStatus();
}


void MDEC::writeControl(uint32_t value) {
	if ((value & 0x80000000) != 0) {
		control = 0;
		status = STATUS_AFTER_RESET;
		state = State::Idle;
		parameterWordsRemaining = 0;
		parameterIndex = 0;
		resetDecoderState();
		return;
	}

	// Bits 30 and 29 enable the data-in and data-out DMA requests.
	control = value;
	refreshStatus();
}



//Privates
int32_t MDEC::signExtend10(uint16_t value) const {
	int32_t result = value & 0x3FF;
	if (result & 0x200) {
		result -= 0x400;
	}
	return result;
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
		case State::ReceivingScaleTable: {
			scaleTable[parameterIndex * 2 + 0] = static_cast<int16_t>(value & 0xFFFF);
			scaleTable[parameterIndex * 2 + 1] = static_cast<int16_t>(value >> 16);
			break;
		}
		case State::ReceivingMacroblockData: {
			// Low halfword comes first in the stream
			inputHalfwords.push_back(static_cast<uint16_t>(value & 0xFFFF));
			inputHalfwords.push_back(static_cast<uint16_t>(value >> 16));
			decodeAvailableBlocks();
			break;
		}
		case State::Idle: {
			break; // unreachable, parameterWordsRemaining is 0 when idle
		}
	}

	parameterIndex++;
	parameterWordsRemaining--;

	if (parameterWordsRemaining == 0) {
		if (state == State::ReceivingMacroblockData) {
			// Anything left over is an incomplete block and gets dropped
			inputHalfwords.clear();
			inputReadPosition = 0;
			decodeStep = 0;
		}
		state = State::Idle;
	}

	refreshStatus();
}


void MDEC::refreshStatus() {
	// Keep bits 26-23 (output depth/signed/bit15), those are set by the decode command
	uint32_t updatedStatus = status & 0x07800000;

	bool outputAvailable = outputReadPosition < outputBytes.size();
	
	if (!outputAvailable) {
		updatedStatus |= 0x80000000; // Data-out FIFO empty
	} 
	else if (control & (1u << 29)) {
		updatedStatus |= 1u << 27; // Data-out request (DMA1 enabled)
	}

	updatedStatus |= BLOCK_INDEX_FOR_STEP[decodeStep] << 16;

	bool waitingForParameters = parameterWordsRemaining > 0;

	if (waitingForParameters) {
		updatedStatus |= 1u << 29; // Command busy

		if (control & (1u << 30)) {
			updatedStatus |= 1u << 28; // Data-in request (DMA0 enabled)
		}
	}

	// Bits 0-15: remaining parameter words minus 1, 0xFFFF when none
	updatedStatus |= (parameterWordsRemaining - 1) & 0xFFFF;

	status = updatedStatus;
}


void MDEC::decodeAvailableBlocks() {
	bool isColor = (outputDepth == Depth24Bit) || (outputDepth == Depth15Bit);

	while (true) {
		// Only commit the read position once the whole block is present, otherwise retry when more words arrive
		size_t trialPosition = inputReadPosition;

		if (isColor) {
			// Order within a macroblock: Cr, Cb, Y1 (top-left), Y2 (top-right), Y3 (bottom-left), Y4 (bottom-right)
			const uint8_t* quantizationTable = (decodeStep < 2) ? chrominanceQuantizationTable : luminanceQuantizationTable;

			if (!decodeBlock(trialPosition, macroblockBlocks[decodeStep], quantizationTable)) return;

			inputReadPosition = trialPosition;
			decodeStep++;

			if (decodeStep == 6) {
				decodeStep = 0;
				outputColorMacroblock(macroblockBlocks[0], macroblockBlocks[1], macroblockBlocks + 2);
			}
		} 
		else {
			if (!decodeBlock(trialPosition, macroblockBlocks[0], luminanceQuantizationTable)) return;

			inputReadPosition = trialPosition;
			outputMonochromeBlock(macroblockBlocks[0]);
		}
	}
}


bool MDEC::decodeBlock(size_t& position, int16_t* block, const uint8_t* quantizationTable) const {
	for (int i = 0; i < 64; i++) block[i] = 0;

	uint16_t halfword = END_OF_BLOCK;

	while (halfword == END_OF_BLOCK) {
		if (position >= inputHalfwords.size()) {
			return false;
		}
		halfword = inputHalfwords[position++];
	}

	uint32_t index = 0;
	int32_t quantizationScale = (halfword >> 10) & 0x3F;
	int32_t value = signExtend10(halfword) * quantizationTable[0];

	if (quantizationScale == 0) {
		value = signExtend10(halfword) * 2;
	}

	value = std::clamp(value, -0x400, 0x3FF);

	while (true) {
		if (quantizationScale == 0) {
			block[index] = static_cast<int16_t>(value);
		} 
		else {
			block[ZIGZAG[index]] = static_cast<int16_t>(value);
		}

		if (index == 63) {
			break;
		}

		if (position >= inputHalfwords.size()) {
			return false;
		}

		halfword = inputHalfwords[position++];

		index += ((halfword >> 10) & 0x3F) + 1;

		if (index >= 64) {
			break;
		}

		value = (signExtend10(halfword) * quantizationTable[index] * quantizationScale + 4) / 8;

		if (quantizationScale == 0) {
			value = signExtend10(halfword) * 2;
		}

		value = std::clamp(value, -0x400, 0x3FF);
	}

	inverseDCT(block);
	return true;
}


void MDEC::inverseDCT(int16_t* block) const {
	int32_t buffers[2][64];

	for (int i = 0; i < 64; i++) buffers[0][i] = block[i];

	// Two identical passes, each one transposes the data so after both it is back in row-major order
	for (int pass = 0; pass < 2; pass++) {
		const int32_t* source = buffers[pass];
		int32_t* destination = buffers[pass ^ 1];

		for (int x = 0; x < 8; x++) {
			for (int y = 0; y < 8; y++) {
				int32_t sum = 0;

				for (int z = 0; z < 8; z++) {
					sum += source[y + z * 8] * static_cast<int32_t>(scaleTable[x + z * 8]);
				}

				destination[x + y * 8] = (sum + 0x8000) >> 16;
			}
		}
	}

	for (int i = 0; i < 64; i++) block[i] = static_cast<int16_t>(std::clamp(buffers[0][i], -128, 127));
}


void MDEC::outputColorMacroblock(const int16_t* chromaRedBlock, const int16_t* chromaBlueBlock, const int16_t (*lumaBlocks)[64]) {
	for (int pixelY = 0; pixelY < 16; pixelY++) {
		for (int pixelX = 0; pixelX < 16; pixelX++) {
			int blockIndex = (pixelX / 8) + (pixelY / 8) * 2;
			int32_t luma = lumaBlocks[blockIndex][(pixelX % 8) + (pixelY % 8) * 8];

			int32_t chromaRed = chromaRedBlock[(pixelX / 2) + (pixelY / 2) * 8];
			int32_t chromaBlue = chromaBlueBlock[(pixelX / 2) + (pixelY / 2) * 8];

			int32_t red = luma + ((1436 * chromaRed) >> 10);
			int32_t blue = luma + ((1815 * chromaBlue) >> 10);
			int32_t green = luma + ((-352 * chromaBlue - 731 * chromaRed) >> 10);

			red = std::clamp(red, -128, 127);
			green = std::clamp(green, -128, 127);
			blue = std::clamp(blue, -128, 127);

			if (outputDepth == Depth15Bit) {
				red = std::clamp(red + 4, -128, 127);
				green = std::clamp(green + 4, -128, 127);
				blue = std::clamp(blue + 4, -128, 127);
			}

			if (!outputSigned) {
				red += 128;
				green += 128;
				blue += 128;
			}

			uint32_t r = static_cast<uint32_t>(red) & 0xFF;
			uint32_t g = static_cast<uint32_t>(green) & 0xFF;
			uint32_t b = static_cast<uint32_t>(blue) & 0xFF;

			if (outputDepth == Depth24Bit) {
				outputBytes.push_back(static_cast<uint8_t>(r));
				outputBytes.push_back(static_cast<uint8_t>(g));
				outputBytes.push_back(static_cast<uint8_t>(b));
			} 
			else {
				uint32_t pixel = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (outputBit15 ? 0x8000u : 0u);
				outputBytes.push_back(static_cast<uint8_t>(pixel & 0xFF));
				outputBytes.push_back(static_cast<uint8_t>(pixel >> 8));
			}
		}
	}
}


void MDEC::outputMonochromeBlock(const int16_t* block) {
	uint8_t pixels[64];
	for (int i = 0; i < 64; i++) {
		int32_t luma = block[i];

		if (!outputSigned) {
			luma += 128;
		}

		pixels[i] = static_cast<uint8_t>(luma & 0xFF);
	}

	if (outputDepth == Depth8Bit) {
		for (int i = 0; i < 64; i++) outputBytes.push_back(pixels[i]);
	} 
	else {
		uint8_t nibbles[64];
		for (int i = 0; i < 64; i++) {
			int32_t luma = std::clamp(static_cast<int32_t>(block[i]) + 8, -128, 127);

			if (!outputSigned) {
				luma += 128;
			}

			nibbles[i] = static_cast<uint8_t>((luma >> 4) & 0xF);
		}
		for (int i = 0; i < 64; i += 2) {
			outputBytes.push_back(static_cast<uint8_t>(nibbles[i] | (nibbles[i + 1] << 4)));
		}
	}
}


void MDEC::resetDecoderState() {
	inputHalfwords.clear();
	inputReadPosition = 0;
	decodeStep = 0;
	outputBytes.clear();
	outputReadPosition = 0;
}

