// The SIO0 registers (JOY_DATA 0x1F801040, JOY_STAT 0x1F801044, JOY_MODE 0x1F801048, JOY_CTRL 0x1F80104A, JOY_BAUD 0x1F80104E) and how a byte
// sent to the pad shows up in them: the status bits while a byte is shifting, how long a byte takes at each baud setting, the /ACK pulse and
// the interrupt flag it raises, the reset bit, reads through narrower accesses than the register's own width, and IRQ7 reaching I_STAT.
// What the pad answers is in sio_pad_protocol. Needs no BIOS and no disc; there is no host gamepad in a test run.
//
// What is checked is what the hardware documents (psx-spx "Serial Port (SIO)", "Controller and Memory Card I/O Ports"): JOY_STAT bit 1 is
// "received byte waiting" and is cleared by reading JOY_DATA, bit 2 is "transmit idle", bit 7 is the /ACK level, bit 9 is the interrupt flag set
// by an /ACK while the DSR interrupt is enabled (JOY_CTRL bit 12) and cleared by writing JOY_CTRL bit 4; a bit takes reload * multiplier cycles and a byte is 8 bits.
// The narrow reads rely on one assumption rather than a documented bit: reading part of a 32-bit-aligned register through a byte or halfword
// access returns that part of it (the same assumption as the DICR / DPCR narrow reads in dma_interrupt_control).
// Left out on purpose because the hardware behaviour is not confirmed: what a narrow WRITE to JOY_CTRL or JOY_MODE does to the other half, the baud
// timer in JOY_STAT bits 11-31, writing JOY_DATA while transmit is disabled, and what a baud reload of 0 does.
#include "core/Bus.h"
#include "io/SIO0.h"
#include "support/echo_check.h"
#include <cstdint>
#include <string>

using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;

