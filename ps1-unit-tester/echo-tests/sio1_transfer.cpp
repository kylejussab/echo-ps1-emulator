// SIO1 sending and receiving: how long a byte takes at each baud and frame setting, the TX status bits, what happens when DR is written
// again while busy, TXEN, CTS and the STOP setting, the 8-byte RX FIFO and its overrun, the RX / DSR interrupts and their acknowledge, the reset
// bit, and IRQ8 reaching I_STAT through the Bus. The registers themselves are in sio1_registers. Needs no BIOS and no devices: the host side of the
// port is the receiveByte / setDSR / setCTS calls and the transmit callback.
//
// What is checked is what the stable psx-spx page "Serial Interfaces (SIO)" documents. In particular:
//   - SIO1 bit rate = 33868800 / MAX((reload * factor) AND NOT 1, factor), so a bit lasts that many CPU cycles. A frame is the start bit, 5-8 data
//     bits, an optional parity bit and 1, 1.5 or 2 stop bits. STAT bit 2 (TX idle) is set after the stop bit. A baud factor of 0 means STOP.
//   - Writing DR starts a transfer if, or as soon as, TXEN is set and CTS is on. Writing DR makes STAT bits 0 and 2 zero; bit 0 comes back
//     once the start bit is sent. Writing DR while bit 0 is zero overwrites the old value. The hardware holds almost two bytes in the TX direction.
//   - The RX FIFO holds 8 bytes; one more overwrites the LAST entry and sets STAT bit 4 (sticky). Clearing CTRL.RXEN clears the FIFO.
//   - CTRL bits 8-9 pick the RX interrupt level (1, 2, 4 or 8 bytes), bit 11 enables it, bit 12 enables the DSR interrupt. STAT bit 9 is sticky and
//     CTRL bit 4 clears it and the error flags. Acknowledging while the condition still holds raises the interrupt again almost at once.
// Left out on purpose because the page says "probably" / gives no value, or nothing here can produce the condition: the TX interrupt (it says only
// "when STAT.0 or 2 is ready"), parity and framing errors, the latched-TXEN quirk, reading an empty FIFO, and what a reset does to BAUD.
#include "core/Bus.h"
#include "io/SIO1.h"
#include "support/echo_check.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;

