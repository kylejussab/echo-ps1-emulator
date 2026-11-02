// Memory map of the Bus, checked against the documented hardware layout (psx-spx "Memory Map"):
// RAM and its mirrors, KUSEG/KSEG0/KSEG1 aliasing, the scratchpad, the BIOS ROM and unattached expansion region 1.
// Needs no real BIOS: it generates one (support/synthetic_bios.h).
//
// The Bus decodes the top address bits itself, so only what a plain read/write can show is checked here. Things that depend on which segment
// the CPU used (the scratchpad being unreachable through KSEG1, KSEG2 faulting) belong to the CPU's memory-access path, not to this test.
#include "core/Bus.h"
#include "support/echo_check.h"
#include "support/synthetic_bios.h"
#include <cstdint>
#include <string>

using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;

namespace {

// The hardware layout, written out here on purpose instead of using the emulator's own constants: the test should not share the numbers it checks.
constexpr uint32_t RAM_SIZE = 2 * 1024 * 1024;
constexpr uint32_t RAM_MIRROR_COUNT = 4;   // the 2 MB repeat four times across the first 8 MB
constexpr uint32_t SCRATCHPAD_START = 0x1F800000;
constexpr uint32_t SCRATCHPAD_SIZE = 1024;
constexpr uint32_t BIOS_START = 0x1FC00000;
constexpr uint32_t EXPANSION_1_START = 0x1F000000;
constexpr uint32_t EXPANSION_1_SIZE = 8 * 1024 * 1024;

// The same physical memory is visible through these three segments
constexpr uint32_t KUSEG = 0x00000000;
constexpr uint32_t KSEG0 = 0x80000000;
constexpr uint32_t KSEG1 = 0xA0000000;

uint32_t pattern(uint32_t seed) {
	return seed * 0x9E3779B1u + 0x01234567u;
}

void checkRamMirrors() {
	Bus bus;
	const uint32_t offsets[] = {0x00000000, 0x00000004, 0x00100000, 0x001FFFFC};
	for (uint32_t offset : offsets) {
		bus.write32(offset, pattern(offset));
		bool allMirrorsMatch = true;
		std::string details;
		for (uint32_t mirror = 0; mirror < RAM_MIRROR_COUNT && allMirrorsMatch; mirror++) {
			uint32_t address = mirror * RAM_SIZE + offset;
			uint32_t value = bus.read32(address);
			if (value != pattern(offset)) {
				allMirrorsMatch = false;
				details = "read " + hex(value) + " at " + hex(address) + ", expected " + hex(pattern(offset));
			}
		}
		check(allMirrorsMatch, "RAM word at " + hex(offset) + " reads the same in all four 2 MB mirrors", details);
	}

	for (uint32_t mirror = 0; mirror < RAM_MIRROR_COUNT; mirror++) {
		bus.write32(mirror * RAM_SIZE + 0x200, pattern(mirror + 100));
		checkEqual(bus.read32(0x200), pattern(mirror + 100), "a write through RAM mirror " + std::to_string(mirror) + " is seen at 0x00000200");
	}

	bus.write8(RAM_SIZE + 0x301, 0xAB);
	checkEqual(bus.read8(0x301), 0xAB, "a byte written through mirror 1 is seen in the base RAM");
	bus.write16(3 * RAM_SIZE + 0x402, 0xBEEF);
	checkEqual(bus.read16(0x402), 0xBEEF, "a halfword written through mirror 3 is seen in the base RAM");
	bus.write8(0x007FFFFF, 0x5A);
	checkEqual(bus.read8(0x001FFFFF), 0x5A, "the last byte of the 8 MB window is the last byte of RAM");
}

void checkSegmentAliasing() {
	Bus bus;
	bus.write32(KSEG1 + 0x1000, 0xA1B2C3D4);
	checkEqual(bus.read32(KUSEG + 0x1000), 0xA1B2C3D4u, "RAM written through KSEG1 reads back through KUSEG");
	checkEqual(bus.read32(KSEG0 + 0x1000), 0xA1B2C3D4u, "RAM written through KSEG1 reads back through KSEG0");

	bus.write32(KSEG0 + 0x2000, 0x01020304);
	checkEqual(bus.read32(KUSEG + 0x2000), 0x01020304u, "RAM written through KSEG0 reads back through KUSEG");
	checkEqual(bus.read32(KSEG1 + 0x2000), 0x01020304u, "RAM written through KSEG0 reads back through KSEG1");

	bus.write32(KUSEG + 0x3000, 0x0BADF00D);
	checkEqual(bus.read32(KSEG0 + RAM_SIZE + 0x3000), 0x0BADF00Du, "a RAM mirror is visible through KSEG0 too");

	bus.write32(KUSEG + SCRATCHPAD_START + 0x10, 0x5C4A7C80);
	checkEqual(bus.read32(KSEG0 + SCRATCHPAD_START + 0x10), 0x5C4A7C80u, "scratchpad written through KUSEG reads back through KSEG0");
}

void checkScratchpad() {
	Bus bus;
	uint32_t wordCount = SCRATCHPAD_SIZE / 4;
	for (uint32_t word = 0; word < wordCount; word++) {
		bus.write32(SCRATCHPAD_START + word * 4, pattern(word));
	}
	bool allWordsMatch = true;
	std::string details;
	for (uint32_t word = 0; word < wordCount && allWordsMatch; word++) {
		uint32_t value = bus.read32(SCRATCHPAD_START + word * 4);
		if (value != pattern(word)) {
			allWordsMatch = false;
			details = "word " + std::to_string(word) + " read " + hex(value) + ", expected " + hex(pattern(word));
		}
	}
	check(allWordsMatch, "all 1 KB of the scratchpad holds " + std::to_string(wordCount) + " independent words", details);

	// Scratchpad and RAM are separate storage
	Bus separateBus;
	separateBus.write32(0x00000000, 0x11111111);
	separateBus.write32(SCRATCHPAD_START, 0x22222222);
	checkEqual(separateBus.read32(0x00000000), 0x11111111u, "a scratchpad write does not change RAM");
	checkEqual(separateBus.read32(SCRATCHPAD_START), 0x22222222u, "a RAM write does not change the scratchpad");
}

// Byte, halfword and word accesses to the same memory agree, little-endian
void checkLittleEndianWidths(const std::string& regionName, uint32_t base) {
	Bus bus;
	bus.write32(base, 0x44332211);
	check(bus.read8(base) == 0x11 && bus.read8(base + 1) == 0x22 && bus.read8(base + 2) == 0x33 && bus.read8(base + 3) == 0x44,
		regionName + ": a word is stored little-endian (byte reads)",
		"bytes " + hex(bus.read8(base), 2) + " " + hex(bus.read8(base + 1), 2) + " " + hex(bus.read8(base + 2), 2) + " " + hex(bus.read8(base + 3), 2));
	check(bus.read16(base) == 0x2211 && bus.read16(base + 2) == 0x4433, regionName + ": a word is stored little-endian (halfword reads)",
		"halfwords " + hex(bus.read16(base), 4) + " " + hex(bus.read16(base + 2), 4));

	bus.write16(base + 4, 0xBBAA);
	check(bus.read8(base + 4) == 0xAA && bus.read8(base + 5) == 0xBB, regionName + ": a halfword is stored little-endian",
		"bytes " + hex(bus.read8(base + 4), 2) + " " + hex(bus.read8(base + 5), 2));

	bus.write8(base + 8, 0x7F);
	bus.write8(base + 9, 0x80);
	bus.write8(base + 10, 0x81);
	bus.write8(base + 11, 0x82);
	checkEqual(bus.read32(base + 8), 0x8281807Fu, regionName + ": four byte writes make one word");
}

void checkBios(const std::string& biosPath) {
	Bus bus;
	bus.loadBIOS(biosPath);

	const uint32_t offsets[] = {0x00000000, 0x00000004, 0x00001000, 0x00040000, synthetic_bios::SIZE - 4};
	for (uint32_t offset : offsets) {
		uint32_t expected = synthetic_bios::wordAt(offset);
		bool allSegmentsMatch = true;
		std::string details;
		for (uint32_t segment : {KUSEG, KSEG0, KSEG1}) {
			uint32_t address = segment + BIOS_START + offset;
			uint32_t value = bus.read32(address);
			if (value != expected) {
				allSegmentsMatch = false;
				details = "read " + hex(value) + " at " + hex(address) + ", expected " + hex(expected);
			}
		}
		check(allSegmentsMatch, "BIOS word at offset " + hex(offset) + " is visible through KUSEG, KSEG0 and KSEG1", details);
	}
	checkEqual(bus.read8(BIOS_START + 5), synthetic_bios::byteAt(5), "BIOS byte read");
	checkEqual(bus.read16(BIOS_START + 6), synthetic_bios::halfwordAt(6), "BIOS halfword read");

	// The ROM ignores writes. The values written are the complement of what is stored, so a write that did land cannot go unnoticed.
	bus.write32(BIOS_START, ~synthetic_bios::wordAt(0));
	bus.write16(BIOS_START + 8, static_cast<uint16_t>(~synthetic_bios::halfwordAt(8)));
	bus.write8(BIOS_START + 12, static_cast<uint8_t>(~synthetic_bios::byteAt(12)));
	bus.write32(KSEG1 + BIOS_START + 16, ~synthetic_bios::wordAt(16));
	bool contentUnchanged = bus.read32(BIOS_START) == synthetic_bios::wordAt(0)
						&& bus.read16(BIOS_START + 8) == synthetic_bios::halfwordAt(8)
						&& bus.read8(BIOS_START + 12) == synthetic_bios::byteAt(12)
						&& bus.read32(BIOS_START + 16) == synthetic_bios::wordAt(16);
	check(contentUnchanged, "BIOS ROM ignores 8, 16 and 32-bit writes");
	checkEqual(bus.read32(0x00000000), 0u, "a BIOS write does not land in RAM");
}

void checkExpansion1() {
	Bus bus;
	for (uint32_t address : {EXPANSION_1_START, EXPANSION_1_START + 0x84, EXPANSION_1_START + EXPANSION_1_SIZE - 4}) {
		checkEqual(bus.read8(address), 0xFF, "expansion 1 with nothing attached: 8-bit read at " + hex(address) + " is all ones");
		checkEqual(bus.read16(address), 0xFFFF, "expansion 1 with nothing attached: 16-bit read at " + hex(address) + " is all ones");
		checkEqual(bus.read32(address), 0xFFFFFFFFu, "expansion 1 with nothing attached: 32-bit read at " + hex(address) + " is all ones");
	}
	bus.write32(EXPANSION_1_START, 0);
	checkEqual(bus.read32(EXPANSION_1_START), 0xFFFFFFFFu, "a write to expansion 1 is ignored");
}

} // namespace

int main() {
	checkRamMirrors();
	checkSegmentAliasing();
	checkScratchpad();
	checkLittleEndianWidths("RAM", 0x00000100);
	checkLittleEndianWidths("scratchpad", SCRATCHPAD_START + 0x100);
	checkExpansion1();

	std::string biosPath = synthetic_bios::createFile();
	if (check(!biosPath.empty(), "could write a temporary BIOS image")) {
		checkBios(biosPath);
		synthetic_bios::removeFile(biosPath);
	}
	return echo_check::finish();
}
