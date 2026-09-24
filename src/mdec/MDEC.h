#pragma once
#include <cstdint>

class MDEC {
public:
	uint32_t readStatus() const;

	/// Writes to 0x1F801820. Either a new command word, or a parameter word for the command in flight.
	void writeCommand(uint32_t value);

	/// Writes to 0x1F801824.
	void writeControl(uint32_t value);

private:
	// After a reset the data-out FIFO is empty and the current block is 4 (from memory, may need tuning)
	static constexpr uint32_t STATUS_AFTER_RESET = 0x80040000;

	enum class State {
		Idle,
		ReceivingQuantizationTables,
		ReceivingScaleTable,
	};

	void receiveParameterWord(uint32_t value);
	void refreshStatus();

	uint32_t control = 0;
	uint32_t status = STATUS_AFTER_RESET;

	State state = State::Idle;
	uint32_t parameterWordsRemaining = 0;
	uint32_t parameterIndex = 0;

	// Tables loaded by commands 0x02 and 0x03
	uint8_t luminanceQuantizationTable[64] = {};
	uint8_t chrominanceQuantizationTable[64] = {};
	int16_t scaleTable[64] = {};
};