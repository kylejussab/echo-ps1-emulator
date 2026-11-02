// The SIO1 registers (the serial port, 0x1F801050-0x1F80105F): what holds what is written, which bits are always zero, the baud timer in
// the status register, and what a read of the data register returns at 8, 16 and 32 bits. How bytes are sent and received, and the
// interrupts, are in sio1_transfer. Needs no BIOS and no devices.
//
// What is checked is what the stable psx-spx page "Serial Interfaces (SIO)" documents. In particular:
//   - MODE bits 8-15 are always zero on SIO1 (bit 8 is SIO0 only, 9-15 are unused), and CTRL bits 14-15 are always zero. CTRL bit 4 (acknowledge)
//     and bit 6 (reset) are write-only. CTRL bit 7 is read/write only while the baud factor in MODE is not 0.
//   - The baud timer (STAT bits 11-31) is decremented on every CPU cycle and reloaded when BAUD is written and when it reaches zero. On a reload
//     it receives (reload * factor) / 2.
//   - DR, read: bits 0-7 are the oldest RX FIFO entry and bits 8-15, 16-23 and 24-31 preview the next three. A 16-bit read removes ONE entry
//     and a 32-bit read removes FOUR.
// Left out on purpose because the page says "probably" or "eventually" or gives no value: the SIO1_MISC register at 0x1F80105C, the power-on
// values of STAT bits 0 and 2 (they depend on TXEN and CTS), the RX input level in STAT bit 6, the value of a read from an empty RX FIFO, narrow
// WRITES, and what a reset does to BAUD.
#include "io/SIO1.h"
#include "support/echo_check.h"
#include <cstdint>
#include <string>

using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;

