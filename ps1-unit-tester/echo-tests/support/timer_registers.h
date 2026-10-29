// Register access and bit names for the Timers tests (timers_unit.cpp, timers_diff.cpp).
#pragma once
#include <cstdint>
#include "core/Timers.h"

namespace timer_registers {

constexpr uint32_t BASE_ADDRESS = 0x1F801100; // timer n lives at BASE_ADDRESS + n * 0x10

// Which register of a timer
constexpr int COUNTER = 0;
constexpr int MODE = 4;
constexpr int TARGET = 8;

constexpr int TIMER_DOT_CLOCK = 0;
constexpr int TIMER_HBLANK = 1;
constexpr int TIMER_SYSTEM_CLOCK = 2;

// Mode register bits you write
constexpr uint32_t SYNC_ENABLE = 0x001;       // bits 1-2 then choose the sync mode (see syncMode)
constexpr uint32_t RESET_AT_TARGET = 0x008;
constexpr uint32_t IRQ_AT_TARGET = 0x010;
constexpr uint32_t IRQ_AT_OVERFLOW = 0x020;
constexpr uint32_t IRQ_REPEAT = 0x040;        // 0 = IRQ once
constexpr uint32_t IRQ_TOGGLE = 0x080;        // 0 = short pulse
constexpr uint32_t CLOCK_SOURCE_BIT_8 = 0x100;
constexpr uint32_t CLOCK_SOURCE_BIT_9 = 0x200;

// Mode register bits you read
constexpr uint32_t IRQ_NOT_REQUESTED = 0x400; // bit 10: 1 = no IRQ pending
constexpr uint32_t REACHED_TARGET = 0x800;    // cleared by reading the register
constexpr uint32_t REACHED_OVERFLOW = 0x1000; // cleared by reading the register

constexpr uint32_t syncMode(int mode) {
	return SYNC_ENABLE | (static_cast<uint32_t>(mode) << 1);
}

inline uint32_t readRegister(Timers& timers, int timerIndex, int registerOffset) {
	return timers.read(BASE_ADDRESS + timerIndex * 0x10 + registerOffset);
}

inline void writeRegister(Timers& timers, int timerIndex, int registerOffset, uint32_t value) {
	timers.write(BASE_ADDRESS + timerIndex * 0x10 + registerOffset, value);
}

inline bool isSet(uint32_t value, uint32_t bits) {
	return (value & bits) != 0;
}

} // namespace timer_registers
