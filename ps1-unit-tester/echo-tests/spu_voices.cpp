// The SPU voices as a game hears them: the ADPCM loop flags and ENDX, how fast a block is played at each PITCH, the ADSR envelope read back through
// ENVX, the mix (voice and main volumes, SPU enable / mute), the interpolation between samples, CD audio, the capture buffers, and the interrupt a voice
// raises by reading the IRQ address. Register behaviour is in spu_registers and sound RAM transfers in spu_transfers. Needs no BIOS and no other
// device: sound RAM is filled with the documented manual-write procedure and time passes only when a test says so (768 cycles per sample).
//
// What is checked is what the psx-spx page "Sound Processing Unit (SPU)" documents:
//   - ADPCM header flags: bit 0 loop end (set ENDX and jump to the repeat address), bit 1 repeat (0 = force release and set the envelope to zero, only
//     with bit 0), bit 2 loop start (copy the current address to the repeat address). Code 2 behaves as code 0. ENDX bits are cleared by a key on.
//   - PITCH: 1000h = 44100 Hz (28 ADPCM samples per block per 28 output samples), 0 = stop, 4000h = fastest, anything above is clipped to 4000h.
//   - Envelope: a step is 7 - step (attack, increase), 8 - step (decrease), scaled by 2 ^ (11 - shift) below shift 11, or taken once per
//     2 ^ (shift - 11) samples above it. Exponential increase above 6000h takes a quarter of the step for shifts below 10. Decay is exponential, ends
//     at the sustain level (N + 1) * 800h. Key off starts the release; key on starts the envelope from zero.
//   - 4-point Gaussian interpolation with a 512-entry table: out = (g[0FFh-i] * oldest + g[1FFh-i] * older + g[100h+i] * old + g[000h+i] * new) >> 15
//     where i is bits 4-11 of the pitch counter. At i = 0 the weights are 4807, 22963, 4871 and -1; at i = 80h they are 412, 15855, 15948 and 424.
//   - SPU enable (ATTR bit 15) and mute (bit 14) silence the voices but are "don't care for CD audio". CD audio capture is always active.
//   - The capture buffers are four 400h-byte buffers at 0000h (CD left), 0400h (CD right), 0800h (voice 1) and 0C00h (voice 3, after ADSR). STATX
//     bit 11 says whether the first or the second half of a buffer is being written (0 = first).
//   - All voices permanently read data from sound RAM, even after the release has finished, so an inaudible voice can trigger the SPU interrupt.
// Left out on purpose because this page does not define it, or the value depends on hardware timing: the ADPCM shift / filter arithmetic (the page says
// "same as CD-XA"), the exact sample at which ENDX or an envelope step shows up (only differences and ratios are compared), noise, pitch modulation,
// reverb, and the exact envelope after "hold" (a step value of all ones never steps, but how the level settles on the way there is not defined).
#include "spu/SPU.h"
#include "support/echo_check.h"
#include "support/spu_rig.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;
using namespace spu_rig;

