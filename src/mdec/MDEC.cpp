#include "MDEC.h"

uint32_t MDEC::readStatus() const {
	return status;
}

void MDEC::writeControl(uint32_t value) {
	if ((value & 0x80000000) != 0) {
		control = 0;
		status = STATUS_AFTER_RESET;
		return;
	}

	// Bits 30 and 29 enable the data-in and data-out DMA requests. Stored, but nothing acts on them yet.
	control = value;
}