namespace {

constexpr uint32_t SIO1_DR = 0x1F801050;
constexpr uint32_t SIO1_SR = 0x1F801054;
constexpr uint32_t SIO1_MR = 0x1F801058;
constexpr uint32_t SIO1_CR = 0x1F80105A;
constexpr uint32_t SIO1_BR = 0x1F80105E;
constexpr uint32_t INTERRUPT_STATUS = 0x1F801070;
constexpr uint32_t IRQ8_SIO1 = 0x00000100;

constexpr uint32_t SR_TXRDY = 1u << 0;
constexpr uint32_t SR_RXRDY = 1u << 1;
constexpr uint32_t SR_TX_IDLE = 1u << 2;
constexpr uint32_t SR_OVERRUN = 1u << 4;
constexpr uint32_t SR_DSR = 1u << 7;
constexpr uint32_t SR_CTS = 1u << 8;
constexpr uint32_t SR_IRQ = 1u << 9;

constexpr uint16_t CR_TXEN = 1u << 0;
constexpr uint16_t CR_RXEN = 1u << 2;
constexpr uint16_t CR_ACKNOWLEDGE = 1u << 4;
constexpr uint16_t CR_RESET = 1u << 6;
constexpr uint16_t CR_RX_IRQ_ENABLE = 1u << 11;
constexpr uint16_t CR_DSR_IRQ_ENABLE = 1u << 12;
constexpr uint16_t cr_rx_irq_level(int mode) { return static_cast<uint16_t>(mode << 8); }

constexpr uint16_t MODE_8N1_MUL16 = 0x004E;
constexpr uint16_t BAUD_FOR_TESTS = 0x0020;           // with MUL16 a bit lasts 0x20 * 16 = 512 cycles, a 8N1 frame 5120

// A port with a host attached: the transmit callback records what was sent and when, and time only passes when the test says so
class Rig {
public:
	explicit Rig(uint16_t mode = MODE_8N1_MUL16, uint16_t baud = BAUD_FOR_TESTS, bool cts = true) {
		sio.setTransmitCallback([this](uint8_t byte) { sent.push_back({byte, elapsed}); });
		sio.write16(SIO1_MR, mode);
		sio.write16(SIO1_BR, baud);
		sio.setCTS(cts);
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	void run(uint64_t cycles) {
		for (uint64_t cycle = 0; cycle < cycles; cycle++) {
			sio.advanceClock(1);
			elapsed++;
		}
	}
	uint32_t status() { return sio.read32(SIO1_SR); }
	std::vector<uint8_t> sentBytes() const {
		std::vector<uint8_t> bytes;
		for (const auto& entry : sent) bytes.push_back(entry.first);
		return bytes;
	}

	SIO1 sio;
	std::vector<std::pair<uint8_t, uint64_t>> sent;
	uint64_t elapsed = 0;
};

std::string show(const std::vector<uint8_t>& bytes) {
	std::string text;
	for (uint8_t byte : bytes) {
		if (!text.empty()) text += " ";
		text += hex(byte, 2);
	}
	return text.empty() ? "(none)" : text;
}

// A frame lasts exactly `expectedCycles`: TX idle is clear one cycle before and set at the end, and the byte has been handed over by then
void checkFrameTime(const std::string& name, uint16_t mode, uint16_t baud, uint64_t expectedCycles) {
	Rig rig(mode, baud);
	rig.sio.write16(SIO1_CR, CR_TXEN);
	rig.sio.write8(SIO1_DR, 0xA5);
	rig.run(expectedCycles - 1);
	bool idleBefore = (rig.status() & SR_TX_IDLE) != 0;
	size_t sentBefore = rig.sent.size();
	rig.run(1);
	bool idleAtEnd = (rig.status() & SR_TX_IDLE) != 0;
	bool delivered = rig.sentBytes() == std::vector<uint8_t>({0xA5});
	check(!idleBefore && sentBefore == 0 && idleAtEnd && delivered, "frame time, " + name + " (" + std::to_string(expectedCycles) + " cycles): TX idle is set exactly when the frame ends",
		std::string("idle one cycle before: ") + (idleBefore ? "yes" : "no") + ", bytes handed over before: " + std::to_string(sentBefore) + ", idle at the end: "
		+ (idleAtEnd ? "yes" : "no") + ", sent: " + show(rig.sentBytes()));
}

void checkFrameTimes() {
	// mode bits: 0-1 factor, 2-3 length (0=5 .. 3=8), 4 parity enable, 5 parity type, 6-7 stop bits (1=1, 2=1.5, 3=2)
	checkFrameTime("8N1, factor 16, reload 20h: 10 bits of 512", 0x004E, 0x0020, 10 * 512);
	checkFrameTime("8N1, factor 1, reload 88h: 10 bits of 136", 0x004D, 0x0088, 10 * 136);
	checkFrameTime("8N1, factor 64, reload 10h: 10 bits of 1024", 0x004F, 0x0010, 10 * 1024);
	checkFrameTime("8E1, factor 1, reload 40h: 11 bits of 64 (start, 8 data, parity, stop)", 0x007D, 0x0040, 11 * 64);
	checkFrameTime("5N2, factor 1, reload 40h: 8 bits of 64 (start, 5 data, 2 stop)", 0x00C1, 0x0040, 8 * 64);
	checkFrameTime("8N1.5, factor 1, reload 40h: 10.5 bits of 64", 0x008D, 0x0040, 672);
	checkFrameTime("8N1, factor 16, reload 0: the divisor is at least the factor, so a bit lasts 16 cycles", 0x004E, 0x0000, 10 * 16);
}

// Right after DR is written, TX ready and TX idle are both zero; TX ready comes back after the start bit, TX idle after the stop bit
void checkTransmitStatus() {
	Rig rig;
	rig.sio.write16(SIO1_CR, CR_TXEN);
	rig.sio.write8(SIO1_DR, 0x41);
	check((rig.status() & (SR_TXRDY | SR_TX_IDLE)) == 0, "TX status: writing DR makes STAT bits 0 (TX ready) and 2 (TX idle) both zero", "STAT is " + hex(rig.status(), 8));
	rig.run(2 * 512);
	check((rig.status() & (SR_TXRDY | SR_TX_IDLE)) == SR_TXRDY, "TX status: after the start bit, TX ready is back and TX idle is still zero", "STAT is " + hex(rig.status(), 8));
	rig.run(10 * 512 - 2 * 512);
	check((rig.status() & SR_TX_IDLE) != 0, "TX status: after the stop bit TX idle is set", "STAT is " + hex(rig.status(), 8));
}

// One byte shifting and one waiting in DR: both are sent in order, and TX idle comes only after the second
void checkTwoBytes() {
	Rig rig;
	rig.sio.write16(SIO1_CR, CR_TXEN);
	rig.sio.write8(SIO1_DR, 0x11);
	rig.run(2 * 512);
	rig.sio.write8(SIO1_DR, 0x22);
	check((rig.status() & SR_TXRDY) == 0, "two bytes: writing DR again while a byte is shifting makes TX ready zero", "STAT is " + hex(rig.status(), 8));
	rig.run(10 * 512 - 2 * 512);
	check((rig.status() & SR_TX_IDLE) == 0 && rig.sentBytes() == std::vector<uint8_t>({0x11}), "two bytes: when the first frame ends the second has started: TX idle is still zero",
		"STAT is " + hex(rig.status(), 8) + ", sent " + show(rig.sentBytes()));
	rig.run(10 * 512 + 512);
	check(rig.sentBytes() == std::vector<uint8_t>({0x11, 0x22}) && (rig.status() & SR_TX_IDLE) != 0, "two bytes: both are sent in order and TX idle is set after the second",
		"sent " + show(rig.sentBytes()) + ", STAT " + hex(rig.status(), 8));
}

// Writing DR while TX ready is zero overwrites the byte that was waiting there
void checkOverwrite() {
	Rig rig;
	rig.sio.write16(SIO1_CR, CR_TXEN);
	rig.sio.write8(SIO1_DR, 0x11);
	rig.run(2 * 512);
	rig.sio.write8(SIO1_DR, 0x22);   // waits in DR
	rig.sio.write8(SIO1_DR, 0x33);   // TX ready is zero: this overwrites 22h
	rig.run(3 * 10 * 512);
	check(rig.sentBytes() == std::vector<uint8_t>({0x11, 0x33}), "overwrite: a byte written to DR while TX ready is zero replaces the one waiting there (11h, 22h, 33h sends 11h, 33h)",
		"sent " + show(rig.sentBytes()));
}

// A byte written with TXEN off waits until TXEN is set
void checkTransmitEnable() {
	Rig rig;
	rig.sio.write8(SIO1_DR, 0x5A);
	rig.run(3 * 10 * 512);
	size_t sentWhileDisabled = rig.sent.size();
	rig.sio.write16(SIO1_CR, CR_TXEN);
	rig.run(10 * 512);
	check(sentWhileDisabled == 0 && rig.sentBytes() == std::vector<uint8_t>({0x5A}), "TXEN: a byte written with TXEN off is not sent, and is sent a frame after TXEN is set",
		"sent while disabled: " + std::to_string(sentWhileDisabled) + ", sent in the end: " + show(rig.sentBytes()));
}

// A byte is sent only while CTS is on
void checkClearToSend() {
	Rig rig(MODE_8N1_MUL16, BAUD_FOR_TESTS, false);
	rig.sio.write16(SIO1_CR, CR_TXEN);
	rig.sio.write8(SIO1_DR, 0x5A);
	rig.run(3 * 10 * 512);
	size_t sentWhileOff = rig.sent.size();
	rig.sio.setCTS(true);
	uint64_t ctsOn = rig.elapsed;
	rig.run(10 * 512);
	bool sentAfter = rig.sentBytes() == std::vector<uint8_t>({0x5A}) && rig.sent[0].second >= ctsOn;
	check(sentWhileOff == 0 && sentAfter, "CTS: a byte written while CTS is off is not sent until CTS goes on",
		"sent while CTS was off: " + std::to_string(sentWhileOff) + ", sent in the end: " + show(rig.sentBytes()));
}

// Baud factor 0 is STOP on SIO1: the port does not run
void checkStop() {
	Rig rig(0x004C, BAUD_FOR_TESTS);   // 8N1 with factor 0
	rig.sio.write16(SIO1_CR, CR_TXEN);
	rig.sio.write8(SIO1_DR, 0x5A);
	rig.sio.advanceClock(1000000);
	check(rig.sent.empty(), "STOP: with the baud factor 0 nothing is sent", "sent " + show(rig.sentBytes()));
}

void checkReceiveFifo() {
	{
		Rig rig;
		rig.sio.write16(SIO1_CR, CR_RXEN);
		for (uint8_t byte : {0x01, 0x02, 0x03}) rig.sio.receiveByte(byte);
		check((rig.status() & SR_RXRDY) != 0, "RX: STAT bit 1 is set once a byte has arrived");
		bool inOrder = rig.sio.read8(SIO1_DR) == 0x01 && rig.sio.read8(SIO1_DR) == 0x02;
		check(inOrder && (rig.status() & SR_RXRDY) != 0, "RX: bytes come out in the order they arrived, and bit 1 stays set while more are waiting");
		check(rig.sio.read8(SIO1_DR) == 0x03 && (rig.status() & SR_RXRDY) == 0, "RX: bit 1 clears when the last byte is read");
	}
	{
		Rig rig;
		rig.sio.write16(SIO1_CR, 0);
		rig.sio.receiveByte(0x01);
		check((rig.status() & SR_RXRDY) == 0, "RX: with RXEN off, a byte arriving is ignored");
	}
	{
		Rig rig;
		rig.sio.write16(SIO1_CR, CR_RXEN);
		for (uint8_t byte : {0x01, 0x02, 0x03}) rig.sio.receiveByte(byte);
		rig.sio.write16(SIO1_CR, 0);
		check((rig.status() & SR_RXRDY) == 0, "RX: clearing RXEN also clears the RX FIFO", "STAT is " + hex(rig.status(), 8));
	}
	{
		// Nine bytes into a FIFO of eight: the ninth overwrites the last entry and sets the sticky overrun flag
		Rig rig;
		rig.sio.write16(SIO1_CR, CR_RXEN);
		for (uint8_t byte = 1; byte <= 9; byte++) rig.sio.receiveByte(byte);
		check((rig.status() & SR_OVERRUN) != 0, "RX overrun: receiving a ninth byte with eight waiting sets STAT bit 4");
		std::vector<uint8_t> read;
		for (int index = 0; index < 8; index++) read.push_back(rig.sio.read8(SIO1_DR));
		check(read == std::vector<uint8_t>({1, 2, 3, 4, 5, 6, 7, 9}), "RX overrun: the ninth byte overwrites the last FIFO entry (1..9 reads back 1 2 3 4 5 6 7 9)", "read " + show(read));
		check((rig.status() & SR_OVERRUN) != 0, "RX overrun: the flag is sticky, reading the FIFO does not clear it");
		rig.sio.write16(SIO1_CR, CR_RXEN | CR_ACKNOWLEDGE);
		check((rig.status() & SR_OVERRUN) == 0, "RX overrun: writing CTRL bit 4 clears the flag");
	}
}

// The RX interrupt: raised when the FIFO holds the number of bytes CTRL bits 8-9 select, if bit 11 enables it
void checkReceiveInterrupt() {
	const int levels[] = {1, 2, 4, 8};
	for (int mode = 0; mode < 4; mode++) {
		Rig rig;
		rig.sio.write16(SIO1_CR, CR_RXEN | CR_RX_IRQ_ENABLE | cr_rx_irq_level(mode));
		for (int byte = 0; byte < levels[mode] - 1; byte++) rig.sio.receiveByte(0x10);
		bool quietBelow = !rig.sio.consumeInterruptRequest() && (rig.status() & SR_IRQ) == 0;
		rig.sio.receiveByte(0x10);
		bool raisedAt = rig.sio.consumeInterruptRequest() && (rig.status() & SR_IRQ) != 0;
		check(quietBelow && raisedAt, "RX interrupt level " + std::to_string(levels[mode]) + (levels[mode] == 1 ? " byte" : " bytes") + ": no interrupt below it, an interrupt when the FIFO reaches it",
			std::string("quiet below: ") + (quietBelow ? "yes" : "no") + ", raised at the level: " + (raisedAt ? "yes" : "no"));
	}
	{
		Rig rig;
		rig.sio.write16(SIO1_CR, CR_RXEN | cr_rx_irq_level(0));   // RX interrupt not enabled
		for (int byte = 0; byte < 8; byte++) rig.sio.receiveByte(0x10);
		check(!rig.sio.consumeInterruptRequest() && (rig.status() & SR_IRQ) == 0, "RX interrupt: with CTRL bit 11 clear, no interrupt is raised");
	}
	{
		// STAT bit 9 is sticky; while it is set a further byte raises nothing new
		Rig rig;
		rig.sio.write16(SIO1_CR, CR_RXEN | CR_RX_IRQ_ENABLE);
		rig.sio.receiveByte(0x10);
		rig.sio.consumeInterruptRequest();
		rig.sio.receiveByte(0x11);
		check(!rig.sio.consumeInterruptRequest(), "RX interrupt: a further byte while STAT bit 9 is still set raises no new interrupt");
		rig.sio.read8(SIO1_DR);
		rig.sio.read8(SIO1_DR);
		check((rig.status() & SR_IRQ) != 0, "RX interrupt: STAT bit 9 stays set after the FIFO is read empty (it is sticky)");
		rig.sio.write16(SIO1_CR, CR_RXEN | CR_RX_IRQ_ENABLE | CR_ACKNOWLEDGE);
		check((rig.status() & SR_IRQ) == 0, "RX interrupt: writing CTRL bit 4 clears STAT bit 9 once the condition is gone");
	}
	{
		// Acknowledging while the condition still holds: the interrupt comes again almost at once
		Rig rig;
		rig.sio.write16(SIO1_CR, CR_RXEN | CR_RX_IRQ_ENABLE);
		rig.sio.receiveByte(0x10);
		rig.sio.consumeInterruptRequest();
		rig.sio.write16(SIO1_CR, CR_RXEN | CR_RX_IRQ_ENABLE | CR_ACKNOWLEDGE);
		rig.run(100);
		check(rig.sio.consumeInterruptRequest(), "RX interrupt: acknowledging while the FIFO still holds a byte raises the interrupt again almost at once");
	}
}

// The DSR and CTS input levels, and the DSR interrupt
void checkModemSignals() {
	{
		Rig rig(MODE_8N1_MUL16, BAUD_FOR_TESTS, false);
		check((rig.status() & SR_CTS) == 0, "CTS: STAT bit 8 is clear while CTS is off");
		rig.sio.setCTS(true);
		check((rig.status() & SR_CTS) != 0, "CTS: STAT bit 8 follows the CTS input");
	}
	{
		Rig rig;
		rig.sio.write16(SIO1_CR, CR_DSR_IRQ_ENABLE);
		rig.sio.setDSR(true);
		check((rig.status() & SR_DSR) != 0, "DSR: STAT bit 7 follows the DSR input");
		check(rig.sio.consumeInterruptRequest() && (rig.status() & SR_IRQ) != 0, "DSR: with CTRL bit 12 set, DSR going on raises the interrupt and sets STAT bit 9");
		rig.sio.write16(SIO1_CR, CR_DSR_IRQ_ENABLE | CR_ACKNOWLEDGE);
		rig.sio.setDSR(false);
		check(!rig.sio.consumeInterruptRequest(), "DSR: DSR going off raises no interrupt");
	}
	{
		Rig rig;
		rig.sio.write16(SIO1_CR, 0);
		rig.sio.setDSR(true);
		check((rig.status() & SR_DSR) != 0 && !rig.sio.consumeInterruptRequest(), "DSR: with CTRL bit 12 clear, STAT bit 7 still shows DSR but no interrupt is raised");
	}
}

// Writing CTRL bit 6 resets most registers to zero
void checkReset() {
	Rig rig;
	rig.sio.write16(SIO1_CR, CR_RXEN | CR_RX_IRQ_ENABLE);
	rig.sio.receiveByte(0x10);
	rig.sio.write16(SIO1_CR, CR_RESET);
	checkEqual(rig.sio.read16(SIO1_CR), 0, "reset: CTRL reads 0");
	check((rig.status() & (SR_RXRDY | SR_IRQ)) == 0, "reset: no received byte and no interrupt flag", "STAT is " + hex(rig.status(), 8));
}

// IRQ8 reaching I_STAT bit 8 through the Bus, and the Bus running the SIO1 clock
void checkThroughTheBus() {
	{
		Bus bus;
		bus.write16(SIO1_MR, MODE_8N1_MUL16);
		bus.write16(SIO1_CR, CR_RXEN | CR_RX_IRQ_ENABLE);
		bus.getSIO1().receiveByte(0x55);
		bus.advanceClock(1);
		check((bus.read32(INTERRUPT_STATUS) & IRQ8_SIO1) != 0, "Bus: an RX interrupt sets I_STAT bit 8");
		bus.write32(INTERRUPT_STATUS, ~IRQ8_SIO1);
		bus.getSIO1().receiveByte(0x56);
		bus.advanceClock(1);
		check((bus.read32(INTERRUPT_STATUS) & IRQ8_SIO1) == 0, "Bus: with STAT bit 9 still set, a further byte does not set I_STAT bit 8 again");
	}
	{
		Bus bus;
		bus.write16(SIO1_MR, MODE_8N1_MUL16);
		bus.write16(SIO1_CR, CR_DSR_IRQ_ENABLE);
		bus.getSIO1().setDSR(true);
		bus.advanceClock(1);
		check((bus.read32(INTERRUPT_STATUS) & IRQ8_SIO1) != 0, "Bus: the DSR interrupt sets I_STAT bit 8");
	}
	{
		Bus bus;
		std::vector<uint8_t> sent;
		bus.getSIO1().setTransmitCallback([&sent](uint8_t byte) { sent.push_back(byte); });
		bus.getSIO1().setCTS(true);
		bus.write16(SIO1_MR, MODE_8N1_MUL16);
		bus.write16(SIO1_BR, BAUD_FOR_TESTS);
		bus.write16(SIO1_CR, CR_TXEN);
		bus.write8(SIO1_DR, 0x41);
		bus.advanceClock(10 * 512 - 1);
		bool early = !sent.empty();
		bus.advanceClock(1);
		check(!early && sent == std::vector<uint8_t>({0x41}), "Bus: a byte written to DR through the Bus is sent exactly one frame later, so the Bus runs the SIO1 clock", "sent " + show(sent) + (early ? " (too early)" : ""));
	}
}

} // namespace

int main() {
	checkFrameTimes();
	checkTransmitStatus();
	checkTwoBytes();
	checkOverwrite();
	checkTransmitEnable();
	checkClearToSend();
	checkStop();
	checkReceiveFifo();
	checkReceiveInterrupt();
	checkModemSignals();
	checkReset();
	checkThroughTheBus();
	return echo_check::finish();
}