namespace {

constexpr uint32_t SIO1_DR = 0x1F801050;
constexpr uint32_t SIO1_SR = 0x1F801054;
constexpr uint32_t SIO1_MR = 0x1F801058;
constexpr uint32_t SIO1_CR = 0x1F80105A;
constexpr uint32_t SIO1_BR = 0x1F80105E;

constexpr uint16_t CR_RXEN = 1u << 2;

constexpr uint16_t MODE_8N1_MUL16 = 0x004E;   // 8 data bits, no parity, 1 stop bit, baud factor MUL16 (the example psx-spx gives)
constexpr uint16_t MODE_8N1_MUL1 = 0x004D;
constexpr uint16_t MODE_8N1_MUL64 = 0x004F;

uint32_t timerOf(SIO1& sio) { return sio.read32(SIO1_SR) >> 11; }

void checkPowerOn() {
	SIO1 sio;
	uint32_t status = sio.read32(SIO1_SR);
	// bit 1 (RX not empty), 3-5 (error flags), 9 (interrupt) and 10 (unused) all clear
	check((status & 0x0000063A) == 0, "power-on: no received byte, no error flag, no interrupt flag and the unused bit 10 clear", "STAT is " + hex(status, 8));
}

void checkRegisterReadback() {
	{
		SIO1 sio;
		sio.write16(SIO1_BR, 0x00DC);
		checkEqual(sio.read16(SIO1_BR), 0x00DC, "BAUD holds what is written (00DCh, 9600 bps at MUL16)");
	}
	{
		SIO1 sio;
		sio.write16(SIO1_MR, MODE_8N1_MUL16);
		checkEqual(sio.read16(SIO1_MR), MODE_8N1_MUL16, "MODE holds what is written (004Eh, 8N1 at MUL16)");
	}
	{
		SIO1 sio;
		sio.write16(SIO1_MR, 0xFFFF);
		check(sio.read16(SIO1_MR) == 0x00FF, "MODE bits 8-15 are always zero: writing FFFFh reads back 00FFh", "read " + hex(sio.read16(SIO1_MR), 4));
	}
	{
		SIO1 sio;
		sio.write16(SIO1_MR, MODE_8N1_MUL16);
		sio.write16(SIO1_CR, 0x1FAF);   // bits 0-3, 5, 7-12: every writable bit that has a defined meaning (bit 7 needs a non-zero baud factor)
		checkEqual(sio.read16(SIO1_CR), 0x1FAF, "CTRL holds what is written (bits 0-3, 5, 7-12)");
	}
	{
		SIO1 sio;
		sio.write16(SIO1_MR, MODE_8N1_MUL16);
		sio.write16(SIO1_CR, 0x1FAF | 0x0010);
		checkEqual(sio.read16(SIO1_CR), 0x1FAF, "CTRL bit 4 (acknowledge) is write-only: it reads back as 0");
	}
	{
		SIO1 sio;
		sio.write16(SIO1_MR, MODE_8N1_MUL16);
		sio.write16(SIO1_CR, 0xDFAF);
		check((sio.read16(SIO1_CR) & 0xC000) == 0, "CTRL bits 14-15 are always zero", "wrote DFAFh, read " + hex(sio.read16(SIO1_CR), 4));
	}
	{
		SIO1 sio;
		sio.write16(SIO1_MR, MODE_8N1_MUL16);
		sio.write16(SIO1_CR, 0x1003);
		checkEqual(sio.read32(SIO1_MR), 0x1003004E, "a 32-bit read at MODE returns MODE in the low half and CTRL in the high half");
	}
	{
		SIO1 sio;
		sio.write32(SIO1_MR, 0x1003004E);
		check(sio.read16(SIO1_MR) == 0x004E && sio.read16(SIO1_CR) == 0x1003, "a 32-bit write at MODE sets MODE (low half) and CTRL (high half)",
			"MODE " + hex(sio.read16(SIO1_MR), 4) + ", CTRL " + hex(sio.read16(SIO1_CR), 4));
	}
	{
		SIO1 sio;
		sio.write16(SIO1_BR, 0x00DC);
		checkEqual(sio.read32(0x1F80105C) >> 16, 0x00DC, "a 32-bit read at 1F80105Ch returns BAUD in the high half");
	}
}

// The baud timer starts from (reload * factor) / 2 on a write to BAUD and counts down by one every CPU cycle
void checkBaudTimer() {
	struct Case {
		uint16_t mode;
		uint16_t reload;
		uint32_t start;
		const char* name;
	};
	const Case cases[] = {
		{MODE_8N1_MUL16, 0x0040, 512, "reload 40h, factor 16: (40h * 16) / 2 = 512"},
		{MODE_8N1_MUL1, 0x0088, 68, "reload 88h, factor 1: 88h / 2 = 68"},
		{MODE_8N1_MUL64, 0x0010, 512, "reload 10h, factor 64: (10h * 64) / 2 = 512"},
	};
	for (const Case& testCase : cases) {
		SIO1 sio;
		sio.write16(SIO1_MR, testCase.mode);
		sio.write16(SIO1_BR, testCase.reload);
		if (&testCase == &cases[0]) {
			checkEqual(timerOf(sio), testCase.start, std::string("baud timer, ") + testCase.name + ": the timer is reloaded by the write to BAUD");
		}
		sio.advanceClock(10);
		uint32_t value = timerOf(sio);
		check(value == testCase.start - 10, std::string("baud timer, ") + testCase.name + ": it counts down one per cycle (10 cycles later it is " + std::to_string(testCase.start - 10) + ")",
			"read " + std::to_string(value));
	}
	{
		SIO1 sio;
		sio.write16(SIO1_MR, MODE_8N1_MUL16);
		sio.write16(SIO1_BR, 0x0400);
		uint32_t before = timerOf(sio);
		sio.advanceClock(16);
		uint32_t after = timerOf(sio);
		check(after < before, "baud timer: it decrements as time passes", "before " + std::to_string(before) + ", after 16 cycles " + std::to_string(after));
	}
}

// DR when read: the oldest entry in bits 0-7, the next three previewed above it. 16 bits removes one entry, 32 bits removes four.
void checkDataRegisterReads() {
	auto prepared = [](SIO1& sio) {
		sio.write16(SIO1_MR, MODE_8N1_MUL16);
		sio.write16(SIO1_CR, CR_RXEN);
		for (uint8_t byte : {0x11, 0x22, 0x33, 0x44, 0x55}) sio.receiveByte(byte);
	};
	{
		SIO1 sio;
		prepared(sio);
		checkEqual(sio.read8(SIO1_DR), 0x11, "DR: an 8-bit read returns the oldest entry");
		checkEqual(sio.read8(SIO1_DR), 0x22, "DR: an 8-bit read removes exactly one entry");
	}
	{
		SIO1 sio;
		prepared(sio);
		uint16_t value = sio.read16(SIO1_DR);
		check(value == 0x2211, "DR: a 16-bit read returns the oldest entry and previews the second in bits 8-15", "read " + hex(value, 4) + ", expected 0x2211");
		checkEqual(sio.read8(SIO1_DR), 0x22, "DR: a 16-bit read removes only one entry");
	}
	{
		SIO1 sio;
		prepared(sio);
		uint32_t value = sio.read32(SIO1_DR);
		check(value == 0x44332211, "DR: a 32-bit read returns the oldest entry and previews the next three", "read " + hex(value, 8) + ", expected 0x44332211");
		checkEqual(sio.read8(SIO1_DR), 0x55, "DR: a 32-bit read removes four entries");
	}
}

// Reading part of a register through a byte access returns that part of it (the same assumption as the narrow reads of the other registers)
void checkNarrowReads() {
	SIO1 sio;
	sio.write16(SIO1_MR, MODE_8N1_MUL16);
	sio.write16(SIO1_CR, 0x1003);
	sio.write16(SIO1_BR, 0x00DC);
	struct Case {
		uint32_t address;
		uint8_t expected;
		const char* name;
	};
	const Case cases[] = {
		{SIO1_MR, 0x4E, "MODE byte 0"},
		{SIO1_CR, 0x03, "CTRL byte 0"},
		{SIO1_CR + 1, 0x10, "CTRL byte 1"},
		{SIO1_BR, 0xDC, "BAUD byte 0"},
	};
	for (const Case& testCase : cases) {
		uint8_t value = sio.read8(testCase.address);
		check(value == testCase.expected, std::string("narrow reads: ") + testCase.name + " is that part of the 16-bit register", "read " + hex(value, 2) + ", expected " + hex(testCase.expected, 2));
	}
}

} // namespace

int main() {
	checkPowerOn();
	checkRegisterReadback();
	checkBaudTimer();
	checkDataRegisterReads();
	checkNarrowReads();
	return echo_check::finish();
}
