// Checks of the documented Timers behaviour: counter, mode, target, IRQs, clock sources and sync modes.
// Needs no BIOS, no disc and no SDL.
#include "core/Constants.h"
#include "core/Timers.h"
#include "support/echo_check.h"
#include "support/timer_registers.h"
#include <algorithm>
#include <cstdint>
#include <string>

using echo_check::check;
using echo_check::checkEqual;
using echo_check::checkRange;
using namespace timer_registers;

int main() {
	// 1. Target reset: "reached target" set, "reached overflow" clear, and the counter never goes above the target
	{
		Timers timers;
		writeRegister(timers, TIMER_SYSTEM_CLOCK, TARGET, 0x10);
		writeRegister(timers, TIMER_SYSTEM_CLOCK, MODE, RESET_AT_TARGET);
		uint32_t highestCounter = 0;
		for (int cycle = 0; cycle < 500; cycle++) {
			timers.tick(1);
			highestCounter = std::max<uint32_t>(highestCounter, readRegister(timers, TIMER_SYSTEM_CLOCK, COUNTER));
		}
		uint32_t mode = readRegister(timers, TIMER_SYSTEM_CLOCK, MODE);
		check(isSet(mode, REACHED_TARGET), "target reset: reached-target flag set");
		check(!isSet(mode, REACHED_OVERFLOW), "target reset: reached-overflow flag clear");
		checkEqual(highestCounter, 0x10, "target reset: target value is visible and never exceeded");
		check(!isSet(readRegister(timers, TIMER_SYSTEM_CLOCK, MODE), REACHED_TARGET), "target reset: reached-target flag cleared by reading");
	}

	// 2. System clock / 8 counts eight times slower than the plain system clock
	{
		Timers dividedTimers, plainTimers;
		writeRegister(dividedTimers, TIMER_SYSTEM_CLOCK, TARGET, 0xFFFF);
		writeRegister(dividedTimers, TIMER_SYSTEM_CLOCK, MODE, CLOCK_SOURCE_BIT_9);
		dividedTimers.tick(8000);
		writeRegister(plainTimers, TIMER_SYSTEM_CLOCK, TARGET, 0xFFFF);
		writeRegister(plainTimers, TIMER_SYSTEM_CLOCK, MODE, 0);
		plainTimers.tick(8000);
		uint32_t dividedCounter = readRegister(dividedTimers, TIMER_SYSTEM_CLOCK, COUNTER);
		uint32_t plainCounter = readRegister(plainTimers, TIMER_SYSTEM_CLOCK, COUNTER);
		check(dividedCounter != 0 && plainCounter / dividedCounter == 8, "system clock / 8 ratio",
			"divided counter " + echo_check::formatValue(dividedCounter) + ", plain counter " + echo_check::formatValue(plainCounter));
	}

	// 3. A mode write resets the counter and sets bit 10
	{
		Timers timers;
		timers.tick(500);
		checkEqual(readRegister(timers, TIMER_SYSTEM_CLOCK, COUNTER), 500, "free run: counter counts");
		writeRegister(timers, TIMER_SYSTEM_CLOCK, MODE, 0);
		checkEqual(readRegister(timers, TIMER_SYSTEM_CLOCK, COUNTER), 0, "mode write resets the counter");
		check(isSet(readRegister(timers, TIMER_SYSTEM_CLOCK, MODE), IRQ_NOT_REQUESTED), "mode write sets bit 10");
	}

	// 4. Overflow flag
	{
		Timers timers;
		writeRegister(timers, TIMER_SYSTEM_CLOCK, TARGET, 0xFFFF);
		timers.tick(70000);
		uint32_t mode = readRegister(timers, TIMER_SYSTEM_CLOCK, MODE);
		check(isSet(mode, REACHED_OVERFLOW), "overflow: flag set");
		check(!isSet(readRegister(timers, TIMER_SYSTEM_CLOCK, MODE), REACHED_OVERFLOW), "overflow: flag cleared by reading");
	}

	// 5. IRQs: once versus repeat, and the right IRQ number for each timer (each timer checked on its own object)
	{
		Timers timers;
		int interruptCount = 0;
		uint16_t interruptBits = 0;
		timers.setInterruptCallback([&](uint16_t bits) { interruptCount++; interruptBits |= bits; });
		writeRegister(timers, TIMER_SYSTEM_CLOCK, TARGET, 0x20);
		writeRegister(timers, TIMER_SYSTEM_CLOCK, MODE, RESET_AT_TARGET | IRQ_AT_TARGET);
		timers.tick(500);
		check(interruptCount == 1 && interruptBits == 0x40, "IRQ once: a single IRQ6",
			"IRQ count " + std::to_string(interruptCount) + ", IRQ bits " + echo_check::formatValue(interruptBits));
		interruptCount = 0;
		interruptBits = 0;
		writeRegister(timers, TIMER_SYSTEM_CLOCK, MODE, RESET_AT_TARGET | IRQ_AT_TARGET | IRQ_REPEAT);
		timers.tick(500);
		check(interruptCount >= 10, "IRQ repeat: many IRQs", "IRQ count " + std::to_string(interruptCount));
	}
	{
		Timers timers;
		int interruptCount = 0;
		uint16_t interruptBits = 0;
		timers.setInterruptCallback([&](uint16_t bits) { interruptCount++; interruptBits |= bits; });
		writeRegister(timers, TIMER_DOT_CLOCK, TARGET, 0x20);
		writeRegister(timers, TIMER_DOT_CLOCK, MODE, RESET_AT_TARGET | IRQ_AT_TARGET | IRQ_REPEAT);
		timers.tick(200);
		check(interruptCount >= 3 && interruptBits == 0x10, "timer 0 raises IRQ4 only",
			"IRQ count " + std::to_string(interruptCount) + ", IRQ bits " + echo_check::formatValue(interruptBits));
	}
	{
		Timers timers;
		int interruptCount = 0;
		uint16_t interruptBits = 0;
		timers.setInterruptCallback([&](uint16_t bits) { interruptCount++; interruptBits |= bits; });
		writeRegister(timers, TIMER_HBLANK, TARGET, 0x20);
		writeRegister(timers, TIMER_HBLANK, MODE, RESET_AT_TARGET | IRQ_AT_TARGET | IRQ_REPEAT);
		timers.tick(200);
		check(interruptCount >= 3 && interruptBits == 0x20, "timer 1 raises IRQ5 only",
			"IRQ count " + std::to_string(interruptCount) + ", IRQ bits " + echo_check::formatValue(interruptBits));
	}

	// 6. Pulse mode leaves bit 10 high afterwards; toggle mode shows both states
	{
		Timers timers;
		writeRegister(timers, TIMER_SYSTEM_CLOCK, TARGET, 0x80);
		writeRegister(timers, TIMER_SYSTEM_CLOCK, MODE, RESET_AT_TARGET | IRQ_AT_TARGET | IRQ_REPEAT);
		timers.tick(1000);
		check(isSet(readRegister(timers, TIMER_SYSTEM_CLOCK, MODE), IRQ_NOT_REQUESTED), "pulse mode: bit 10 high");
	}
	{
		Timers timers;
		int interruptCount = 0;
		timers.setInterruptCallback([&](uint16_t) { interruptCount++; });
		writeRegister(timers, TIMER_SYSTEM_CLOCK, TARGET, 0x10);
		writeRegister(timers, TIMER_SYSTEM_CLOCK, MODE, RESET_AT_TARGET | IRQ_AT_TARGET | IRQ_REPEAT | IRQ_TOGGLE);
		bool sawLow = false;
		bool sawHigh = false;
		timers.tick(200);
		for (int sample = 0; sample < 50; sample++) {
			if (isSet(readRegister(timers, TIMER_SYSTEM_CLOCK, MODE), IRQ_NOT_REQUESTED)) {
				sawHigh = true;
			} else {
				sawLow = true;
			}
			timers.tick(10);
		}
		check(sawLow && sawHigh, "toggle mode: bit 10 shows both states");
		check(interruptCount > 0, "toggle mode: raises IRQs");
	}

	// 7. Timer 2 sync modes: 0 and 3 stop the counter, 1 and 2 let it run
	for (int syncModeNumber = 0; syncModeNumber < 4; syncModeNumber++) {
		Timers timers;
		writeRegister(timers, TIMER_SYSTEM_CLOCK, TARGET, 0xFFFF);
		writeRegister(timers, TIMER_SYSTEM_CLOCK, MODE, syncMode(syncModeNumber));
		timers.tick(5000);
		uint32_t counterAfterFirstWait = readRegister(timers, TIMER_SYSTEM_CLOCK, COUNTER);
		timers.tick(5000);
		uint32_t counterAfterSecondWait = readRegister(timers, TIMER_SYSTEM_CLOCK, COUNTER);
		std::string details = "counter " + std::to_string(counterAfterFirstWait) + " then " + std::to_string(counterAfterSecondWait);
		if (syncModeNumber == 0 || syncModeNumber == 3) {
			check(counterAfterFirstWait == counterAfterSecondWait, "timer 2 sync mode " + std::to_string(syncModeNumber) + " stops the counter", details);
		} else {
			check(counterAfterFirstWait != counterAfterSecondWait, "timer 2 sync mode " + std::to_string(syncModeNumber) + " lets the counter run", details);
		}
	}

	// 8. Timer 1 with the hblank source: about one tick per scanline
	{
		Timers timers;
		writeRegister(timers, TIMER_HBLANK, MODE, CLOCK_SOURCE_BIT_8);
		timers.tick(Hardware::CYCLES_PER_FRAME);
		checkRange(readRegister(timers, TIMER_HBLANK, COUNTER), 262, 264, "hblank source counts the lines of a frame");
	}

	// 9. Dot clock rate: GPU clock / divider. NTSC GPU clock 53.693175 MHz, CPU 33.8688 MHz = 715909/451584 per cycle,
	// so 7000 cycles per divider step -> 11097 ticks
	{
		Timers timers;
		timers.setDotDividerCallback([] { return 10; });
		writeRegister(timers, TIMER_DOT_CLOCK, MODE, CLOCK_SOURCE_BIT_8);
		timers.tick(70000);
		checkRange(readRegister(timers, TIMER_DOT_CLOCK, COUNTER), 11087, 11107, "dot clock, divider 10");
	}
	{
		Timers timers;
		timers.setDotDividerCallback([] { return 8; });
		writeRegister(timers, TIMER_DOT_CLOCK, MODE, CLOCK_SOURCE_BIT_8);
		timers.tick(56000);
		checkRange(readRegister(timers, TIMER_DOT_CLOCK, COUNTER), 11087, 11107, "dot clock, divider 8");
	}

	// 10. Timer 0 gate mode 1: reset at the start of hblank, so the counter stays small (less than one line)
	{
		Timers timers;
		writeRegister(timers, TIMER_DOT_CLOCK, TARGET, 0xFFFF);
		writeRegister(timers, TIMER_DOT_CLOCK, MODE, syncMode(1));
		timers.tick(Hardware::CYCLES_PER_SCANLINE * 5 + 10);
		uint32_t counter = readRegister(timers, TIMER_DOT_CLOCK, COUNTER);
		check(counter <= Hardware::CYCLES_PER_SCANLINE, "gate mode 1 resets the counter at each hblank", "counter " + echo_check::formatValue(counter));
	}

	// 11. Timer 0 gate mode 3: paused until the first hblank, then free-runs
	{
		Timers timers;
		writeRegister(timers, TIMER_DOT_CLOCK, TARGET, 0xFFFF);
		writeRegister(timers, TIMER_DOT_CLOCK, MODE, syncMode(3));
		timers.tick(100);
		checkEqual(readRegister(timers, TIMER_DOT_CLOCK, COUNTER), 0, "gate mode 3: paused before the first hblank");
		timers.tick(Hardware::CYCLES_PER_SCANLINE);
		uint32_t counter = readRegister(timers, TIMER_DOT_CLOCK, COUNTER);
		check(counter > 0, "gate mode 3: free-runs after the first hblank", "counter " + echo_check::formatValue(counter));
	}

	// 12. Timer 0 gate mode 0: counts less than a free-running timer because it pauses during hblank
	{
		Timers gatedTimers, freeTimers;
		writeRegister(gatedTimers, TIMER_DOT_CLOCK, MODE, syncMode(0));
		writeRegister(freeTimers, TIMER_DOT_CLOCK, MODE, 0);
		gatedTimers.tick(Hardware::CYCLES_PER_SCANLINE * 20);
		freeTimers.tick(Hardware::CYCLES_PER_SCANLINE * 20);
		uint32_t gatedCounter = readRegister(gatedTimers, TIMER_DOT_CLOCK, COUNTER);
		uint32_t freeCounter = readRegister(freeTimers, TIMER_DOT_CLOCK, COUNTER);
		check(gatedCounter < freeCounter, "gate mode 0 pauses during hblank",
			"gated counter " + echo_check::formatValue(gatedCounter) + ", free counter " + echo_check::formatValue(freeCounter));
	}

	// 13. Timer 1 gate (vblank) mode 2: only counts during vblank
	{
		Timers timers;
		writeRegister(timers, TIMER_HBLANK, MODE, syncMode(2));
		timers.tick(Hardware::CYCLES_PER_FRAME - 100);
		uint32_t counter = readRegister(timers, TIMER_HBLANK, COUNTER);
		check(counter <= Hardware::CYCLES_PER_SCANLINE * Timers::VBLANK_LINES + 10 && counter > 0, "vblank-only counting (gate mode 2)",
			"counter " + echo_check::formatValue(counter));
	}

	return echo_check::finish();
}
