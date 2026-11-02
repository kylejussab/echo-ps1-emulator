// The SPU registers (1F801C00h-1F801FFFh): that the voice, volume and reverb registers hold what is written, the current-volume registers and the
// volume sweep, how an 8-bit access is treated, the registers the page calls "general-purpose RAM", and how STATX mirrors ATTR. Sound RAM transfers
// are in spu_transfers and the voices themselves (ADPCM, envelope, pitch, mixing) in spu_voices. Needs no BIOS and no other device: the SPU is driven
// only through its register interface.
//
// What is checked is what the psx-spx page "Sound Processing Unit (SPU)" documents:
//   - The voice volume registers take a fixed volume (bit 15 = 0, bits 0-14 = volume / 2: 3FFFh gives 7FFEh, 4000h gives -8000h) or start a sweep
//     (bit 15 = 1) from the current volume. A sweep steps once per sample by 7 - step (increase) or -(8 - step) (decrease), scaled by
//     2 ^ (11 - shift) for shift below 11, and for shift above 11 it steps once per 2 ^ (shift - 11) samples. The current volume is read at 1F801E00h + 4 * voice
//     (left) and +2 (right); the main volume's at 1F801DB8h / 1F801DBAh.
//   - 8-bit writes to ODD addresses are simply ignored; 8-bit writes to EVEN addresses are executed as 16-bit writes.
//   - 1F801DA0h, 1F801DBCh (4 bytes) and 1F801E60h (32 bytes) are read/write-able, "just some kind of general-purpose RAM".
//   - STATX bits 5-0 are ATTR bits 5-0, applied a little later.
// Left out on purpose because the page says "applied a bit delayed" / gives no value: the exact cycle at which a written volume or a mode change
// shows up, what a 32-bit access does beyond being two 16-bit accesses, the value of an even 8-bit write beyond its low byte (it is the CPU register's
// low halfword, which the Bus does not pass on), and the key on / off registers when read.
#include "spu/SPU.h"
#include "support/echo_check.h"
#include "support/spu_rig.h"
#include <cstdint>
#include <string>
#include <vector>

using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;
using namespace spu_rig;

