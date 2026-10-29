// Register accesses through the Bus: does the access work at the widths the hardware accepts, and do the interrupt registers behave?
//
// The Bus prints FATAL and exits on anything it does not handle, so every probe runs in its own child process (support/isolated_probe.h):
// one unsupported access is one failed check, and the rest still run. Needs no BIOS.
//
// What is checked is what the hardware documents (psx-spx "Interrupts", "Memory Control", "Serial Port (SIO)"). Behaviour I could not
// confirm is left out rather than guessed: for example whether bits 11-15 of I_MASK read back as zero.
#include "core/Bus.h"
#include "support/echo_check.h"
#include "support/isolated_probe.h"
#include <cstdint>
#include <string>
#include <vector>

using echo_check::check;
using echo_check::hex;
using isolated_probe::Outcome;
using isolated_probe::run;

namespace {

constexpr uint32_t SIO1_BASE = 0x1F801050;
constexpr uint32_t RAM_SIZE_REGISTER = 0x1F801060;
constexpr uint32_t INTERRUPT_STATUS = 0x1F801070;   // I_STAT
constexpr uint32_t INTERRUPT_MASK = 0x1F801074;     // I_MASK

void checkProbeValue(const Outcome& outcome, uint32_t expected, const std::string& name) {
	if (!outcome.finished) {
		check(false, name, isolated_probe::describeFailure(outcome));
	} else {
		check(outcome.value == expected, name, "read " + hex(outcome.value) + ", expected " + hex(expected));
	}
}

// SIO1 (the serial port) has nothing attached here, but its registers still answer. A program may use any width on them.
void checkSio1Widths() {
	const std::vector<uint32_t> offsets32 = {0x00, 0x04, 0x08, 0x0C};
	const std::vector<uint32_t> offsets16 = {0x00, 0x04, 0x08, 0x0A, 0x0C, 0x0E};
	const std::vector<uint32_t> offsets8 = {0x00, 0x04, 0x08, 0x0A};

	for (uint32_t offset : offsets32) {
		uint32_t address = SIO1_BASE + offset;
		Outcome read = run([address] { Bus bus; return bus.read32(address); });
		check(read.finished, "SIO1: 32-bit read at " + hex(address) + " works", isolated_probe::describeFailure(read));
		Outcome write = run([address] { Bus bus; bus.write32(address, 0); return 0; });
		check(write.finished, "SIO1: 32-bit write at " + hex(address) + " works", isolated_probe::describeFailure(write));
	}
	for (uint32_t offset : offsets16) {
		uint32_t address = SIO1_BASE + offset;
		Outcome read = run([address] { Bus bus; return bus.read16(address); });
		check(read.finished, "SIO1: 16-bit read at " + hex(address) + " works", isolated_probe::describeFailure(read));
		Outcome write = run([address] { Bus bus; bus.write16(address, 0); return 0; });
		check(write.finished, "SIO1: 16-bit write at " + hex(address) + " works", isolated_probe::describeFailure(write));
	}
	for (uint32_t offset : offsets8) {
		uint32_t address = SIO1_BASE + offset;
		Outcome read = run([address] { Bus bus; return bus.read8(address); });
		check(read.finished, "SIO1: 8-bit read at " + hex(address) + " works", isolated_probe::describeFailure(read));
		Outcome write = run([address] { Bus bus; bus.write8(address, 0); return 0; });
		check(write.finished, "SIO1: 8-bit write at " + hex(address) + " works", isolated_probe::describeFailure(write));
	}
}

// The RAM size register (written by the BIOS with 0x00000B88) is read/write on the hardware
void checkRamSizeRegister() {
	Outcome write = run([] { Bus bus; bus.write32(RAM_SIZE_REGISTER, 0x00000B88); return 0; });
	check(write.finished, "RAM size register: 32-bit write works", isolated_probe::describeFailure(write));
	Outcome roundTrip = run([] {
		Bus bus;
		bus.write32(RAM_SIZE_REGISTER, 0x00000B88);
		return bus.read32(RAM_SIZE_REGISTER);
	});
	checkProbeValue(roundTrip, 0x00000B88, "RAM size register: reads back what was written");
}

// I_STAT: a raised interrupt shows up, and a write acknowledges (clears) the bits written as 0 while bits written as 1 are left alone
void checkInterruptStatus() {
	checkProbeValue(run([] { Bus bus; bus.triggerHardwareInterrupt(0x0105); return bus.read32(INTERRUPT_STATUS); }),
					0x0105, "I_STAT: a raised interrupt is visible (32-bit read)");
	checkProbeValue(run([] { Bus bus; bus.triggerHardwareInterrupt(0x0105); return bus.read16(INTERRUPT_STATUS); }),
					0x0105, "I_STAT: a raised interrupt is visible (16-bit read)");
	checkProbeValue(run([] {
						Bus bus;
						bus.triggerHardwareInterrupt(0x0105);
						bus.write32(INTERRUPT_STATUS, ~0x0004u);
						return bus.read32(INTERRUPT_STATUS);
					}),
					0x0101, "I_STAT: writing 0 to a bit acknowledges only that bit (32-bit write)");
	checkProbeValue(run([] {
						Bus bus;
						bus.triggerHardwareInterrupt(0x0105);
						bus.write16(INTERRUPT_STATUS, 0xFFFB);
						return bus.read32(INTERRUPT_STATUS);
					}),
					0x0101, "I_STAT: writing 0 to a bit acknowledges only that bit (16-bit write)");
	checkProbeValue(run([] {
						Bus bus;
						bus.write32(INTERRUPT_STATUS, 0xFFFFFFFF);
						return bus.read32(INTERRUPT_STATUS);
					}),
					0x0000, "I_STAT: writing 1s cannot raise an interrupt");
}

// I_MASK: the eleven interrupt bits read back what was written
void checkInterruptMask() {
	checkProbeValue(run([] { Bus bus; bus.write32(INTERRUPT_MASK, 0x07FF); return bus.read32(INTERRUPT_MASK) & 0x07FF; }),
					0x07FF, "I_MASK: all eleven bits can be set (32-bit)");
	checkProbeValue(run([] { Bus bus; bus.write32(INTERRUPT_MASK, 0x0555); return bus.read32(INTERRUPT_MASK) & 0x07FF; }),
					0x0555, "I_MASK: an alternating pattern reads back (32-bit)");
	checkProbeValue(run([] { Bus bus; bus.write16(INTERRUPT_MASK, 0x02AA); return bus.read16(INTERRUPT_MASK) & 0x07FF; }),
					0x02AA, "I_MASK: an alternating pattern reads back (16-bit)");
	checkProbeValue(run([] { Bus bus; bus.write32(INTERRUPT_MASK, 0x07FF); bus.write32(INTERRUPT_MASK, 0); return bus.read32(INTERRUPT_MASK); }),
					0x0000, "I_MASK: writing 0 clears it");
}

// The CPU only sees an interrupt when a bit is raised in I_STAT and enabled in I_MASK
void checkInterruptGating() {
	Outcome gating = run([] {
		Bus bus;
		bus.triggerHardwareInterrupt(0x0004);
		uint32_t result = 0;
		bus.write32(INTERRUPT_MASK, 0x0000);
		result |= bus.hasPendingInterrupts() ? 0x1 : 0;   // raised, not enabled
		bus.write32(INTERRUPT_MASK, 0x0002);
		result |= bus.hasPendingInterrupts() ? 0x2 : 0;   // a different bit enabled
		bus.write32(INTERRUPT_MASK, 0x0004);
		result |= bus.hasPendingInterrupts() ? 0x4 : 0;   // raised and enabled
		bus.write32(INTERRUPT_STATUS, ~0x0004u);
		result |= bus.hasPendingInterrupts() ? 0x8 : 0;   // acknowledged
		return result;
	});
	if (!gating.finished) {
		std::string details = isolated_probe::describeFailure(gating);
		check(false, "interrupt gating: raised but not enabled is not pending", details);
		check(false, "interrupt gating: another enabled bit does not make it pending", details);
		check(false, "interrupt gating: raised and enabled is pending", details);
		check(false, "interrupt gating: acknowledging clears the pending state", details);
		return;
	}
	check((gating.value & 0x1) == 0, "interrupt gating: raised but not enabled is not pending");
	check((gating.value & 0x2) == 0, "interrupt gating: another enabled bit does not make it pending");
	check((gating.value & 0x4) != 0, "interrupt gating: raised and enabled is pending");
	check((gating.value & 0x8) == 0, "interrupt gating: acknowledging clears the pending state");
}

} // namespace

int main() {
	checkSio1Widths();
	checkRamSizeRegister();
	checkInterruptStatus();
	checkInterruptMask();
	checkInterruptGating();
	return echo_check::finish();
}
