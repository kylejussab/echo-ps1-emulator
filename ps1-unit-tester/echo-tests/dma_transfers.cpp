// What the DMA controller does with a transfer: the DPCR reset value, which bits of the channel registers hold what is written, the list the
// ordering-table clear (channel 6) builds in RAM and what it leaves behind in MADR, BCR and CHCR, and a RAM -> SPU -> RAM round trip through
// channel 4 (the real SPU) with its block accounting. The DMA interrupt register is in dma_interrupt_control. Needs no BIOS and no GPU: channel 6
// only writes RAM, and channel 4 talks to the SPU through its transfer registers (the SPU is driven through the Bus, as a game does).
//
// What is checked is what the stable psx-spx page "DMA Channels" documents:
//   - DPCR resets to 07654321h and all 32 bits are read/write. MADR bits 0-23 hold the address (bits 0-1 writable), 24-31 read 0. BCR is read/write.
//   - OTC (channel 6) builds a list of BC entries (0 = 10000h) going DOWN from MADR, each entry pointing at the previous one and the last holding
//     FFFFFFh. D6_CHCR has only bits 24, 28 and 30 read/write; bit 1 is always 1 and the other bits always 0 (11000002h to start it, "always").
//   - SyncMode 0 does not update MADR or BCR (they keep the start address and the count). The MADR counter wraps from 000000h down to FFFFFCh,
//     which is past the 8 MB of RAM, so words after the wraparound are not written to RAM.
//   - SyncMode 1 counts BA down to zero and leaves MADR at the end address. Bit 24 of CHCR clears on COMPLETION, bit 28 when the transfer BEGINS.
//   - A channel's master enable in DPCR (bit 4n+3) decides whether it runs.
// Left out on purpose because the hardware behaviour is not confirmed, or is not defined by the page: how long a transfer takes (the page's own
// table asks "is SPU really only 4 clks?"), the unused bits of CHCR on channels other than 6, what a disabled channel's CHCR reads, slice pacing.
#include "core/Bus.h"
#include "support/echo_check.h"
#include <cstdint>
#include <string>
#include <vector>

using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;

