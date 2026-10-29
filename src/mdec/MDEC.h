#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>


/// @brief Macroblock Decoder (MDEC) handling image and video decompression.
class MDEC {
public:
	/// @brief Reads from Data/Command Register (0x1F801820). Also targeted by DMA channel 1 (MDEC -> RAM).
	/// @return The next 32-bit word of decoded output.
	uint32_t readData();


	/// @brief Writes to Data/Command Register (0x1F801820). Submits either a new command word, or a parameter word for the command in flight.
	/// @param value The 32-bit command or parameter word to write.
	void writeCommand(uint32_t value);

	
	/// @brief Writes to Control/Reset Register (0x1F801824).
	/// @param value The 32-bit control word to write.
	void writeControl(uint32_t value);


	/// @brief Reads from Status Register (0x1F801824).
	/// @return The current 32-bit status of the MDEC, including FIFO states and current configuration.
	uint32_t readStatus() const { return status; };


	/// @brief Checks if there is decoded output waiting to be read. DMA1 uses this to know when it can start.
	/// @return True if decoded output is available, false otherwise.
	bool hasOutput() const { return outputReadPosition < outputBytes.size(); }


	/// @brief Calculates the amount of decoded data available to read.
	/// @return The number of whole or partial words still waiting to be read.
	size_t outputWordsAvailable() const { return (outputBytes.size() - outputReadPosition + 3) / 4; }

private:
	enum class State {
		Idle,
		ReceivingQuantizationTables,
		ReceivingScaleTable,
		ReceivingMacroblockData,
	};


	enum OutputDepth : uint32_t { // Values of bits 28-27 of the Decode Macroblock command
		Depth4Bit = 0,
		Depth8Bit = 1,
		Depth24Bit = 2,
		Depth15Bit = 3,
	};


	// After a reset the data-out FIFO is empty and the current block is 4 (from memory, may need tuning)
	static constexpr uint32_t STATUS_AFTER_RESET = 0x80040000;
	static constexpr uint16_t END_OF_BLOCK = 0xFE00;
	static constexpr uint32_t BLOCK_INDEX_FOR_STEP[6] = { 4, 5, 0, 1, 2, 3 };

	static constexpr uint8_t ZIGZAG[64] = {
		0,  1,  8, 16,  9,  2,  3, 10,
		17, 24, 32, 25, 18, 11,  4,  5,
		12, 19, 26, 33, 40, 48, 41, 34,
		27, 20, 13,  6,  7, 14, 21, 28,
		35, 42, 49, 56, 57, 50, 43, 36,
		29, 22, 15, 23, 30, 37, 44, 51,
		58, 59, 52, 45, 38, 31, 39, 46,
		53, 60, 61, 54, 47, 55, 62, 63,
	};

	uint32_t control = 0;
	uint32_t status = STATUS_AFTER_RESET;

	State state = State::Idle;
	uint32_t parameterWordsRemaining = 0;
	uint32_t parameterIndex = 0;

	// Tables loaded by commands 0x02 and 0x03
	uint8_t luminanceQuantizationTable[64] = {};
	uint8_t chrominanceQuantizationTable[64] = {};
	int16_t scaleTable[64] = {};

	size_t inputReadPosition = 0;
	uint32_t decodeStep = 0; // Next block of the current macroblock: 0 = Cr, 1 = Cb, 2..5 = Y1..Y4
	int16_t macroblockBlocks[6][64] = {};

	// Settings of the Decode Macroblock command in flight
	uint32_t outputDepth = Depth24Bit;
	bool outputSigned = false;
	bool outputBit15 = false;

	// Compressed input (two halfwords per received word) and decoded output
	std::vector<uint16_t> inputHalfwords;
	std::vector<uint8_t> outputBytes;
	size_t outputReadPosition = 0;


	/// @brief Sign-extends a 10-bit value to a 32-bit integer.
	/// @param value The 10-bit value extracted from a compressed halfword.
	/// @return The sign-extended 32-bit integer.
	int32_t signExtend10(uint16_t value) const;


	/// @brief Processes an incoming parameter word based on the current decoder state.
	/// @param value The 32-bit parameter word received from the command register.
	void receiveParameterWord(uint32_t value);


	/// @brief Updates the internal status register reflecting FIFO states and current configuration.
	void refreshStatus();


	/// @brief Decodes complete blocks as buffered input allows and appends finished macroblocks to outputBytes.
	void decodeAvailableBlocks();


	/// @brief Run-length decodes, dequantizes, and performs IDCT on one 8x8 block.
	/// @param position The current read position in the input buffer (passed by reference to update).
	/// @param block Pointer to the 8x8 block array to be populated with decoded data.
	/// @param quantizationTable Pointer to the 64-byte quantization table to use (luminance or chrominance).
	/// @return True if a block was successfully decoded, false if the input ran out.
	bool decodeBlock(size_t& position, int16_t* block, const uint8_t* quantizationTable) const;


	/// @brief Performs Inverse Discrete Cosine Transform on an 8x8 block.
	/// @param block Pointer to the 8x8 block of frequency-domain coefficients to transform into spatial domain.
	void inverseDCT(int16_t* block) const;


	/// @brief Converts YCbCr macroblock data to RGB/color output format.
	/// @param chromaRedBlock Pointer to the 8x8 Cr (red-difference) chrominance block.
	/// @param chromaBlueBlock Pointer to the 8x8 Cb (blue-difference) chrominance block.
	/// @param lumaBlocks Pointer to the four 8x8 Y (luminance) blocks.
	void outputColorMacroblock(const int16_t* chromaRedBlock, const int16_t* chromaBlueBlock, const int16_t (*lumaBlocks)[64]);



	/// @brief Converts Y block data to monochrome output format.
	/// @param block Pointer to the 8x8 Y (luminance) block.
	void outputMonochromeBlock(const int16_t* block);


	/// @brief Resets decoder state machine and flushes FIFOs.
	void resetDecoderState();
};