namespace {

void checkVoiceRegisters() {
	for (int voice : {0, 11, 23}) {
		SPU spu;
		struct Case {
			uint32_t offset;
			uint16_t value;
			const char* name;
		};
		const Case cases[] = {
			{VOICE_VOLUME_LEFT, 0x3FFF, "volume left"}, {VOICE_VOLUME_RIGHT, 0x1234, "volume right"}, {VOICE_PITCH, 0x1000, "pitch"},
			{VOICE_START_ADDRESS, 0x0123, "start address"}, {VOICE_ADSR1, 0x80FF, "ADSR1"}, {VOICE_ADSR2, 0xDFC5, "ADSR2"},
			{VOICE_REPEAT_ADDRESS, 0x0456, "repeat address"},
		};
		for (const Case& testCase : cases) {
			spu.write16(voiceRegister(voice, testCase.offset), testCase.value);
			uint16_t read = spu.read16(voiceRegister(voice, testCase.offset));
			check(read == testCase.value, "voice " + std::to_string(voice) + " " + testCase.name + " register holds what is written", "wrote " + hex(testCase.value, 4) + ", read " + hex(read, 4));
		}
	}
	{
		SPU spu;
		spu.write16(0x1F801DB0, 0x7FFF);
		spu.write16(0x1F801DB2, 0x4321);
		spu.write16(0x1F801DC0, 0x1234);   // a reverb register
		spu.write16(0x1F801DFE, 0x5678);
		check(spu.read16(0x1F801DB0) == 0x7FFF && spu.read16(0x1F801DB2) == 0x4321 && spu.read16(0x1F801DC0) == 0x1234 && spu.read16(0x1F801DFE) == 0x5678,
			"the CD volume and reverb registers hold what is written");
	}
}

// Fixed volumes appear in the current-volume registers as volume * 2 (3FFFh -> 7FFEh, 4000h -> -8000h)
void checkCurrentVolume() {
	SPU spu;
	struct Case {
		uint16_t written;
		uint16_t current;
		const char* name;
	};
	const Case cases[] = {
		{0x3FFF, 0x7FFE, "3FFFh is the maximum, 7FFEh"},
		{0x0001, 0x0002, "0001h is 0002h"},
		{0x4000, 0x8000, "4000h is the most negative, 8000h"},
		{0x6000, 0xC000, "6000h is -4000h (C000h)"},
	};
	for (const Case& testCase : cases) {
		spu.write16(voiceRegister(5, VOICE_VOLUME_LEFT), testCase.written);
		spu.write16(voiceRegister(5, VOICE_VOLUME_RIGHT), testCase.written);
		runSamples(spu, 2);
		check(spu.read16(currentVolumeRegister(5, false)) == testCase.current && spu.read16(currentVolumeRegister(5, true)) == testCase.current,
			std::string("current volume of voice 5 for a fixed volume: ") + testCase.name,
			"left " + hex(spu.read16(currentVolumeRegister(5, false)), 4) + ", right " + hex(spu.read16(currentVolumeRegister(5, true)), 4));
	}
	spu.write16(MAIN_VOLUME_LEFT, 0x3FFF);
	spu.write16(MAIN_VOLUME_RIGHT, 0x2000);
	runSamples(spu, 2);
	check(spu.read16(MAIN_CURRENT_LEFT) == 0x7FFE && spu.read16(MAIN_CURRENT_RIGHT) == 0x4000, "current main volume for fixed volumes (3FFFh -> 7FFEh, 2000h -> 4000h)",
		"left " + hex(spu.read16(MAIN_CURRENT_LEFT), 4) + ", right " + hex(spu.read16(MAIN_CURRENT_RIGHT), 4));
}

// The current volume as it is after each of `samples` samples
std::vector<int> currentVolumes(SPU& spu, uint32_t reg, int samples) {
	std::vector<int> values;
	for (int sample = 0; sample < samples; sample++) {
		runSamples(spu, 1);
		values.push_back(static_cast<int16_t>(spu.read16(reg)));
	}
	return values;
}

bool everyDifferenceIs(const std::vector<int>& values, int difference, size_t from, size_t to) {
	for (size_t index = from; index + 1 < to && index + 1 < values.size(); index++) {
		if (values[index + 1] - values[index] != difference) return false;
	}
	return true;
}

std::string showValues(const std::vector<int>& values, size_t count) {
	std::string text;
	for (size_t index = 0; index < count && index < values.size(); index++) text += (index ? " " : "") + std::to_string(values[index]);
	return text;
}

// A sweep: bit 15 set, bit 14 exponential, bit 13 decrease, bit 12 negative phase, bits 6-2 shift, bits 1-0 step
void checkVolumeSweep() {
	{
		// Increase, linear, shift 11: one step per sample of 7 - step. From 0.
		for (int step = 0; step < 4; step++) {
			SPU spu;
			spu.write16(voiceRegister(3, VOICE_VOLUME_LEFT), static_cast<uint16_t>(0x8000 | (11 << 2) | step));
			std::vector<int> values = currentVolumes(spu, currentVolumeRegister(3, false), 12);
			check(everyDifferenceIs(values, 7 - step, 2, 12), "volume sweep: linear increase, shift 11, step " + std::to_string(step) + " raises the volume by " + std::to_string(7 - step) + " per sample",
				"volumes: " + showValues(values, 12));
		}
	}
	{
		// Decrease, linear, shift 11: -(8 - step) per sample, from the current (fixed) volume
		for (int step = 0; step < 4; step++) {
			SPU spu;
			spu.write16(voiceRegister(3, VOICE_VOLUME_LEFT), 0x3FFF);
			runSamples(spu, 2);
			spu.write16(voiceRegister(3, VOICE_VOLUME_LEFT), static_cast<uint16_t>(0xA000 | (11 << 2) | step));
			std::vector<int> values = currentVolumes(spu, currentVolumeRegister(3, false), 12);
			check(everyDifferenceIs(values, -(8 - step), 2, 12), "volume sweep: linear decrease, shift 11, step " + std::to_string(step) + " lowers the volume by " + std::to_string(8 - step) + " per sample, from the volume it had",
				"volumes: " + showValues(values, 12));
		}
	}
	{
		// Shift below 11 scales the step: shift 9 is four times the step
		SPU spu;
		spu.write16(voiceRegister(3, VOICE_VOLUME_LEFT), static_cast<uint16_t>(0x8000 | (9 << 2)));
		std::vector<int> values = currentVolumes(spu, currentVolumeRegister(3, false), 10);
		check(everyDifferenceIs(values, 7 * 4, 2, 10), "volume sweep: shift 9 steps by 7 * 4 per sample", "volumes: " + showValues(values, 10));
	}
	{
		// Shift above 11 steps less often: shift 12 steps once per two samples
		SPU spu;
		spu.write16(voiceRegister(3, VOICE_VOLUME_LEFT), static_cast<uint16_t>(0x8000 | (12 << 2)));
		std::vector<int> values = currentVolumes(spu, currentVolumeRegister(3, false), 16);
		bool everyOther = true;
		for (size_t index = 2; index + 2 < values.size(); index++) everyOther = everyOther && (values[index + 2] - values[index] == 7);
		check(everyOther, "volume sweep: shift 12 steps by 7 once every two samples", "volumes: " + showValues(values, 16));
	}
}

// 8-bit accesses: reads return the right half of the register; writes to odd addresses are ignored; writes to even addresses are 16-bit writes
void checkEightBitAccess() {
	{
		SPU spu;
		spu.write16(voiceRegister(0, VOICE_PITCH), 0x1234);
		check(spu.read8(voiceRegister(0, VOICE_PITCH)) == 0x34 && spu.read8(voiceRegister(0, VOICE_PITCH) + 1) == 0x12, "8-bit reads return the low and the high byte of a register");
	}
	{
		SPU spu;
		spu.write16(voiceRegister(0, VOICE_PITCH), 0x1234);
		spu.write8(voiceRegister(0, VOICE_PITCH) + 1, 0xAB);
		check(spu.read16(voiceRegister(0, VOICE_PITCH)) == 0x1234, "an 8-bit write to an odd address is ignored", "register is " + hex(spu.read16(voiceRegister(0, VOICE_PITCH)), 4) + " after writing ABh to the high byte of 1234h");
	}
	{
		SPU spu;
		spu.write16(voiceRegister(0, VOICE_PITCH), 0x1234);
		spu.write8(voiceRegister(0, VOICE_PITCH), 0xCD);
		check((spu.read16(voiceRegister(0, VOICE_PITCH)) & 0xFF) == 0xCD, "an 8-bit write to an even address is a 16-bit write: its low byte is the byte written", "register is " + hex(spu.read16(voiceRegister(0, VOICE_PITCH)), 4));
	}
}

// Registers that are only storage
void checkGeneralPurposeRegisters() {
	SPU spu;
	struct Case {
		uint32_t address;
		uint16_t value;
		const char* name;
	};
	const Case cases[] = {
		{0x1F801DA0, 0x1357, "1F801DA0h"},
		{0x1F801DBC, 0x2468, "1F801DBCh"},
		{0x1F801DBE, 0x369C, "1F801DBEh (the second half of the 4 bytes at 1F801DBCh)"},
		{0x1F801E60, 0x1234, "1F801E60h (the first of the 32 bytes)"},
		{0x1F801E7E, 0x5678, "1F801E7Eh (the last of the 32 bytes)"},
	};
	for (const Case& testCase : cases) {
		spu.write16(testCase.address, testCase.value);
		uint16_t read = spu.read16(testCase.address);
		check(read == testCase.value, std::string("the \"unused\" register ") + testCase.name + " holds what is written", "wrote " + hex(testCase.value, 4) + ", read " + hex(read, 4));
	}
}

// 32-bit accesses are two 16-bit accesses
void checkThirtyTwoBitAccess() {
	SPU spu;
	spu.write32(voiceRegister(2, VOICE_VOLUME_LEFT), 0x56781234);
	check(spu.read16(voiceRegister(2, VOICE_VOLUME_LEFT)) == 0x1234 && spu.read16(voiceRegister(2, VOICE_VOLUME_RIGHT)) == 0x5678, "a 32-bit write is two halfwords, the low one at the lower address");
	checkEqual(spu.read32(voiceRegister(2, VOICE_VOLUME_LEFT)), 0x56781234, "a 32-bit read returns the two halfwords, the low one at the lower address");
}

// STATX bits 5-0 follow ATTR bits 5-0
void checkStatusMirrorsControl() {
	for (uint16_t mode : {0x0000, 0x0001, 0x0020, 0x0021, 0x0023, 0x0003}) {
		SPU spu;
		spu.write16(CONTROL, static_cast<uint16_t>(CONTROL_ON | mode));
		runSamples(spu, 2);
		uint16_t status = spu.read16(STATUS);
		check((status & 0x3F) == (mode & 0x3F), "STATX bits 5-0 show ATTR bits 5-0 (ATTR low bits " + hex(mode, 2) + ")", "STATX is " + hex(status, 4));
	}
}

} // namespace

int main() {
	checkVoiceRegisters();
	checkCurrentVolume();
	checkVolumeSweep();
	checkEightBitAccess();
	checkGeneralPurposeRegisters();
	checkThirtyTwoBitAccess();
	checkStatusMirrorsControl();
	return echo_check::finish();
}