namespace {

constexpr uint32_t DPCR = 0x1F8010F0;
constexpr uint32_t DICR = 0x1F8010F4;
constexpr uint32_t channelMadr(int channel) { return 0x1F801080 + channel * 0x10; }
constexpr uint32_t channelBcr(int channel) { return 0x1F801084 + channel * 0x10; }
constexpr uint32_t channelChcr(int channel) { return 0x1F801088 + channel * 0x10; }
constexpr uint32_t channelEnableInDpcr(int channel) { return 1u << (channel * 4 + 3); }

constexpr uint32_t SPU_TRANSFER_ADDRESS = 0x1F801DA6;   // in units of 8 bytes
constexpr uint32_t SPUCNT = 0x1F801DAA;
constexpr uint16_t SPUCNT_ENABLED = 0xC000;             // SPU enable and unmute, as the BIOS sets them
constexpr uint16_t SPUCNT_DMA_WRITE = 0x0020;           // transfer mode 2, bits 4-5
constexpr uint16_t SPUCNT_DMA_READ = 0x0030;            // transfer mode 3

constexpr uint32_t CHCR_START = 0x01000000;
constexpr uint32_t CHCR_TRIGGER = 0x10000000;
constexpr uint32_t CHCR_OTC_START = 0x11000000;         // start + trigger, the documented 11000002h with bit 1 as the hardware forces it
constexpr uint32_t CHCR_SPU_WRITE = 0x01000201;         // RAM -> SPU, SyncMode 1, start
constexpr uint32_t CHCR_SPU_READ = 0x01000200;          // SPU -> RAM, SyncMode 1, start

void runCycles(Bus& bus, int cycles) {
	for (int done = 0; done < cycles; done += 100) bus.advanceClock(100);
}

void enableChannel(Bus& bus, int channel) {
	bus.write32(DPCR, bus.read32(DPCR) | channelEnableInDpcr(channel));
}

// Starts an ordering-table clear: BC entries going down from `madr`
void startOrderingTableClear(Bus& bus, uint32_t madr, uint32_t count) {
	enableChannel(bus, 6);
	bus.write32(channelMadr(6), madr);
	bus.write32(channelBcr(6), count);
	bus.write32(channelChcr(6), CHCR_OTC_START);
}

void checkDpcr() {
	{
		Bus bus;
		checkEqual(bus.read32(DPCR), 0x07654321, "DPCR resets to 07654321h (channel n has priority n+1, every channel disabled)");
	}
	{
		Bus bus;
		bus.write32(DPCR, 0xFFFFFFFF);
		checkEqual(bus.read32(DPCR), 0xFFFFFFFF, "DPCR holds what is written, all 32 bits (bit 31 is read/write too)");
		bus.write32(DPCR, 0x0B3C5A71);
		checkEqual(bus.read32(DPCR), 0x0B3C5A71, "DPCR holds what is written (0B3C5A71h)");
	}
}

void checkChannelRegisters() {
	for (int channel : {2, 4, 6}) {
		Bus bus;
		bus.write32(channelMadr(channel), 0xFF001003);
		checkEqual(bus.read32(channelMadr(channel)), 0x00001003, "channel " + std::to_string(channel) + " MADR: bits 24-31 read 0, and bits 0-1 hold what is written");
	}
	{
		Bus bus;
		bus.write32(channelBcr(4), 0x00030010);
		checkEqual(bus.read32(channelBcr(4)), 0x00030010, "channel 4 BCR holds what is written (block size 10h, 3 blocks)");
	}
	{
		// D6_CHCR has three read/write bits (24, 28, 30); bit 1 always reads 1 and the others 0. No bit 24 here, so nothing starts.
		Bus bus;
		bus.write32(channelChcr(6), 0x0EFFFFFF);
		checkEqual(bus.read32(channelChcr(6)), 0x00000002, "channel 6 CHCR: bits other than 24, 28 and 30 are read-only (bit 1 always 1, the rest 0)");
		bus.write32(channelChcr(6), 0x40000000);
		checkEqual(bus.read32(channelChcr(6)), 0x40000002, "channel 6 CHCR: bit 30 is read/write");
	}
}

// The list that the ordering-table clear leaves in RAM
void checkOrderingTableList() {
	{
		Bus bus;
		for (uint32_t address = 0xFF0; address <= 0x1004; address += 4) bus.write32(address, 0xA5A5A5A5);
		startOrderingTableClear(bus, 0x1000, 4);
		std::vector<uint32_t> got = {bus.read32(0x1000), bus.read32(0xFFC), bus.read32(0xFF8), bus.read32(0xFF4)};
		check(got == std::vector<uint32_t>({0x00000FFC, 0x00000FF8, 0x00000FF4, 0x00FFFFFF}), "OTC: four entries going down from MADR, each pointing at the previous one, the last holding FFFFFFh",
			"words at 1000h, FFCh, FF8h, FF4h: " + hex(got[0]) + " " + hex(got[1]) + " " + hex(got[2]) + " " + hex(got[3]));
		check(bus.read32(0x1004) == 0xA5A5A5A5 && bus.read32(0xFF0) == 0xA5A5A5A5, "OTC: nothing above MADR and nothing below the last entry is written");
	}
	{
		Bus bus;
		startOrderingTableClear(bus, 0x2000, 1);
		checkEqual(bus.read32(0x2000), 0x00FFFFFF, "OTC: a list of one entry holds just the end marker, at MADR");
	}
	{
		// BC = 0 means 10000h words
		Bus bus;
		bus.write32(0xC0000, 0xA5A5A5A5);
		startOrderingTableClear(bus, 0x100000, 0);
		bool shape = bus.read32(0x100000) == 0x000FFFFC && bus.read32(0xC0008) == 0x000C0004 && bus.read32(0xC0004) == 0x00FFFFFF;
		check(shape, "OTC: a block count of 0 builds 10000h entries (first, second-to-last and last entry are right)",
			"at 100000h " + hex(bus.read32(0x100000)) + ", at C0008h " + hex(bus.read32(0xC0008)) + ", at C0004h " + hex(bus.read32(0xC0004)));
		checkEqual(bus.read32(0xC0000), 0xA5A5A5A5, "OTC: a block count of 0 does not write more than 10000h entries");
	}
}

// What the ordering-table clear leaves in the registers: SyncMode 0 changes neither MADR nor BCR; bit 24 and bit 28 of CHCR are cleared
void checkOrderingTableRegisters() {
	Bus bus;
	startOrderingTableClear(bus, 0x1000, 4);
	checkEqual(bus.read32(channelMadr(6)), 0x1000, "OTC: MADR still holds the start address after the transfer (SyncMode 0 does not update it)");
	checkEqual(bus.read32(channelBcr(6)), 4, "OTC: BCR still holds the word count after the transfer");
	checkEqual(bus.read32(channelChcr(6)), 0x00000002, "OTC: CHCR reads 00000002h after the transfer (start and trigger bits cleared, bit 1 always 1)");
}

// MADR counts down from 000000h to FFFFFCh, past the 8 MB of RAM, so the words after the wraparound are not written
void checkOrderingTableWraparound() {
	Bus bus;
	bus.write32(0x1FFFFC, 0x5A5A5A5A);     // the RAM word a wrapped-around address would alias to
	startOrderingTableClear(bus, 0x0C, 5);
	std::vector<uint32_t> got = {bus.read32(0x0C), bus.read32(0x08), bus.read32(0x04), bus.read32(0x00)};
	check(got == std::vector<uint32_t>({0x08, 0x04, 0x00, 0x00FFFFFC}), "OTC wraparound: the entries before the wraparound are written, the one at 0 pointing at FFFFFCh",
		"words at 0Ch, 08h, 04h, 00h: " + hex(got[0]) + " " + hex(got[1]) + " " + hex(got[2]) + " " + hex(got[3]));
	checkEqual(bus.read32(0x1FFFFC), 0x5A5A5A5A, "OTC wraparound: the entry after the wraparound (at FFFFFCh, past RAM) is not written into RAM");
}

// A channel runs only if its master enable in DPCR is set
void checkMasterEnable() {
	{
		Bus bus;
		bus.write32(0xFF4, 0xA5A5A5A5);
		bus.write32(channelMadr(6), 0x1000);
		bus.write32(channelBcr(6), 4);
		bus.write32(channelChcr(6), CHCR_OTC_START);   // channel 6 is not enabled in DPCR
		check(bus.read32(0xFF4) == 0xA5A5A5A5 && bus.read32(0x1000) != 0x00000FFC, "master enable: channel 6 does nothing while bit 27 of DPCR is clear");
	}
	{
		Bus bus;
		for (uint32_t index = 0; index < 16; index++) bus.write32(0x4000 + index * 4, 0x01010101 * (index + 1));
		bus.write16(SPU_TRANSFER_ADDRESS, 0x100);
		bus.write16(SPUCNT, SPUCNT_ENABLED | SPUCNT_DMA_WRITE);
		bus.write32(channelMadr(4), 0x4000);
		bus.write32(channelBcr(4), 0x00010010);
		bus.write32(channelChcr(4), CHCR_SPU_WRITE);   // channel 4 is not enabled in DPCR
		runCycles(bus, 4000);
		checkEqual(bus.read32(channelMadr(4)), 0x4000, "master enable: channel 4 moves nothing while bit 19 of DPCR is clear (MADR stays at the start)");
	}
}

uint32_t patternWord(uint32_t index) { return 0x9E3779B9u * (index + 1); }

// RAM -> SPU sound RAM -> RAM through channel 4, in blocks
void checkSpuRoundTrip(uint32_t blockSize, uint32_t blockCount) {
	const std::string name = "SPU, " + std::to_string(blockCount) + " block" + (blockCount == 1 ? "" : "s") + " of " + std::to_string(blockSize) + " words";
	const uint32_t words = blockSize * blockCount;
	const uint32_t source = 0x10000, destination = 0x20000, soundRamAddress = 0x1000;

	Bus bus;
	for (uint32_t index = 0; index < words; index++) {
		bus.write32(source + index * 4, patternWord(index));
		bus.write32(destination + index * 4, 0);
	}
	bus.write32(DICR, 0x00800000 | (1u << 20));      // master enable and channel 4's interrupt enable

	// Write to sound RAM
	bus.write16(SPU_TRANSFER_ADDRESS, soundRamAddress / 8);
	bus.write16(SPUCNT, SPUCNT_ENABLED | SPUCNT_DMA_WRITE);
	enableChannel(bus, 4);
	bus.write32(channelMadr(4), source);
	bus.write32(channelBcr(4), blockSize | (blockCount << 16));
	bus.write32(channelChcr(4), CHCR_SPU_WRITE);
	runCycles(bus, 20000);

	checkEqual(bus.read32(channelMadr(4)), source + words * 4, name + ": MADR holds the end address after the transfer to the SPU (SyncMode 1 updates it)");
	checkEqual(bus.read32(channelBcr(4)), blockSize, name + ": BCR has its block counter counted down to zero, the block size is left");
	checkEqual(bus.read32(channelChcr(4)), 0x00000201, name + ": CHCR has the start bit cleared and the rest left as written");
	check((bus.read32(DICR) & (1u << 28)) != 0, name + ": the completion sets DICR flag 28 (channel 4)", "DICR is " + hex(bus.read32(DICR)));

	// Read it back
	bus.write16(SPU_TRANSFER_ADDRESS, soundRamAddress / 8);
	bus.write16(SPUCNT, SPUCNT_ENABLED | SPUCNT_DMA_READ);
	runCycles(bus, 2000);                           // the SPU fetches its first block before it asks for the transfer
	bus.write32(channelMadr(4), destination);
	bus.write32(channelBcr(4), blockSize | (blockCount << 16));
	bus.write32(channelChcr(4), CHCR_SPU_READ);
	runCycles(bus, 20000);

	std::string firstDifference;
	for (uint32_t index = 0; index < words && firstDifference.empty(); index++) {
		uint32_t got = bus.read32(destination + index * 4);
		if (got != patternWord(index)) firstDifference = "word " + std::to_string(index) + " is " + hex(got) + ", expected " + hex(patternWord(index));
	}
	check(firstDifference.empty(), name + ": the words read back from sound RAM are the words that were sent, in order", firstDifference);
	checkEqual(bus.read32(channelMadr(4)), destination + words * 4, name + ": MADR holds the end address after the transfer from the SPU");
}

// Bit 28 (force start) is cleared when the transfer begins, bit 24 stays set while it runs
void checkStartBitsWhileBusy() {
	Bus bus;
	for (uint32_t index = 0; index < 16; index++) bus.write32(0x4000 + index * 4, patternWord(index));
	bus.write16(SPU_TRANSFER_ADDRESS, 0x100);
	bus.write16(SPUCNT, SPUCNT_ENABLED | SPUCNT_DMA_WRITE);
	enableChannel(bus, 4);
	bus.write32(channelMadr(4), 0x4000);
	bus.write32(channelBcr(4), 0x00010010);
	bus.write32(channelChcr(4), CHCR_SPU_WRITE | CHCR_TRIGGER);
	uint32_t whileBusy = bus.read32(channelChcr(4));
	check(whileBusy == 0x01000201, "CHCR while a transfer runs: bit 28 was cleared when it began, bit 24 stays set until it completes", "CHCR is " + hex(whileBusy) + ", expected 0x01000201");
}

} // namespace

int main() {
	checkDpcr();
	checkChannelRegisters();
	checkOrderingTableList();
	checkOrderingTableRegisters();
	checkOrderingTableWraparound();
	checkMasterEnable();
	checkSpuRoundTrip(16, 4);
	checkSpuRoundTrip(8, 3);
	checkSpuRoundTrip(16, 1);
	checkStartBitsWhileBusy();
	return echo_check::finish();
}