namespace {

constexpr uint32_t JOY_DATA = 0x1F801040;
constexpr uint32_t JOY_STAT = 0x1F801044;
constexpr uint32_t JOY_MODE = 0x1F801048;
constexpr uint32_t JOY_CTRL = 0x1F80104A;
constexpr uint32_t JOY_BAUD = 0x1F80104E;
constexpr uint32_t INTERRUPT_STATUS = 0x1F801070;
constexpr uint32_t IRQ7_PAD = 0x00000080;

constexpr uint16_t STAT_RX_READY = 1u << 1;
constexpr uint16_t STAT_TX_IDLE = 1u << 2;
constexpr uint16_t STAT_DSR = 1u << 7;
constexpr uint16_t STAT_IRQ = 1u << 9;

constexpr uint16_t CTRL_SELECTED = 0x1003;            // transmit enable, DTR (selects the pad) and the DSR interrupt enable
constexpr uint16_t CTRL_SELECTED_NO_IRQ = 0x0003;     // the same without the DSR interrupt enable
constexpr uint16_t CTRL_PORT2_SELECTED = 0x3003;      // select port 2, where nothing is attached
constexpr uint16_t CTRL_ACKNOWLEDGE = 0x10;           // bit 4, write-only
constexpr uint16_t CTRL_RESET = 0x40;                 // bit 6, write-only
constexpr uint16_t MODE_BIOS = 0x000D;
constexpr uint16_t BAUD_BIOS = 0x0088;
constexpr int CYCLES_PER_BYTE = 3000;                 // the byte, its reply and the /ACK pulse are all done within this at the BIOS baud setting

uint16_t statusOf(SIO0& sio) { return sio.read16(JOY_STAT); }

struct Observed {
	int interrupts = 0;   // times advanceClock reported IRQ7
	int ackPulses = 0;    // rising edges of JOY_STAT bit 7
};

// Runs `cycles` CPU cycles, watching the /ACK level and counting interrupts
Observed observe(SIO0& sio, int cycles) {
	Observed observed;
	bool dsrWasHigh = (statusOf(sio) & STAT_DSR) != 0;
	for (int cycle = 0; cycle < cycles; cycle++) {
		if (sio.advanceClock()) observed.interrupts++;
		bool dsrHigh = (statusOf(sio) & STAT_DSR) != 0;
		if (dsrHigh && !dsrWasHigh) observed.ackPulses++;
		dsrWasHigh = dsrHigh;
	}
	return observed;
}

void runCycles(SIO0& sio, int cycles) {
	for (int cycle = 0; cycle < cycles; cycle++) sio.advanceClock();
}

void program(SIO0& sio, uint16_t control, uint16_t mode = MODE_BIOS, uint16_t baud = BAUD_BIOS) {
	sio.write16(JOY_BAUD, baud);
	sio.write16(JOY_MODE, mode);
	sio.write16(JOY_CTRL, control);
}

void checkPowerOn() {
	SIO0 sio;
	uint16_t status = statusOf(sio);
	// bits 0 and 2 (transmit ready) set; 1 (byte received), 7 (/ACK) and 9 (interrupt) clear
	check((status & (STAT_RX_READY | STAT_DSR | STAT_IRQ | 1 | STAT_TX_IDLE)) == 0x0005, "power-on: JOY_STAT shows transmit ready and idle, and no byte, /ACK or interrupt", "JOY_STAT is " + hex(status, 4));
}

void checkRegisterReadback() {
	{
		SIO0 sio;
		sio.write16(JOY_MODE, 0x000D);
		checkEqual(sio.read16(JOY_MODE), 0x000D, "JOY_MODE holds what is written");
	}
	{
		SIO0 sio;
		sio.write16(JOY_BAUD, 0x0088);
		checkEqual(sio.read16(JOY_BAUD), 0x0088, "JOY_BAUD holds what is written (0088h, the BIOS value)");
		sio.write16(JOY_BAUD, 0x1234);
		checkEqual(sio.read16(JOY_BAUD), 0x1234, "JOY_BAUD holds what is written (1234h)");
	}
	{
		SIO0 sio;
		sio.write16(JOY_CTRL, 0x3F2F);   // every writable bit of JOY_CTRL except the write-only 4 and 6
		checkEqual(sio.read16(JOY_CTRL), 0x3F2F, "JOY_CTRL holds what is written (bits 0-3, 5, 8-13)");
	}
	{
		SIO0 sio;
		sio.write16(JOY_CTRL, 0x1003 | CTRL_ACKNOWLEDGE);
		checkEqual(sio.read16(JOY_CTRL), 0x1003, "JOY_CTRL bit 4 (acknowledge) is write-only: it reads back as 0");
	}
	{
		SIO0 sio;
		sio.write16(JOY_MODE, 0x000D);
		sio.write16(JOY_CTRL, 0x1003);
		checkEqual(sio.read32(JOY_MODE), 0x1003000D, "a 32-bit read at JOY_MODE returns JOY_MODE in the low half and JOY_CTRL in the high half");
	}
	{
		SIO0 sio;
		sio.write32(JOY_MODE, 0x1003000D);
		check(sio.read16(JOY_MODE) == 0x000D && sio.read16(JOY_CTRL) == 0x1003, "a 32-bit write at JOY_MODE sets JOY_MODE (low half) and JOY_CTRL (high half)",
			"JOY_MODE " + hex(sio.read16(JOY_MODE), 4) + ", JOY_CTRL " + hex(sio.read16(JOY_CTRL), 4));
	}
}

// While a byte is shifting out nothing has been received and the transmitter is busy; when it is done the reply is waiting; reading it takes it away
void checkStatusDuringTransfer() {
	SIO0 sio;
	program(sio, CTRL_SELECTED);
	sio.write8(JOY_DATA, 0x01);
	const int byteCycles = 8 * 0x88;
	check((statusOf(sio) & (STAT_RX_READY | STAT_TX_IDLE)) == 0, "transfer: right after the write, no byte is received and the transmitter is not idle", "JOY_STAT is " + hex(statusOf(sio), 4));
	runCycles(sio, byteCycles / 2);
	check((statusOf(sio) & (STAT_RX_READY | STAT_TX_IDLE)) == 0, "transfer: halfway through the byte, still no byte received and the transmitter busy", "JOY_STAT is " + hex(statusOf(sio), 4));
	runCycles(sio, byteCycles - byteCycles / 2);
	check((statusOf(sio) & (STAT_RX_READY | STAT_TX_IDLE)) == (STAT_RX_READY | STAT_TX_IDLE), "transfer: when the byte is done, the reply is waiting and the transmitter is idle", "JOY_STAT is " + hex(statusOf(sio), 4));
	sio.read8(JOY_DATA);
	check((statusOf(sio) & STAT_RX_READY) == 0, "transfer: reading JOY_DATA clears the received-byte flag", "JOY_STAT is " + hex(statusOf(sio), 4));
}

// A byte takes 8 * reload * multiplier cycles, the multiplier being 1, 16 or 64 for JOY_MODE bits 0-1 = 1, 2, 3
void checkByteTime() {
	struct Case {
		uint16_t mode;
		uint16_t reload;
		int cycles;
		const char* name;
	};
	const Case cases[] = {
		{0x000D, 0x0088, 8 * 0x88, "reload 88h, multiplier 1 (the BIOS setting)"},
		{0x000D, 0x0030, 8 * 0x30, "reload 30h, multiplier 1"},
		{0x000E, 0x0020, 8 * 0x20 * 16, "reload 20h, multiplier 16"},
		{0x000F, 0x0010, 8 * 0x10 * 64, "reload 10h, multiplier 64"},
	};
	for (const Case& testCase : cases) {
		SIO0 sio;
		program(sio, CTRL_SELECTED, testCase.mode, testCase.reload);
		sio.write8(JOY_DATA, 0x01);
		runCycles(sio, testCase.cycles - 1);
		bool early = (statusOf(sio) & STAT_RX_READY) != 0;
		runCycles(sio, 1);
		bool done = (statusOf(sio) & STAT_RX_READY) != 0;
		std::string name = std::string("byte time, ") + testCase.name + " (" + std::to_string(testCase.cycles) + " cycles)";
		check(!early && done, name + ": the byte is received after exactly 8 * reload * multiplier cycles, not before",
			std::string("received one cycle before: ") + (early ? "yes" : "no") + ", received at the full time: " + (done ? "yes" : "no"));
	}
}

// The pad's /ACK: seen in JOY_STAT bit 7, and, if the DSR interrupt is enabled, one interrupt and the flag in bit 9 that a write to JOY_CTRL bit 4 clears
void checkAcknowledgeAndInterrupt() {
	{
		SIO0 sio;
		program(sio, CTRL_SELECTED);
		sio.write8(JOY_DATA, 0x01);
		Observed observed = observe(sio, CYCLES_PER_BYTE);
		checkEqual(observed.ackPulses, 1, "ACK: the pad's /ACK shows once in JOY_STAT bit 7");
		checkEqual(observed.interrupts, 1, "ACK: with the DSR interrupt enabled, one interrupt is raised per /ACK");
		check((statusOf(sio) & STAT_IRQ) != 0, "ACK: with the DSR interrupt enabled, the interrupt flag (JOY_STAT bit 9) is set", "JOY_STAT is " + hex(statusOf(sio), 4));

		sio.write16(JOY_CTRL, CTRL_SELECTED | CTRL_ACKNOWLEDGE);
		check((statusOf(sio) & STAT_IRQ) == 0, "ACK: writing JOY_CTRL bit 4 clears the interrupt flag", "JOY_STAT is " + hex(statusOf(sio), 4));

		sio.read8(JOY_DATA);
		sio.write8(JOY_DATA, 0x42);
		observed = observe(sio, CYCLES_PER_BYTE);
		checkEqual(observed.interrupts, 1, "ACK: the next byte's /ACK raises an interrupt again");
	}
	{
		SIO0 sio;
		program(sio, CTRL_SELECTED_NO_IRQ);
		sio.write8(JOY_DATA, 0x01);
		Observed observed = observe(sio, CYCLES_PER_BYTE);
		checkEqual(observed.ackPulses, 1, "ACK without the DSR interrupt enabled: the /ACK still shows in JOY_STAT bit 7");
		checkEqual(observed.interrupts, 0, "ACK without the DSR interrupt enabled: no interrupt is raised");
		check((statusOf(sio) & STAT_IRQ) == 0, "ACK without the DSR interrupt enabled: the interrupt flag stays clear", "JOY_STAT is " + hex(statusOf(sio), 4));
	}
	{
		SIO0 sio;
		program(sio, CTRL_PORT2_SELECTED);
		sio.write8(JOY_DATA, 0x01);
		Observed observed = observe(sio, CYCLES_PER_BYTE);
		check(observed.ackPulses == 0 && observed.interrupts == 0, "ACK: nothing answers on port 2, so no /ACK and no interrupt",
			std::to_string(observed.ackPulses) + " /ACK pulses, " + std::to_string(observed.interrupts) + " interrupts");
	}
}

// Writing JOY_CTRL bit 6 resets the port: the control register and the received byte go, and a pending /ACK no longer raises an interrupt
void checkReset() {
	SIO0 sio;
	program(sio, CTRL_SELECTED);
	sio.write8(JOY_DATA, 0x01);
	runCycles(sio, 8 * 0x88 + 10);   // the byte is received, its /ACK is still to come
	check((statusOf(sio) & STAT_RX_READY) != 0, "reset: a received byte was waiting before the reset (the checks below rely on it)", "JOY_STAT is " + hex(statusOf(sio), 4));
	sio.write16(JOY_CTRL, CTRL_RESET);
	checkEqual(sio.read16(JOY_CTRL), 0, "reset: JOY_CTRL reads 0");
	check((statusOf(sio) & (STAT_RX_READY | STAT_IRQ)) == 0, "reset: no received byte and no interrupt flag", "JOY_STAT is " + hex(statusOf(sio), 4));
	Observed observed = observe(sio, CYCLES_PER_BYTE);
	check(observed.interrupts == 0 && (statusOf(sio) & STAT_IRQ) == 0, "reset: the /ACK that was still to come raises no interrupt", std::to_string(observed.interrupts) + " interrupts");
}

// Reading part of a register through a byte or halfword access. The registers get non-zero values first, so a read that returns 0 is a wrong read.
void checkNarrowReads() {
	SIO0 sio;
	program(sio, CTRL_SELECTED);
	sio.write8(JOY_DATA, 0x01);
	runCycles(sio, CYCLES_PER_BYTE);
	sio.read8(JOY_DATA);
	sio.write8(JOY_DATA, 0x42);       // the pad answers 41h; JOY_STAT now has bits 0, 1, 2 and 9 set
	runCycles(sio, CYCLES_PER_BYTE);

	uint16_t status = statusOf(sio);
	check(status == 0x0207 && sio.read16(JOY_MODE) == 0x000D && sio.read16(JOY_CTRL) == 0x1003 && sio.read16(JOY_BAUD) == 0x0088,
		"narrow reads: the registers hold the values the checks below read", "JOY_STAT " + hex(status, 4) + ", JOY_MODE " + hex(sio.read16(JOY_MODE), 4)
		+ ", JOY_CTRL " + hex(sio.read16(JOY_CTRL), 4) + ", JOY_BAUD " + hex(sio.read16(JOY_BAUD), 4));

	struct ByteCase {
		uint32_t address;
		uint8_t expected;
		const char* name;
	};
	const ByteCase byteCases[] = {
		{JOY_STAT, 0x07, "JOY_STAT byte 0"},
		{JOY_STAT + 1, 0x02, "JOY_STAT byte 1"},
		{JOY_MODE, 0x0D, "JOY_MODE byte 0"},
		{JOY_CTRL, 0x03, "JOY_CTRL byte 0"},
		{JOY_BAUD, 0x88, "JOY_BAUD byte 0"},
	};
	for (const ByteCase& testCase : byteCases) {
		uint8_t value = sio.read8(testCase.address);
		check(value == testCase.expected, std::string("narrow reads: ") + testCase.name + " is that part of the 16-bit register", "read " + hex(value, 2) + ", expected " + hex(testCase.expected, 2));
	}

	// Last, because reading JOY_DATA takes the byte away
	uint16_t data = sio.read16(JOY_DATA);
	check((data & 0xFF) == 0x41, "narrow reads: a halfword read of JOY_DATA returns the received byte in its low half", "read " + hex(data, 4) + ", expected the low byte to be 41h");
}

// IRQ7 reaching I_STAT bit 7, through the Bus, once per /ACK and only with the DSR interrupt enabled
void checkInterruptReachesStatus() {
	{
		Bus bus;
		bus.write16(JOY_BAUD, BAUD_BIOS);
		bus.write16(JOY_MODE, MODE_BIOS);
		bus.write16(JOY_CTRL, CTRL_SELECTED);
		bus.write8(JOY_DATA, 0x01);
		bus.advanceClock(CYCLES_PER_BYTE);
		check((bus.read32(INTERRUPT_STATUS) & IRQ7_PAD) != 0, "IRQ7: the pad's /ACK sets I_STAT bit 7");
		bus.write32(INTERRUPT_STATUS, ~IRQ7_PAD);
		bus.read8(JOY_DATA);
		bus.advanceClock(CYCLES_PER_BYTE);
		check((bus.read32(INTERRUPT_STATUS) & IRQ7_PAD) == 0, "IRQ7: once acknowledged in I_STAT, it does not come back without another /ACK");
		bus.write8(JOY_DATA, 0x42);
		bus.advanceClock(CYCLES_PER_BYTE);
		check((bus.read32(INTERRUPT_STATUS) & IRQ7_PAD) != 0, "IRQ7: the next byte's /ACK sets I_STAT bit 7 again");
	}
	{
		Bus bus;
		bus.write16(JOY_BAUD, BAUD_BIOS);
		bus.write16(JOY_MODE, MODE_BIOS);
		bus.write16(JOY_CTRL, CTRL_SELECTED_NO_IRQ);
		bus.write8(JOY_DATA, 0x01);
		bus.advanceClock(CYCLES_PER_BYTE);
		check((bus.read32(INTERRUPT_STATUS) & IRQ7_PAD) == 0, "IRQ7: with the DSR interrupt not enabled in JOY_CTRL, I_STAT bit 7 stays clear");
	}
}

} // namespace

int main() {
	checkPowerOn();
	checkRegisterReadback();
	checkStatusDuringTransfer();
	checkByteTime();
	checkAcknowledgeAndInterrupt();
	checkReset();
	checkNarrowReads();
	checkInterruptReachesStatus();
	return echo_check::finish();
}
