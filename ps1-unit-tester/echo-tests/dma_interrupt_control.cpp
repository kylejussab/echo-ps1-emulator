// The DMA interrupt register (DICR, 0x1F8010F4) and how a finished transfer reaches the CPU through I_STAT bit 3 (IRQ3).
// Checks the bit layout, which writes can and cannot change which bits, when a completion latches its flag, the master flag (bit 31),
// the 0 -> 1 edge that raises IRQ3, and reads of DICR and DPCR through narrower accesses than 32 bits.
// Needs no BIOS and no devices: a finished transfer is made with the ordering-table clear channel (channel 6, which only writes RAM),
// and a second channel (channel 2 with an empty linked list, which sends the GPU nothing) shows the flag follows the channel number.
//
// What is checked is what the hardware documents (psx-spx "DMA Channels", "DMA Interrupt Register") and what emulators agree on. Left out on
// purpose because the hardware behaviour is not confirmed (sources disagree, or none was found): what a byte or halfword WRITE to DICR does
// to the bits outside the access; whether a finished transfer latches its flag while the master enable (bit 23) is off; whether bit 31 is
// "master enable AND any (channel enable AND flag)" or "master enable AND any flag" (so, whether clearing a flagged channel's enable bit
// drops it); and the value of DPCR at power-on.
#include "core/Bus.h"
#include "support/echo_check.h"
#include "support/isolated_probe.h"
#include <cstdint>
#include <string>

using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;
using isolated_probe::Outcome;
using isolated_probe::run;

