#pragma once
#include <cstdint>

class MDEC {
public:
	uint32_t readStatus() const;
	void writeControl(uint32_t value);

private:
	// After a reset the data-out FIFO is empty and the current block is 4 (from memory, may need tuning)
	static constexpr uint32_t STATUS_AFTER_RESET = 0x80040000;

	uint32_t control = 0;
	uint32_t status = STATUS_AFTER_RESET;
};