namespace {

constexpr uint32_t BLOCKS_AT = 0x1000;

// Puts the ADPCM blocks one after the other in sound RAM and starts `voice` on the first one, with the SPU on and the main volume at maximum
void startVoice(SPU& spu, const std::vector<std::vector<uint8_t>>& blocks, int voice, uint16_t pitch, uint16_t adsr1, uint16_t adsr2, uint16_t volume = 0x3FFF) {
	std::vector<uint8_t> all;
	for (const auto& block : blocks) all.insert(all.end(), block.begin(), block.end());
	upload(spu, BLOCKS_AT, all);
	spu.write16(CONTROL, CONTROL_ON);
	spu.write16(MAIN_VOLUME_LEFT, 0x3FFF);
	spu.write16(MAIN_VOLUME_RIGHT, 0x3FFF);
	programVoice(spu, voice, BLOCKS_AT, pitch, adsr1, adsr2, volume, volume);
	keyOn(spu, voice);
}

std::string show(const std::vector<int>& values, size_t count) {
	std::string text;
	for (size_t index = 0; index < count && index < values.size(); index++) text += (index ? " " : "") + std::to_string(values[index]);
	return text;
}

std::vector<int> envelopeSequence(SPU& spu, int voice, int samples) {
	std::vector<int> values;
	for (int sample = 0; sample < samples; sample++) {
		runSamples(spu, 1);
		values.push_back(envelope(spu, voice));
	}
	return values;
}

// Samples until ENDX shows for the voice, or -1
int samplesUntilEnd(uint16_t pitch) {
	SPU spu;
	startVoice(spu, {adpcmBlockFilled(0, 0, 0x03, 7)}, 0, pitch, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD);
	for (int sample = 1; sample <= 500; sample++) {
		runSamples(spu, 1);
		if (endReached(spu, 0)) return sample;
	}
	return -1;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Loop flags and ENDX

void checkLoopFlags() {
	struct Case {
		uint8_t flags;
		const char* name;
		bool end;
	};
	const Case cases[] = {
		{0x00, "code 0 (normal): no end reached", false},
		{0x02, "code 2 (repeat without end) is ignored like code 0: no end reached", false},
		{0x01, "code 1 (end + mute): ENDX is set", true},
		{0x03, "code 3 (end + repeat): ENDX is set", true},
	};
	for (const Case& testCase : cases) {
		SPU spu;
		startVoice(spu, {adpcmBlockFilled(0, 0, testCase.flags, 7)}, 0, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD);
		runSamples(spu, 100);
		check(endReached(spu, 0) == testCase.end, std::string("loop flags, ") + testCase.name, "ENDX is " + hex(spu.read16(ENDX_LOW), 4));
	}
	{
		SPU spu;
		startVoice(spu, {adpcmBlockFilled(0, 0, 0x01, 7)}, 0, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD);
		runSamples(spu, 100);
		checkEqual(envelope(spu, 0), 0, "loop flags: code 1 (end + mute) sets the envelope to zero");
	}
	{
		SPU spu;
		startVoice(spu, {adpcmBlockFilled(0, 0, 0x03, 7)}, 0, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD);
		runSamples(spu, 100);
		check(envelope(spu, 0) > 0x7000, "loop flags: code 3 (end + repeat) keeps the voice sounding at full envelope", "ENVX is " + hex(envelope(spu, 0), 4));
	}
	{
		// A key on clears the voice's ENDX bit
		SPU spu;
		startVoice(spu, {adpcmBlockFilled(0, 0, 0x03, 7)}, 0, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD);
		runSamples(spu, 60);
		bool setBefore = endReached(spu, 0);
		keyOn(spu, 0);
		check(setBefore && !endReached(spu, 0), "ENDX: a key on clears the voice's bit", "set before the key on: " + std::string(setBefore ? "yes" : "no"));
	}
	{
		// Voice 20 is bit 4 of the second ENDX register
		SPU spu;
		startVoice(spu, {adpcmBlockFilled(0, 0, 0x03, 7)}, 20, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD);
		runSamples(spu, 60);
		check((spu.read16(ENDX_HIGH) & 0x0010) != 0 && (spu.read16(ENDX_LOW) & 0xFFFF) == 0, "ENDX: voice 20 shows as bit 4 of the second register and no voice of the first", "ENDX: " + hex(spu.read16(ENDX_LOW), 4) + " " + hex(spu.read16(ENDX_HIGH), 4));
	}
}

// Three blocks of different levels: the loop-start flag in the second block makes the jump at the end of the third land on the second
void checkLoopStartAndJump() {
	SPU spu;
	startVoice(spu, {adpcmBlockFilled(0, 0, 0x00, 2), adpcmBlockFilled(0, 0, 0x04, 6), adpcmBlockFilled(0, 0, 0x03, 4)}, 0, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD);
	runSamples(spu, 40);
	check(spu.read16(voiceRegister(0, VOICE_REPEAT_ADDRESS)) == (BLOCKS_AT + 16) / 8, "loop start: the repeat address register holds the address of the block with the loop-start flag", "LSAX is " + hex(spu.read16(voiceRegister(0, VOICE_REPEAT_ADDRESS)), 4));
	runSamples(spu, 140 - 40);
	std::vector<int16_t> left = drainLeft(spu);
	// Plateaus of the blocks (levels 2000h, 6000h, 4000h): the voice plays them once, then the second and third alternate
	const int centres[] = {14, 42, 70, 98, 126};
	const int expected[] = {0x2000, 0x6000, 0x4000, 0x6000, 0x4000};
	bool plateaus = left.size() >= 130;
	std::string details = "output has " + std::to_string(left.size()) + " samples";
	for (int index = 0; index < 5 && plateaus; index++) {
		int value = left[centres[index]];
		if (std::abs(value - expected[index]) > 700) {
			plateaus = false;
			details = "sample " + std::to_string(centres[index]) + " is " + std::to_string(value) + ", expected about " + std::to_string(expected[index]);
		}
	}
	check(plateaus, "loop jump: after the end flag the voice goes back to the loop-start block and never plays the first block again", details);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Pitch

void checkPitchTiming() {
	int normal = samplesUntilEnd(0x1000);
	check(normal >= 27 && normal <= 30, "pitch 1000h plays a 28-sample block in about 28 samples (44100 Hz)", "ENDX after " + std::to_string(normal) + " samples");
	struct Case {
		uint16_t pitch;
		int fewerSamples;
		const char* name;
	};
	// the difference to the time at 1000h does not depend on a constant latency
	const Case cases[] = {
		{0x2000, 14, "pitch 2000h is twice as fast (14 samples sooner than 1000h)"},
		{0x4000, 21, "pitch 4000h is four times as fast (21 samples sooner than 1000h)"},
		{0x8000, 21, "pitch 8000h is clipped to 4000h, four times as fast (21 samples sooner)"},
		{0xFFFF, 21, "pitch FFFFh is clipped to 4000h, four times as fast (21 samples sooner)"},
		{0x0800, -28, "pitch 0800h is half as fast (28 samples later than 1000h)"},
	};
	for (const Case& testCase : cases) {
		int time = samplesUntilEnd(testCase.pitch);
		int difference = normal - time;
		check(time > 0 && difference == testCase.fewerSamples, std::string("pitch: ") + testCase.name, "ENDX after " + std::to_string(time) + " samples against " + std::to_string(normal) + " at 1000h");
	}
	check(samplesUntilEnd(0x0000) == -1, "pitch 0 stops the voice: no block ever ends");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Envelope

bool everyStepIs(const std::vector<int>& values, int step, size_t from, size_t to) {
	for (size_t index = from; index + 1 < to && index + 1 < values.size(); index++) {
		if (values[index + 1] - values[index] != step) return false;
	}
	return true;
}

void checkEnvelope() {
	const std::vector<std::vector<uint8_t>> dc = {adpcmBlockFilled(0, 0, 0x03, 7)};
	for (int step = 0; step < 4; step++) {
		SPU spu;
		startVoice(spu, dc, 0, 0x1000, static_cast<uint16_t>((11 << 10) | (step << 8) | 0x00FF), ADSR2_HOLD);
		std::vector<int> values = envelopeSequence(spu, 0, 12);
		check(everyStepIs(values, 7 - step, 1, 12), "attack: linear, shift 11, step " + std::to_string(step) + " raises ENVX by " + std::to_string(7 - step) + " per sample", "ENVX: " + show(values, 12));
	}
	{
		SPU spu;
		startVoice(spu, dc, 0, 0x1000, static_cast<uint16_t>((9 << 10) | 0x00FF), ADSR2_HOLD);
		std::vector<int> values = envelopeSequence(spu, 0, 10);
		check(everyStepIs(values, 7 * 4, 1, 9), "attack: linear, shift 9 raises ENVX by 7 * 4 per sample", "ENVX: " + show(values, 10));
	}
	{
		SPU spu;
		startVoice(spu, dc, 0, 0x1000, static_cast<uint16_t>((12 << 10) | 0x00FF), ADSR2_HOLD);
		std::vector<int> values = envelopeSequence(spu, 0, 20);
		bool everyOther = true;
		for (size_t index = 2; index + 2 < values.size(); index++) everyOther = everyOther && (values[index + 2] - values[index] == 7);
		check(everyOther, "attack: linear, shift 12 raises ENVX by 7 once every two samples", "ENVX: " + show(values, 20));
	}
	{
		// Exponential increase: above 6000h each step is a quarter of the size (shift below 10). Shift 0 steps by 3800h, then 0E00h.
		SPU spu;
		startVoice(spu, dc, 0, 0x1000, 0x80FF, ADSR2_HOLD);
		std::vector<int> values = envelopeSequence(spu, 0, 8);
		int next = -1, first = -1;
		for (size_t index = 0; index + 1 < values.size(); index++) {
			if (values[index] > 0x6000) {
				first = values[index];
				next = values[index + 1];
				break;
			}
		}
		check(first > 0x6000 && next == first + 0x0E00, "attack: exponential, the step above 6000h is a quarter of the step below (3800h, then 0E00h)", "ENVX: " + show(values, 8));
	}
	{
		SPU spu;
		startVoice(spu, dc, 0, 0x1000, 0x00FF, ADSR2_HOLD);
		std::vector<int> values = envelopeSequence(spu, 0, 4);
		check(values[0] == 0x3800 && values[1] == 0x7000 && values[2] > 0x7F00, "attack: linear, shift 0 steps by 3800h and is not slowed above 6000h", "ENVX: " + show(values, 4));
	}
	{
		// Decay ends at the sustain level (N + 1) * 800h
		for (int sustain : {7, 3}) {
			SPU spu;
			startVoice(spu, dc, 0, 0x1000, static_cast<uint16_t>(sustain), ADSR2_HOLD);   // attack shift 0, decay shift 0, sustain level N
			std::vector<int> values = envelopeSequence(spu, 0, 30);
			int target = (sustain + 1) * 0x800;
			check(values.back() >= target - 0x300 && values.back() <= target + 0x100, "decay: the level stops at the sustain level " + hex(target, 4) + " (sustain level " + std::to_string(sustain) + ")", "ENVX: " + show(values, 30));
		}
	}
	{
		// Sustain with decrease, linear, shift 11, step 0: -8 per sample
		SPU spu;
		startVoice(spu, dc, 0, 0x1000, 0x00FF, static_cast<uint16_t>(0x4000 | (11 << 8)));
		std::vector<int> values = envelopeSequence(spu, 0, 24);
		check(everyStepIs(values, -8, 8, 24), "sustain: decrease, linear, shift 11, step 0 lowers ENVX by 8 per sample", "ENVX: " + show(values, 24));
	}
	{
		// Release, linear, shift 11: -8 per sample
		SPU spu;
		startVoice(spu, dc, 0, 0x1000, 0x00FF, static_cast<uint16_t>(ADSR2_HOLD | 11));
		runSamples(spu, 6);
		keyOff(spu, 0);
		std::vector<int> values = envelopeSequence(spu, 0, 14);
		check(everyStepIs(values, -8, 1, 14), "release: linear, shift 11 lowers ENVX by 8 per sample after a key off", "ENVX: " + show(values, 14));
	}
	{
		// Release, linear, shift 0: -4000h per sample, so the envelope is gone within a few samples, and stays gone
		SPU spu;
		startVoice(spu, dc, 0, 0x1000, 0x00FF, ADSR2_HOLD);
		runSamples(spu, 6);
		keyOff(spu, 0);
		std::vector<int> values = envelopeSequence(spu, 0, 12);
		bool gone = values[3] == 0;
		for (size_t index = 3; index < values.size(); index++) gone = gone && values[index] == 0;
		check(gone, "release: linear, shift 0 takes the envelope to zero within a few samples and keeps it there", "ENVX: " + show(values, 12));
	}
	{
		// A key on starts from zero
		SPU spu;
		startVoice(spu, dc, 0, 0x1000, 0x00FF, ADSR2_HOLD);
		runSamples(spu, 10);
		bool loud = envelope(spu, 0) > 0x7000;
		keyOn(spu, 0);
		check(loud && envelope(spu, 0) == 0, "key on: the envelope of a sounding voice goes back to zero and attacks again");
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// The mix

// The mean of the last 10 of 30 output samples of a voice playing a constant level (ADPCM nibble 4 = 4000h)
std::pair<double, double> meanOutput(SPU& spu) {
	runSamples(spu, 30);
	std::vector<int16_t> interleaved;
	spu.drainOutput(interleaved);
	double left = 0, right = 0;
	int count = 0;
	for (size_t index = interleaved.size() - 20; index + 1 < interleaved.size(); index += 2) {
		left += interleaved[index];
		right += interleaved[index + 1];
		count++;
	}
	return {left / count, right / count};
}

double level(uint16_t fixedVolume) { return static_cast<int16_t>(fixedVolume << 1) / 32768.0; }

void checkMixingLevels() {
	const std::vector<std::vector<uint8_t>> dc = {adpcmBlockFilled(0, 0, 0x03, 4)};
	const double sample = 0x4000;
	struct Case {
		uint16_t voiceLeft, voiceRight, mainLeft, mainRight;
		const char* name;
	};
	const Case cases[] = {
		{0x3FFF, 0x1FFF, 0x3FFF, 0x3FFF, "voice volumes 3FFFh and 1FFFh give full and half level"},
		{0x6000, 0x3FFF, 0x3FFF, 0x3FFF, "a negative voice volume (6000h) inverts the sound at half level"},
		{0x3FFF, 0x3FFF, 0x1FFF, 0x2800, "the main volumes scale the mix (1FFFh half, 2800h)"},
	};
	for (const Case& testCase : cases) {
		SPU spu;
		upload(spu, BLOCKS_AT, dc[0]);
		spu.write16(CONTROL, CONTROL_ON);
		spu.write16(MAIN_VOLUME_LEFT, testCase.mainLeft);
		spu.write16(MAIN_VOLUME_RIGHT, testCase.mainRight);
		programVoice(spu, 0, BLOCKS_AT, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD, testCase.voiceLeft, testCase.voiceRight);
		keyOn(spu, 0);
		auto [left, right] = meanOutput(spu);
		double expectedLeft = sample * level(testCase.voiceLeft) * level(testCase.mainLeft), expectedRight = sample * level(testCase.voiceRight) * level(testCase.mainRight);
		check(std::abs(left - expectedLeft) <= 0.02 * sample && std::abs(right - expectedRight) <= 0.02 * sample, std::string("mix: ") + testCase.name,
			"left " + std::to_string(static_cast<int>(left)) + " (expected about " + std::to_string(static_cast<int>(expectedLeft)) + "), right " + std::to_string(static_cast<int>(right)) + " (expected about " + std::to_string(static_cast<int>(expectedRight)) + ")");
	}
	{
		// Voice 23 sounds like voice 0, and two voices add
		SPU spu;
		upload(spu, BLOCKS_AT, adpcmBlockFilled(0, 0, 0x03, 2));
		spu.write16(CONTROL, CONTROL_ON);
		spu.write16(MAIN_VOLUME_LEFT, 0x3FFF);
		spu.write16(MAIN_VOLUME_RIGHT, 0x3FFF);
		programVoice(spu, 0, BLOCKS_AT, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD, 0x3FFF, 0x3FFF);
		programVoice(spu, 23, BLOCKS_AT, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD, 0x3FFF, 0x3FFF);
		keyOn(spu, 0);
		keyOn(spu, 23);
		auto [left, right] = meanOutput(spu);
		check(std::abs(left - 0x4000) <= 0.02 * 0x4000 && std::abs(right - 0x4000) <= 0.02 * 0x4000, "mix: voices 0 and 23 playing 2000h each add up to 4000h", "left " + std::to_string(static_cast<int>(left)) + ", right " + std::to_string(static_cast<int>(right)));
	}
	for (uint16_t control : {static_cast<uint16_t>(CONTROL_ENABLE), static_cast<uint16_t>(CONTROL_UNMUTE), static_cast<uint16_t>(0)}) {
		SPU spu;
		upload(spu, BLOCKS_AT, dc[0]);
		spu.write16(CONTROL, CONTROL_ON);
		spu.write16(MAIN_VOLUME_LEFT, 0x3FFF);
		spu.write16(MAIN_VOLUME_RIGHT, 0x3FFF);
		programVoice(spu, 0, BLOCKS_AT, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD, 0x3FFF, 0x3FFF);
		keyOn(spu, 0);
		spu.write16(CONTROL, control);
		auto [left, right] = meanOutput(spu);
		check(std::abs(left) < 50 && std::abs(right) < 50, "mix: with ATTR bit 15 (enable) or bit 14 (unmute) clear the voices are silent (ATTR " + hex(control, 4) + ")", "left " + std::to_string(static_cast<int>(left)));
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Interpolation

constexpr int GAUSS_FRACTION_0[4] = {4807, 22963, 4871, -1};          // oldest, older, old, new
constexpr int GAUSS_FRACTION_80[4] = {412, 15855, 15948, 424};

// The decoded samples of the impulse block repeated: a single sample of 7000h at index 10 of each 28
int impulseSample(int index) {
	if (index < 0) return 0;
	return (index % 28) == 10 ? 0x7000 : 0;
}

// What the documented interpolation gives for output n at a pitch of 1000h (fraction 0) or 0800h (fractions 0 and 80h alternating)
int gaussOutput(int n, int pitch) {
	long long counter = static_cast<long long>(n) * pitch;
	int index = static_cast<int>(counter >> 12);
	int fraction = static_cast<int>((counter >> 4) & 0xFF);
	const int* weights = fraction == 0 ? GAUSS_FRACTION_0 : GAUSS_FRACTION_80;
	long long sum = static_cast<long long>(weights[0]) * impulseSample(index - 3) + static_cast<long long>(weights[1]) * impulseSample(index - 2)
		+ static_cast<long long>(weights[2]) * impulseSample(index - 1) + static_cast<long long>(weights[3]) * impulseSample(index);
	return static_cast<int>(sum >> 15);
}

std::vector<int16_t> impulseOutput(uint16_t pitch, int samples) {
	SPU spu;
	uint8_t nibbles[28] = {};
	nibbles[10] = 7;
	startVoice(spu, {adpcmBlock(0, 0, 0x03, nibbles)}, 0, pitch, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD);
	runSamples(spu, samples);
	return drainLeft(spu);
}

// One sample of 7000h in a stream of zeros comes out spread over four consecutive outputs by the weights of the Gaussian table
void checkGaussianInterpolation() {
	const int amplitude = 0x7000;
	{
		std::vector<int16_t> out = impulseOutput(0x1000, 40);
		size_t peak = 0;
		for (size_t index = 0; index < out.size(); index++) if (std::abs(out[index]) > std::abs(out[peak])) peak = index;
		double peakRatio = static_cast<double>(out[peak]) / amplitude;
		check(peak >= 2 && std::abs(peakRatio - 22963.0 / 32768) <= 0.03, "interpolation: at fraction 0 a single sample peaks at the weight of the 'older' tap, 22963 / 32768 of its value",
			"peak " + std::to_string(out[peak]) + " (" + std::to_string(peakRatio) + " of the sample), expected about " + std::to_string(22963.0 / 32768));
		double before = peak >= 1 ? static_cast<double>(out[peak - 1]) / amplitude : 9, after = peak + 1 < out.size() ? static_cast<double>(out[peak + 1]) / amplitude : 9;
		check(std::abs(before - 4871.0 / 32768) <= 0.02 && std::abs(after - 4807.0 / 32768) <= 0.02, "interpolation: at fraction 0 the neighbours of the peak carry 4871 and 4807 / 32768 of the sample",
			"before the peak " + std::to_string(before) + ", after " + std::to_string(after) + " (expected about 0.149 and 0.147)");
	}
	{
		// Pitch 0800h alternates fractions 0 and 80h; compare a stretch around the peak with what the table gives
		std::vector<int16_t> out = impulseOutput(0x0800, 60);
		int expectedPeak = 0;
		std::vector<int> expected;
		for (int n = 0; n < 60; n++) expected.push_back(gaussOutput(n, 0x0800));
		for (int n = 0; n < 60; n++) if (expected[n] > expected[expectedPeak]) expectedPeak = n;
		size_t peak = 0;
		for (size_t index = 0; index < out.size(); index++) if (std::abs(out[index]) > std::abs(out[peak])) peak = index;
		bool matches = peak >= 6;
		std::string details = "peak at output " + std::to_string(peak);
		for (int offset = -6; offset <= 6 && matches; offset++) {
			int actual = out[peak + offset], wanted = expected[expectedPeak + offset];
			if (std::abs(actual - wanted) > 0.03 * amplitude) {
				matches = false;
				details = "at " + std::to_string(offset) + " from the peak the output is " + std::to_string(actual) + ", the table gives " + std::to_string(wanted);
			}
		}
		check(matches, "interpolation: at pitch 0800h (fractions 0 and 80h) the outputs around the peak follow the Gaussian table", details);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// CD audio and capture

void setCdSource(SPU& spu, int16_t left, int16_t right) {
	spu.setCDAudioSource([left, right](int16_t& l, int16_t& r) {
		l = left;
		r = right;
		return true;
	});
	spu.write16(CD_VOLUME_LEFT, 0x7FFF);
	spu.write16(CD_VOLUME_RIGHT, 0x7FFF);
	spu.write16(MAIN_VOLUME_LEFT, 0x3FFF);
	spu.write16(MAIN_VOLUME_RIGHT, 0x3FFF);
}

void checkCdAudio() {
	struct Case {
		uint16_t control;
		bool audible;
		const char* name;
	};
	const Case cases[] = {
		{static_cast<uint16_t>(CONTROL_ON | CONTROL_CD_INPUT), true, "SPU enabled and unmuted, CD input on: the CD audio is heard"},
		{static_cast<uint16_t>(CONTROL_ON), false, "CD input off (ATTR bit 0 clear): no CD audio"},
		{static_cast<uint16_t>(CONTROL_CD_INPUT), true, "SPU disabled and muted, CD input on: the CD audio is still heard (enable and mute are don't care for CD audio)"},
		{static_cast<uint16_t>(CONTROL_ENABLE | CONTROL_CD_INPUT), true, "SPU enabled but muted, CD input on: the CD audio is still heard"},
	};
	for (const Case& testCase : cases) {
		SPU spu;
		setCdSource(spu, 0x1234, 0x2345);
		spu.write16(CONTROL, testCase.control);
		runSamples(spu, 12);
		std::vector<int16_t> interleaved;
		spu.drainOutput(interleaved);
		int left = interleaved.size() >= 2 ? interleaved[interleaved.size() - 2] : 0, right = interleaved.size() >= 2 ? interleaved[interleaved.size() - 1] : 0;
		bool heard = std::abs(left - 0x1234) <= 0.02 * 0x1234 && std::abs(right - 0x2345) <= 0.02 * 0x2345;
		bool silent = std::abs(left) < 20 && std::abs(right) < 20;
		check(testCase.audible ? heard : silent, std::string("CD audio: ") + testCase.name, "left " + std::to_string(left) + ", right " + std::to_string(right) + " (CD samples 4660 and 9029)");
	}
	{
		SPU spu;
		setCdSource(spu, 0x4000, 0x4000);
		spu.write16(CD_VOLUME_LEFT, 0x4000);
		spu.write16(CD_VOLUME_RIGHT, 0x2000);
		spu.write16(CONTROL, CONTROL_ON | CONTROL_CD_INPUT);
		runSamples(spu, 12);
		std::vector<int16_t> interleaved;
		spu.drainOutput(interleaved);
		int left = interleaved[interleaved.size() - 2], right = interleaved[interleaved.size() - 1];
		check(std::abs(left - 0x2000) <= 0.02 * 0x4000 && std::abs(right - 0x1000) <= 0.02 * 0x4000, "CD audio: the CD volume registers scale the CD audio (4000h is half, 2000h a quarter)", "left " + std::to_string(left) + ", right " + std::to_string(right));
	}
}

std::vector<int> wordsAt(SPU& spu, uint32_t byteAddress, int count) {
	std::vector<uint8_t> bytes = download(spu, byteAddress, static_cast<size_t>(count) * 2);
	std::vector<int> words;
	for (int index = 0; index < count; index++) words.push_back(static_cast<int16_t>(bytes[index * 2] | (bytes[index * 2 + 1] << 8)));
	return words;
}

bool allWithin(const std::vector<int>& words, int expected, int tolerance, size_t from, size_t to) {
	for (size_t index = from; index < to && index < words.size(); index++) if (std::abs(words[index] - expected) > tolerance) return false;
	return true;
}

void checkCaptureBuffers() {
	{
		SPU spu;
		setCdSource(spu, 0x1234, 0x2345);
		spu.write16(RAM_CONTROL, 0x0004);
		spu.write16(CONTROL, CONTROL_ON | CONTROL_CD_INPUT);
		runSamples(spu, 300);
		std::vector<int> left = wordsAt(spu, 0x0000, 8), right = wordsAt(spu, 0x0400, 8);
		check(allWithin(left, 0x1234, 0, 0, 8), "capture: CD left is written at 0000h", "words at 0000h: " + show(left, 8));
		check(allWithin(right, 0x2345, 0, 0, 8), "capture: CD right is written at 0400h, the second 1 KB buffer", "words at 0400h: " + show(right, 8));
		std::vector<int> leftSecondHalf = wordsAt(spu, 0x0200, 4);
		check(allWithin(leftSecondHalf, 0x1234, 0, 0, 4), "capture: the CD left buffer is 400h bytes long, so after 256 samples it is still being written at 0200h", "words at 0200h: " + show(leftSecondHalf, 4));
	}
	{
		// CD capture is always active, whatever ATTR says about the CD audio output
		SPU spu;
		setCdSource(spu, 0x1234, 0x2345);
		spu.write16(CONTROL, CONTROL_ON);
		runSamples(spu, 20);
		std::vector<int> left = wordsAt(spu, 0x0000, 8);
		check(allWithin(left, 0x1234, 0, 0, 8), "capture: CD audio is captured even with the CD input off in ATTR", "words at 0000h: " + show(left, 8));
	}
	{
		// Voices 1 and 3 are captured after the ADSR, at 0800h and 0C00h
		SPU spu;
		upload(spu, BLOCKS_AT, adpcmBlockFilled(0, 0, 0x03, 4));
		spu.write16(CONTROL, CONTROL_ON);
		for (int voice : {1, 3}) programVoice(spu, voice, BLOCKS_AT, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD, 0x3FFF, 0x3FFF);
		keyOn(spu, 1);
		keyOn(spu, 3);
		runSamples(spu, 60);
		std::vector<int> voice1 = wordsAt(spu, 0x0800, 40), voice3 = wordsAt(spu, 0x0C00, 40);
		check(allWithin(voice1, 0x4000, 0x400, 20, 40), "capture: voice 1 after the ADSR is written at 0800h", "words at 0800h from the 20th: " + show(std::vector<int>(voice1.begin() + 20, voice1.begin() + 28), 8));
		check(allWithin(voice3, 0x4000, 0x400, 20, 40), "capture: voice 3 after the ADSR is written at 0C00h", "words at 0C00h from the 20th: " + show(std::vector<int>(voice3.begin() + 20, voice3.begin() + 28), 8));
	}
	{
		// STATX bit 11: first or second half of the buffers being written. Each buffer is 200h samples; the halves are 100h samples.
		SPU spu;
		spu.write16(RAM_CONTROL, 0x0004);
		spu.write16(CONTROL, CONTROL_ON);
		auto flagAt = [&spu](int samples, int from) {
			runSamples(spu, samples - from);
			return (spu.read16(STATUS) & STATUS_CAPTURE_HALF) != 0;
		};
		bool at200 = flagAt(200, 0), at300 = flagAt(300, 200), at600 = flagAt(600, 300);
		check(!at200 && at300 && !at600, "STATX bit 11: 0 while the first half of the buffers is written (samples 0-255), 1 for the second half (256-511), 0 again after the wrap",
			std::string("bit 11 after 200, 300 and 600 samples: ") + (at200 ? "1" : "0") + (at300 ? "1" : "0") + (at600 ? "1" : "0") + " (expected 010)");
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// The interrupt a voice raises by reading the IRQ address

void checkVoiceInterrupt() {
	{
		SPU spu;
		std::vector<uint16_t> raised;
		spu.setInterruptCallback([&raised](uint16_t bit) { raised.push_back(bit); });
		upload(spu, BLOCKS_AT, adpcmBlockFilled(0, 0, 0x03, 4));
		spu.write16(IRQ_ADDRESS, BLOCKS_AT / 8);
		spu.write16(CONTROL, CONTROL_ON | CONTROL_IRQ_ENABLE);
		programVoice(spu, 0, BLOCKS_AT, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD, 0x3FFF, 0x3FFF);
		keyOn(spu, 0);
		runSamples(spu, 40);
		check(!raised.empty() && (spu.read16(STATUS) & STATUS_IRQ) != 0, "voice IRQ: a voice reading the block at the IRQ address raises IRQ9 and sets STATX bit 6");
	}
	{
		SPU spu;
		std::vector<uint16_t> raised;
		spu.setInterruptCallback([&raised](uint16_t bit) { raised.push_back(bit); });
		upload(spu, BLOCKS_AT, adpcmBlockFilled(0, 0, 0x03, 4));
		spu.write16(IRQ_ADDRESS, 0x3000 / 8);
		spu.write16(CONTROL, CONTROL_ON | CONTROL_IRQ_ENABLE);
		programVoice(spu, 0, BLOCKS_AT, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD, 0x3FFF, 0x3FFF);
		keyOn(spu, 0);
		runSamples(spu, 100);
		check(raised.empty(), "voice IRQ: a voice that never reads the IRQ address raises nothing");
	}
	{
		// A one-shot block (loop start + end + mute) is muted after one play but the voice keeps reading it: every pass over the IRQ address is a new IRQ
		SPU spu;
		std::vector<uint16_t> raised;
		spu.setInterruptCallback([&raised](uint16_t bit) { raised.push_back(bit); });
		upload(spu, BLOCKS_AT, adpcmBlockFilled(0, 0, 0x05, 4));
		spu.write16(IRQ_ADDRESS, BLOCKS_AT / 8);
		spu.write16(CONTROL, CONTROL_ON | CONTROL_IRQ_ENABLE);
		programVoice(spu, 0, BLOCKS_AT, 0x1000, ADSR1_FASTEST_ATTACK_NO_DECAY, ADSR2_HOLD, 0x3FFF, 0x3FFF);
		keyOn(spu, 0);
		runSamples(spu, 40);
		bool first = !raised.empty();
		spu.write16(CONTROL, CONTROL_ON);                          // acknowledge
		spu.write16(CONTROL, CONTROL_ON | CONTROL_IRQ_ENABLE);     // and arm again
		raised.clear();
		runSamples(spu, 100);
		check(first && !raised.empty(), "voice IRQ: a voice that has finished (muted) keeps reading its block, so it raises the IRQ again on every pass", "first IRQ: " + std::string(first ? "yes" : "no") + ", IRQs raised in the next 100 samples: " + std::to_string(raised.size()));
	}
}

} // namespace

int main() {
	checkLoopFlags();
	checkLoopStartAndJump();
	checkPitchTiming();
	checkEnvelope();
	checkMixingLevels();
	checkGaussianInterpolation();
	checkCdAudio();
	checkCaptureBuffers();
	checkVoiceInterrupt();
	return echo_check::finish();
}