namespace {

// The hardware layout, written out here on purpose instead of using the emulator's own constants: the test should not share the numbers it checks.
constexpr uint32_t INTERRUPT_STATUS = 0x1F801070;   // I_STAT
constexpr uint32_t INTERRUPT_MASK = 0x1F801074;     // I_MASK
constexpr uint32_t IRQ3_DMA = 0x00000008;           // I_STAT / I_MASK bit 3

constexpr uint32_t DPCR = 0x1F8010F0;
constexpr uint32_t DICR = 0x1F8010F4;
constexpr uint32_t CHANNEL_REGISTERS = 0x1F801080;  // channel n: + n * 0x10 (address, block control, channel control)

// DICR bits
constexpr uint32_t FORCE_IRQ = 1u << 15;
constexpr uint32_t MASTER_ENABLE = 1u << 23;
constexpr uint32_t MASTER_FLAG = 1u << 31;
constexpr uint32_t WRITABLE_BITS = 0x00FF803F;      // bits 0-5, 15 and 16-23. 6-14 always read 0, 24-30 clear by writing 1, 31 is read only

constexpr uint32_t enableBit(int channel) { return 1u << (16 + channel); }
constexpr uint32_t flagBit(int channel) { return 1u << (24 + channel); }

uint32_t readDicr(Bus& bus) { return bus.read32(DICR); }
bool irq3Pending(Bus& bus) { return (bus.read32(INTERRUPT_STATUS) & IRQ3_DMA) != 0; }
void acknowledgeIrq3(Bus& bus) { bus.write32(INTERRUPT_STATUS, ~IRQ3_DMA); }   // I_STAT: a 0 bit acknowledges, a 1 bit leaves it alone

// Writes DICR keeping its current writable bits and writing 1 to the given flags, which clears them
void clearFlags(Bus& bus, uint32_t flags) { bus.write32(DICR, (readDicr(bus) & WRITABLE_BITS) | flags); }

// Runs a whole ordering-table clear on channel 6 (4 entries ending at 0x1000, written to RAM only) and says whether it really ran.
// This is the way to get a finished transfer, and so a DICR flag, without any device. The channel is complete as soon as it is started.
bool runOrderingTableClear(Bus& bus) {
	const uint32_t madr = CHANNEL_REGISTERS + 6 * 0x10;
	bus.write32(0xFF4, 0);                                  // the last entry, which a finished clear sets to the end marker
	bus.write32(DPCR, bus.read32(DPCR) | (1u << 27));       // channel 6 master enable
	bus.write32(madr, 0x00001000);
	bus.write32(madr + 4, 4);
	bus.write32(madr + 8, 0x11000000);                      // start + manual trigger
	return bus.read32(0xFF4) == 0x00FFFFFF;
}

// DICR bit layout: which bits hold what is written and which do not
void checkBitLayout() {
	{
		Bus bus;
		checkEqual(readDicr(bus), 0, "DICR reads 0 at power-on");
	}

	struct Case {
		uint32_t written;
		uint32_t expected;
		const char* name;
	};
	const Case cases[] = {
		{0x0000003F, 0x0000003F, "bits 0-5 hold what is written"},
		{0x00007FC0, 0x00000000, "bits 6-14 always read 0"},
		{0x00008000, 0x80008000, "bit 15 (force IRQ) holds what is written, and sets bit 31"},
		{0x007F0000, 0x007F0000, "bits 16-22 (the channel enables) hold what is written"},
		{0x00800000, 0x00800000, "bit 23 (master enable) holds what is written; with no flag it does not set bit 31"},
		{0x7F000000, 0x00000000, "bits 24-30 (the flags) cannot be set by writing"},
		{0x80000000, 0x00000000, "bit 31 cannot be set by writing"},
		{0xFFFFFFFF, 0x80FF803F, "writing all ones: only the writable bits are set, and bit 31 follows the force bit"},
	};
	for (const Case& testCase : cases) {
		Bus bus;
		bus.write32(DICR, testCase.written);
		uint32_t value = readDicr(bus);
		check(value == testCase.expected, std::string("DICR: ") + testCase.name, "wrote " + hex(testCase.written) + ", read " + hex(value) + ", expected " + hex(testCase.expected));
	}

	{
		Bus bus;
		bus.write32(DICR, 0x00FF803F);
		bus.write32(DICR, 0);
		checkEqual(readDicr(bus), 0, "DICR: writing 0 clears every writable bit again (and bit 31 with them)");
	}
	{
		Bus bus;
		bus.write32(DICR, 0x80000000);
		check(!irq3Pending(bus), "DICR: writing bit 31 does not raise IRQ3");
	}
}

// A finished transfer latches the flag of its own channel, but only if that channel's enable bit is set
void checkFlagLatching() {
	{
		Bus bus;
		check(runOrderingTableClear(bus), "flags: the ordering-table clear used to finish transfers runs (the checks below rely on it)");
	}
	{
		Bus bus;
		bus.write32(DICR, MASTER_ENABLE);
		runOrderingTableClear(bus);
		checkEqual(readDicr(bus), MASTER_ENABLE, "flags: a finished transfer sets no flag when the channel's enable bit is clear");
		check(!irq3Pending(bus), "flags: a finished transfer raises no IRQ3 when the channel's enable bit is clear");
	}
	{
		Bus bus;
		bus.write32(DICR, enableBit(6));
		runOrderingTableClear(bus);
		check((readDicr(bus) & MASTER_FLAG) == 0, "flags: with the channel enabled but the master enable off, bit 31 stays 0");
		check(!irq3Pending(bus), "flags: with the master enable off, no IRQ3 is raised");
	}
	{
		Bus bus;
		bus.write32(DICR, MASTER_ENABLE | enableBit(6));
		runOrderingTableClear(bus);
		checkEqual(readDicr(bus), MASTER_FLAG | flagBit(6) | MASTER_ENABLE | enableBit(6), "flags: with the channel and the master enabled, the flag and bit 31 are set");
		check(irq3Pending(bus), "flags: with the channel and the master enabled, IRQ3 is raised");
	}

	const uint32_t enabledAndMaster = MASTER_ENABLE | enableBit(6);
	const uint32_t flagged = MASTER_FLAG | flagBit(6) | enabledAndMaster;
	{
		Bus bus;
		bus.write32(DICR, enabledAndMaster);
		runOrderingTableClear(bus);
		clearFlags(bus, flagBit(6));
		checkEqual(readDicr(bus), enabledAndMaster, "flags: writing 1 to a flag clears it, drops bit 31 and keeps the enable bits");
	}
	{
		Bus bus;
		bus.write32(DICR, enabledAndMaster);
		runOrderingTableClear(bus);
		bus.write32(DICR, enabledAndMaster);
		checkEqual(readDicr(bus), flagged, "flags: writing 0 to a flag leaves it set");
	}
	{
		Bus bus;
		bus.write32(DICR, enabledAndMaster);
		runOrderingTableClear(bus);
		clearFlags(bus, flagBit(0));
		checkEqual(readDicr(bus), flagged, "flags: writing 1 to a different channel's flag leaves this one set");
	}
	{
		Bus bus;
		bus.write32(DICR, enabledAndMaster);
		runOrderingTableClear(bus);
		bus.write32(DICR, flagBit(6) | MASTER_ENABLE);   // clear the flag and drop the channel's enable bit in the same write
		checkEqual(readDicr(bus), MASTER_ENABLE, "flags: one write can clear a flag and change the enable bits");
	}
}

// The flag is bit 24 + the channel number: shown with a second channel, 2, whose transfer finishes after a short delay
void checkFlagFollowsChannel() {
	Bus bus;
	const uint32_t channel2 = CHANNEL_REGISTERS + 2 * 0x10;
	bus.write32(0x1000, 0x00FFFFFF);                        // a linked list of one node: no words, and the end marker
	bus.write32(DPCR, bus.read32(DPCR) | (1u << 11));       // channel 2 master enable
	bus.write32(DICR, MASTER_ENABLE | enableBit(2));
	bus.write32(channel2, 0x00001000);
	bus.write32(channel2 + 4, 0);
	bus.write32(channel2 + 8, 0x01000401);                  // start, linked list, from RAM
	bus.advanceClock(1000);
	checkEqual(readDicr(bus), MASTER_FLAG | flagBit(2) | MASTER_ENABLE | enableBit(2), "channel 2: its transfer finishing sets flag bit 26, not another channel's flag");
	check(irq3Pending(bus), "channel 2: its transfer finishing raises IRQ3");
}

// Bit 31 is the force bit, or the master enable with a flag set. Clearing the master enable drops it (and leaves the flag alone), and the force
// bit keeps it up whatever the flags do.
void checkMasterFlag() {
	{
		Bus bus;
		bus.write32(DICR, MASTER_ENABLE | enableBit(6));
		runOrderingTableClear(bus);
		bus.write32(DICR, enableBit(6));                    // the master enable off
		uint32_t value = readDicr(bus);
		check((value & flagBit(6)) != 0, "master flag: clearing the master enable does not clear the flag", "DICR is " + hex(value));
		check((value & MASTER_FLAG) == 0, "master flag: bit 31 drops when the master enable is cleared", "DICR is " + hex(value));
	}
	{
		Bus bus;
		bus.write32(DICR, FORCE_IRQ | MASTER_ENABLE | enableBit(6));
		runOrderingTableClear(bus);
		clearFlags(bus, flagBit(6));
		check((readDicr(bus) & MASTER_FLAG) != 0, "master flag: clearing the flags does not drop bit 31 while force is on");
	}
}

// IRQ3 is raised when bit 31 goes from 0 to 1, not while it stays 1
void checkIrq3Edge() {
	{
		Bus bus;
		bus.write32(DICR, FORCE_IRQ);
		check(irq3Pending(bus), "IRQ3: setting the force bit raises IRQ3");
		acknowledgeIrq3(bus);
		bus.write32(DICR, FORCE_IRQ);
		check(!irq3Pending(bus), "IRQ3: writing the force bit again while bit 31 is still 1 does not raise IRQ3 again");
		bus.write32(DICR, 0);
		bus.write32(DICR, FORCE_IRQ);
		check(irq3Pending(bus), "IRQ3: after bit 31 went back to 0, setting the force bit raises IRQ3 again");
	}
	{
		Bus bus;
		bus.write32(DICR, MASTER_ENABLE | enableBit(6));
		runOrderingTableClear(bus);
		acknowledgeIrq3(bus);
		runOrderingTableClear(bus);
		check(!irq3Pending(bus), "IRQ3: a second transfer finishing while bit 31 is still 1 does not raise IRQ3 again");
		clearFlags(bus, flagBit(6));
		runOrderingTableClear(bus);
		check(irq3Pending(bus), "IRQ3: once the flag was cleared, the next finished transfer raises IRQ3 again");
	}
	{
		Bus bus;
		bus.write32(DICR, MASTER_ENABLE | enableBit(6));
		runOrderingTableClear(bus);
		bus.write32(INTERRUPT_MASK, 0);
		check(!bus.hasPendingInterrupts(), "IRQ3: a DMA interrupt is not pending for the CPU while I_MASK bit 3 is clear");
		bus.write32(INTERRUPT_MASK, IRQ3_DMA);
		check(bus.hasPendingInterrupts(), "IRQ3: a DMA interrupt is pending for the CPU once I_MASK bit 3 is set");
	}
}

// Reading a 32-bit register through a byte or halfword access returns that part of it. Each probe runs in its own process because the Bus
// prints FATAL and exits on an access it does not handle. Reads only: what narrow WRITES do to the rest of DICR is not confirmed hardware behaviour.
uint32_t narrowReadMatches(uint32_t registerAddress, uint32_t offset, int bytes) {
	Bus bus;
	if (registerAddress == DICR) {
		bus.write32(DICR, FORCE_IRQ | 0x00C50025);          // bytes 0x25, 0x80, 0xC5 and, with the flag below, 0xC0
		runOrderingTableClear(bus);
	} else {
		bus.write32(DPCR, 0x0A3C5E71);
	}
	uint32_t whole = bus.read32(registerAddress);
	uint32_t shift = offset * 8;
	if (bytes == 1) return bus.read8(registerAddress + offset) == ((whole >> shift) & 0xFF) ? 1 : 0;
	return bus.read16(registerAddress + offset) == ((whole >> shift) & 0xFFFF) ? 1 : 0;
}

void checkNarrowReads() {
	struct Case {
		uint32_t registerAddress;
		uint32_t offset;
		int bytes;
		const char* name;
	};
	const Case cases[] = {
		{DICR, 0, 1, "DICR byte 0"},      {DICR, 1, 1, "DICR byte 1"},      {DICR, 2, 1, "DICR byte 2"},     {DICR, 3, 1, "DICR byte 3"},
		{DICR, 0, 2, "DICR low halfword"}, {DICR, 2, 2, "DICR high halfword"},
		{DPCR, 0, 1, "DPCR byte 0"},      {DPCR, 3, 1, "DPCR byte 3"},
		{DPCR, 0, 2, "DPCR low halfword"}, {DPCR, 2, 2, "DPCR high halfword"},
	};
	for (const Case& testCase : cases) {
		uint32_t registerAddress = testCase.registerAddress;
		uint32_t offset = testCase.offset;
		int bytes = testCase.bytes;
		Outcome outcome = run([registerAddress, offset, bytes] { return narrowReadMatches(registerAddress, offset, bytes); });
		std::string name = std::string("narrow reads: ") + testCase.name + " is that part of the 32-bit register";
		if (!outcome.finished) {
			check(false, name, isolated_probe::describeFailure(outcome));
		} else {
			check(outcome.value == 1, name, "the value read differs from that part of the 32-bit read");
		}
	}
}

} // namespace

int main() {
	// The probes fork, and on macOS a forked child that uses CoreFoundation (which a Bus does) is killed if this process has already built a Bus.
	// So the probes run first, while this process has not constructed one; every group after them builds its own Bus here.
	checkNarrowReads();

	checkBitLayout();
	checkFlagLatching();
	checkFlagFollowsChannel();
	checkMasterFlag();
	checkIrq3Edge();
	return echo_check::finish();
}
