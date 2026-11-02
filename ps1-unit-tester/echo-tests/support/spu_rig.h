// Helpers for the SPU tests. The SPU is driven only through its register interface, the way a game drives it, following the procedures the psx-spx
// page "Sound Processing Unit (SPU)" describes: sound RAM is filled with the manual-write procedure and read back with the DMA-read procedure (the
// only way to read it), and a voice is a block of registers at 1F801C00h + 10h * voice. Time passes only when a test says so: one audio sample is
// 768 CPU cycles (33.8688 MHz / 44.1 kHz, the page's own figure).
#pragma once
#include "spu/SPU.h"
#include <cstdint>
#include <vector>

namespace spu_rig {

constexpr int CYCLES_PER_SAMPLE = 768;

constexpr uint32_t voiceRegister(int voice, uint32_t offset) { return 0x1F801C00 + static_cast<uint32_t>(voice) * 0x10 + offset; }
constexpr uint32_t VOICE_VOLUME_LEFT = 0x0;
constexpr uint32_t VOICE_VOLUME_RIGHT = 0x2;
constexpr uint32_t VOICE_PITCH = 0x4;
constexpr uint32_t VOICE_START_ADDRESS = 0x6;
constexpr uint32_t VOICE_ADSR1 = 0x8;
constexpr uint32_t VOICE_ADSR2 = 0xA;
constexpr uint32_t VOICE_ENVELOPE = 0xC;          // ENVX, current envelope volume (read)
constexpr uint32_t VOICE_REPEAT_ADDRESS = 0xE;    // LSAX
constexpr uint32_t currentVolumeRegister(int voice, bool right) { return 0x1F801E00 + static_cast<uint32_t>(voice) * 4 + (right ? 2u : 0u); }

constexpr uint32_t MAIN_VOLUME_LEFT = 0x1F801D80;
constexpr uint32_t MAIN_VOLUME_RIGHT = 0x1F801D82;
constexpr uint32_t KEY_ON_LOW = 0x1F801D88;
constexpr uint32_t KEY_ON_HIGH = 0x1F801D8A;
constexpr uint32_t KEY_OFF_LOW = 0x1F801D8C;
constexpr uint32_t KEY_OFF_HIGH = 0x1F801D8E;
constexpr uint32_t ENDX_LOW = 0x1F801D9C;
constexpr uint32_t ENDX_HIGH = 0x1F801D9E;
constexpr uint32_t IRQ_ADDRESS = 0x1F801DA4;
constexpr uint32_t TRANSFER_ADDRESS = 0x1F801DA6;
constexpr uint32_t TRANSFER_FIFO = 0x1F801DA8;
constexpr uint32_t CONTROL = 0x1F801DAA;           // ATTR / SPUCNT
constexpr uint32_t RAM_CONTROL = 0x1F801DAC;
constexpr uint32_t STATUS = 0x1F801DAE;            // STATX
constexpr uint32_t CD_VOLUME_LEFT = 0x1F801DB0;
constexpr uint32_t CD_VOLUME_RIGHT = 0x1F801DB2;
constexpr uint32_t MAIN_CURRENT_LEFT = 0x1F801DB8;
constexpr uint32_t MAIN_CURRENT_RIGHT = 0x1F801DBA;

constexpr uint16_t CONTROL_ENABLE = 0x8000;
constexpr uint16_t CONTROL_UNMUTE = 0x4000;
constexpr uint16_t CONTROL_IRQ_ENABLE = 0x0040;
constexpr uint16_t CONTROL_CD_INPUT = 0x0001;
constexpr uint16_t CONTROL_ON = CONTROL_ENABLE | CONTROL_UNMUTE;
constexpr uint16_t MODE_STOP = 0x00;
constexpr uint16_t MODE_MANUAL_WRITE = 0x10;
constexpr uint16_t MODE_DMA_WRITE = 0x20;
constexpr uint16_t MODE_DMA_READ = 0x30;

constexpr uint16_t STATUS_IRQ = 0x0040;
constexpr uint16_t STATUS_DMA_REQUEST = 0x0080;
constexpr uint16_t STATUS_DMA_WRITE_REQUEST = 0x0100;
constexpr uint16_t STATUS_DMA_READ_REQUEST = 0x0200;
constexpr uint16_t STATUS_BUSY = 0x0400;
constexpr uint16_t STATUS_CAPTURE_HALF = 0x0800;

// ADSR registers. ADSR1: bit 15 attack mode (1 = exponential), 14-10 attack shift, 9-8 attack step, 7-4 decay shift, 3-0 sustain level.
// ADSR2: bit 15 sustain mode, 14 sustain direction (1 = decrease), 12-8 sustain shift, 7-6 sustain step, 5 release mode, 4-0 release shift.
// "Hold" is the sustain setting of a step value of all ones, which never steps: a voice that reaches 7FFFh stays there.
constexpr uint16_t ADSR1_FASTEST_ATTACK_NO_DECAY = 0x00FF;   // linear attack with shift 0 (3800h per sample), decay shift Fh, sustain level Fh
constexpr uint16_t ADSR2_HOLD = 0x1FC0;                      // sustain: increase, shift 1Fh, step 3 (all ones); release: linear, shift 0 (add the shift)

inline void runCycles(SPU& spu, int cycles) {
	for (int done = 0; done < cycles; done += 16) spu.advanceClock(16);
}

// Runs `samples` audio samples, one at a time
inline void runSamples(SPU& spu, int samples) {
	for (int sample = 0; sample < samples; sample++) spu.advanceClock(CYCLES_PER_SAMPLE);
}

// A voice's register block and a block of ADPCM
inline void programVoice(SPU& spu, int voice, uint32_t startByteAddress, uint16_t pitch, uint16_t adsr1, uint16_t adsr2, uint16_t volumeLeft, uint16_t volumeRight) {
	spu.write16(voiceRegister(voice, VOICE_VOLUME_LEFT), volumeLeft);
	spu.write16(voiceRegister(voice, VOICE_VOLUME_RIGHT), volumeRight);
	spu.write16(voiceRegister(voice, VOICE_PITCH), pitch);
	spu.write16(voiceRegister(voice, VOICE_START_ADDRESS), static_cast<uint16_t>(startByteAddress / 8));
	spu.write16(voiceRegister(voice, VOICE_ADSR1), adsr1);
	spu.write16(voiceRegister(voice, VOICE_ADSR2), adsr2);
}

inline void keyOn(SPU& spu, int voice) {
	if (voice < 16) spu.write16(KEY_ON_LOW, static_cast<uint16_t>(1u << voice));
	else spu.write16(KEY_ON_HIGH, static_cast<uint16_t>(1u << (voice - 16)));
}

inline void keyOff(SPU& spu, int voice) {
	if (voice < 16) spu.write16(KEY_OFF_LOW, static_cast<uint16_t>(1u << voice));
	else spu.write16(KEY_OFF_HIGH, static_cast<uint16_t>(1u << (voice - 16)));
}

inline bool endReached(SPU& spu, int voice) {
	uint16_t bits = voice < 16 ? spu.read16(ENDX_LOW) : spu.read16(ENDX_HIGH);
	return (bits & (1u << (voice % 16))) != 0;
}

inline uint16_t envelope(SPU& spu, int voice) { return spu.read16(voiceRegister(voice, VOICE_ENVELOPE)); }

// One 16-byte ADPCM block: header (shift 0-12, filter 0-4), flags (bit 0 loop end, 1 repeat, 2 loop start) and 28 four-bit samples
inline std::vector<uint8_t> adpcmBlock(uint8_t shift, uint8_t filter, uint8_t flags, const uint8_t (&nibbles)[28]) {
	std::vector<uint8_t> block(16, 0);
	block[0] = static_cast<uint8_t>((filter << 4) | shift);
	block[1] = flags;
	for (int index = 0; index < 28; index++) block[2 + index / 2] |= static_cast<uint8_t>((nibbles[index] & 0xF) << (4 * (index & 1)));
	return block;
}

inline std::vector<uint8_t> adpcmBlockFilled(uint8_t shift, uint8_t filter, uint8_t flags, uint8_t nibble) {
	uint8_t nibbles[28];
	for (uint8_t& value : nibbles) value = nibble;
	return adpcmBlock(shift, filter, flags, nibbles);
}

// The documented manual-write procedure: stop, set the transfer address, put up to 32 halfwords in the FIFO, select manual write, wait until the
// transfer is no longer busy. Repeated for each 32 halfwords.
inline void upload(SPU& spu, uint32_t byteAddress, std::vector<uint8_t> bytes, uint16_t controlHighBits = CONTROL_ON) {
	if (bytes.size() % 2) bytes.push_back(0);
	for (size_t offset = 0; offset < bytes.size(); offset += 64) {
		spu.write16(CONTROL, controlHighBits | MODE_STOP);
		spu.write16(TRANSFER_ADDRESS, static_cast<uint16_t>((byteAddress + offset) / 8));
		size_t end = offset + 64 < bytes.size() ? offset + 64 : bytes.size();
		for (size_t index = offset; index < end; index += 2) {
			spu.write16(TRANSFER_FIFO, static_cast<uint16_t>(bytes[index] | (bytes[index + 1] << 8)));
		}
		spu.write16(CONTROL, controlHighBits | MODE_MANUAL_WRITE);
		for (int wait = 0; wait < 4000 && (spu.read16(STATUS) & STATUS_BUSY); wait++) spu.advanceClock(16);
	}
	spu.write16(CONTROL, controlHighBits | MODE_STOP);
}

// The documented DMA-read procedure, with the DMA controller's part played by the test: select DMA read, wait for the SPU to ask for data (it
// first collects it in its FIFO), take a block of 16 words, repeat.
inline std::vector<uint8_t> download(SPU& spu, uint32_t byteAddress, size_t byteCount, uint16_t controlHighBits = CONTROL_ON) {
	std::vector<uint8_t> bytes;
	spu.write16(CONTROL, controlHighBits | MODE_STOP);
	spu.write16(TRANSFER_ADDRESS, static_cast<uint16_t>(byteAddress / 8));
	spu.write16(CONTROL, controlHighBits | MODE_DMA_READ);
	while (bytes.size() < byteCount) {
		for (int wait = 0; wait < 4000 && !(spu.read16(STATUS) & STATUS_DMA_READ_REQUEST); wait++) spu.advanceClock(16);
		for (int word = 0; word < 16; word++) {
			uint32_t value = spu.readDMAWord();
			for (int byte = 0; byte < 4; byte++) bytes.push_back(static_cast<uint8_t>(value >> (8 * byte)));
		}
	}
	spu.write16(CONTROL, controlHighBits | MODE_STOP);
	bytes.resize(byteCount);
	return bytes;
}

// The left channel of what the SPU has output so far (the output is interleaved left, right)
inline std::vector<int16_t> drainLeft(SPU& spu) {
	std::vector<int16_t> interleaved;
	spu.drainOutput(interleaved);
	std::vector<int16_t> left;
	for (size_t index = 0; index + 1 < interleaved.size(); index += 2) left.push_back(interleaved[index]);
	return left;
}

} // namespace spu_rig
