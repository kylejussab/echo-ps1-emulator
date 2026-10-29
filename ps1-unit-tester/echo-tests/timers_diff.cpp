// Differential test for the Timers class. Timers batches its work (tick(n) in one call); this runs thousands of random register writes and
// ticks against a second Timers that is forced to simulate one cycle at a time (reading a counter makes it catch up by a single cycle),
// and compares every counter, mode value and IRQ count. Run it again after any change to Timers.cpp.
#include "core/Constants.h"
#include "core/Timers.h"
#include "support/echo_check.h"
#include "support/timer_registers.h"
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace timer_registers;

int main() {
	static const int DOT_DIVIDERS[] = {4, 5, 7, 8, 10};
	std::mt19937 randomGenerator;
	long long totalCycles = 0;
	int comparisons = 0;

	for (int seed = 0; seed < 60; seed++) {
		randomGenerator.seed(seed * 7919 + 1);
		Timers batched, stepped;
		int batchedInterrupts = 0;
		int steppedInterrupts = 0;
		int dotDivider = 8;
		std::vector<std::string> history;
		long long elapsedCycles = 0;
		batched.setInterruptCallback([&](uint16_t) { batchedInterrupts++; });
		stepped.setInterruptCallback([&](uint16_t) { steppedInterrupts++; });
		batched.setDotDividerCallback([&] { return dotDivider; });
		stepped.setDotDividerCallback([&] { return dotDivider; });

		// On a mismatch: print the last operations for context, report one failed check and end the test (the two timers have diverged)
		auto reportMismatch = [&](const std::string& description) {
			for (size_t index = history.size() > 14 ? history.size() - 14 : 0; index < history.size(); index++) {
				std::printf("   %s\n", history[index].c_str());
			}
			echo_check::check(false, "batched ticks match one-cycle stepping", description);
			return echo_check::finish();
		};

		for (int operation = 0; operation < 400; operation++) {
			int kind = randomGenerator() % 10;
			if (kind < 4) { // tick
				int cycleCount = 1 + randomGenerator() % (randomGenerator() % 4 == 0 ? 20000 : 1500);
				totalCycles += cycleCount;
				elapsedCycles += cycleCount;
				history.push_back("tick " + std::to_string(cycleCount) + " (t=" + std::to_string(elapsedCycles) + ")");
				batched.advanceClock(cycleCount);
				for (int cycle = 0; cycle < cycleCount; cycle++) {
					stepped.advanceClock(1);
					(void)readRegister(stepped, TIMER_SYSTEM_CLOCK, COUNTER); // reading forces a one-cycle catch-up
				}
			} else if (kind < 6) { // mode write
				int timerIndex = randomGenerator() % 3;
				uint32_t mode = randomGenerator() & 0x3FF;
				if (randomGenerator() % 3) mode &= ~(IRQ_AT_TARGET | IRQ_AT_OVERFLOW); // IRQ bits less often, but keep some
				if (randomGenerator() % 2) mode |= RESET_AT_TARGET;
				if (randomGenerator() % 4 == 0) dotDivider = DOT_DIVIDERS[randomGenerator() % 5];
				writeRegister(batched, timerIndex, MODE, mode);
				writeRegister(stepped, timerIndex, MODE, mode);
				history.push_back("timer" + std::to_string(timerIndex) + " mode=" + std::to_string(mode) + " div=" + std::to_string(dotDivider));
			} else if (kind < 8) { // target or counter write
				int timerIndex = randomGenerator() % 3;
				uint32_t value = (randomGenerator() % 3 == 0) ? 0xFFFF - randomGenerator() % 40 : randomGenerator() % 0x300;
				int registerOffset = (randomGenerator() % 2) ? TARGET : COUNTER;
				writeRegister(batched, timerIndex, registerOffset, value);
				writeRegister(stepped, timerIndex, registerOffset, value);
				history.push_back("timer" + std::to_string(timerIndex) + (registerOffset == TARGET ? " target=" : " counter=") + std::to_string(value));
			} else { // read everything and compare
				for (int timerIndex = 0; timerIndex < 3; timerIndex++) {
					uint32_t batchedCounter = readRegister(batched, timerIndex, COUNTER);
					uint32_t steppedCounter = readRegister(stepped, timerIndex, COUNTER);
					uint32_t batchedMode = readRegister(batched, timerIndex, MODE);
					uint32_t steppedMode = readRegister(stepped, timerIndex, MODE);
					comparisons++;
					if (batchedCounter != steppedCounter || batchedMode != steppedMode) {
						char description[160];
						std::snprintf(description, sizeof(description), "seed=%d op=%d timer=%d counter %u vs %u, mode %04x vs %04x",
									seed, operation, timerIndex, batchedCounter, steppedCounter, batchedMode, steppedMode);
						return reportMismatch(description);
					}
				}
			}
		}

		// Final state and IRQ totals
		for (int timerIndex = 0; timerIndex < 3; timerIndex++) {
			uint32_t batchedCounter = readRegister(batched, timerIndex, COUNTER);
			uint32_t steppedCounter = readRegister(stepped, timerIndex, COUNTER);
			if (batchedCounter != steppedCounter) {
				char description[160];
				std::snprintf(description, sizeof(description), "final state, seed=%d timer=%d counter %u vs %u", seed, timerIndex, batchedCounter, steppedCounter);
				return reportMismatch(description);
			}
		}
		if (batchedInterrupts != steppedInterrupts) {
			char description[160];
			std::snprintf(description, sizeof(description), "IRQ count, seed=%d: %d vs %d", seed, batchedInterrupts, steppedInterrupts);
			return reportMismatch(description);
		}
	}

	echo_check::check(true, "batched ticks match one-cycle stepping: " + std::to_string(comparisons) + " comparisons, " + std::to_string(totalCycles) + " simulated cycles, IRQ counts identical");
	return echo_check::finish();
}
