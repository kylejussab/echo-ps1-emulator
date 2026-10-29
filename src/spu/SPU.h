#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>


/// @brief Sound Processing Unit (SPU) handling 24-voice ADPCM playback, CD audio, and reverb.
class SPU {
public:
	SPU();

	/// @brief Reads an 8-bit value from the SPU register space. Route 0x1F801C00-0x1F801FFF here.
	/// @param address The 32-bit memory address to read.
	/// @return The 8-bit value read.
	uint8_t read8(uint32_t address);


	/// @brief Writes an 8-bit value to the SPU register space (mirrored into a 16-bit halfword).
	/// @param address The 32-bit memory address to write to.
	/// @param value The 8-bit value to write.
	void write8(uint32_t address, uint8_t value);


	/// @brief Reads a 16-bit value from the SPU register space.
	/// @param address The 32-bit memory address to read.
	/// @return The 16-bit value read.
	uint16_t read16(uint32_t address);


	/// @brief Writes a 16-bit value to the SPU register space.
	/// @param address The 32-bit memory address to write to.
	/// @param value The 16-bit value to write.
	void write16(uint32_t address, uint16_t value);


	/// @brief Reads a 32-bit value from the SPU register space.
	/// @param address The 32-bit memory address to read.
	/// @return The 32-bit value read.
	uint32_t read32(uint32_t address);


	/// @brief Writes a 32-bit value to the SPU register space.
	/// @param address The 32-bit memory address to write to.
	/// @param value The 32-bit value to write.
	void write32(uint32_t address, uint32_t value);


	/// @brief Reads a 32-bit word via DMA.
	/// @return The 32-bit data word transferred to memory.
	uint32_t readDMAWord();


	/// @brief Writes a 32-bit word via DMA.
	/// @param word The 32-bit data word transferred from memory.
	void writeDMAWord(uint32_t word);
	

	/// @brief Checks the DREQ line status of DMA channel 4 (mirrors SPUSTAT bits 8/9).
	/// @return True if the SPU is requesting a DMA transfer block, false otherwise.
	bool DMARequest() const;


	/// @brief Advances the SPU clock, driving RAM transfers and sample generation.
	/// @param cycles The number of CPU cycles elapsed.
	void advanceClock(int cycles);


	/// @brief Swaps the internal output buffer into the provided destination and clears it.
	/// @param destination The vector to receive the interleaved 16-bit 44.1 kHz stereo samples.
	void drainOutput(std::vector<int16_t>& destination);

	
	/// @brief Checks if the SPU is currently transferring data to sound RAM (SPUSTAT bit 10).
	/// @return True if a transfer is busy, false otherwise.
	bool isTransferBusy() const { return isWriteMode(transferMode()) && fifoCount > 0; }


	/// @brief Calculates DMA channel 4 cycle duration per word based on SPU transfer rate.
	/// @return The number of CPU cycles required per word transfer.
	static constexpr int DMACyclesPerWord() { return static_cast<int>(2 * TRANSFER_CYCLES_PER_HALFWORD); }


	using InterruptCallback = std::function<void(uint16_t)>;

	/// @brief Sets the callback triggered when the SPU raises an interrupt.
	/// @param callback The function to call with the interrupt status bit.
	void setInterruptCallback(InterruptCallback callback) { raiseInterrupt = std::move(callback); }


	using CDAudioSource = std::function<bool(int16_t& left, int16_t& right)>;

	/// @brief Sets the CD audio source (CD-DA and decoded XA-ADPCM).
	/// @param source The function that fills left/right samples and returns true if ready.
	void setCDAudioSource(CDAudioSource source) { cdAudioSource = std::move(source); }

private:
	enum TransferMode : uint32_t { TRANSFER_STOP = 0, TRANSFER_MANUAL_WRITE = 1, TRANSFER_DMA_WRITE = 2, TRANSFER_DMA_READ = 3 };

	enum class ADSRPhase { Off, Attack, Decay, Sustain, Release };

	struct VolumeSweep {
		uint16_t reg = 0;
		int32_t level = 0;
		int32_t counter = 0;

		void write(uint16_t value);
		void step();
	};

	struct Voice {
		uint32_t currentAddress = 0; // Sound RAM address (in bytes) of the ADPCM block being played.
		uint32_t repeatAddress = 0; // Loop point address (in bytes).
		uint32_t pitchCounter = 0; // Fixed-point: bits 12+ = sample index (0-27), low 12 bits = fraction.
		int16_t samples[31] = {}; // ADPCM window: [0..2] = last 3 of previous block, [3..30] = current.
		int32_t adpcmOld = 0;
		int32_t adpcmOlder = 0;
		uint8_t blockFlags = 0;

		ADSRPhase phase = ADSRPhase::Off;
		int32_t adsrLevel = 0; // Current envelope volume level (0 to 0x7FFF).
		int32_t adsrCounter = 0;

		VolumeSweep volumeLeft;
		VolumeSweep volumeRight;

		int16_t lastOutput = 0; // Post-envelope, pre-volume sample (used for pitch mod and capture).
	};


	static constexpr int32_t ADPCM_FILTER_0[5] = { 0, 60, 115, 98, 122 };
	static constexpr int32_t ADPCM_FILTER_1[5] = { 0, 0, -52, -55, -60 };

	static constexpr int32_t REVERB_FIR[39] = {
		-1, 0, 2, 0, -10, 0, 35, 0, -103, 0, 266, 0, -616, 0, 1332, 0, -2960, 0, 10246,
		16384,
		10246, 0, -2960, 0, 1332, 0, -616, 0, 266, 0, -103, 0, 35, 0, -10, 0, 2, 0, -1
	};

	static constexpr int32_t REVERB_FIR_EVEN[20] = {
		-1, 2, -10, 35, -103, 266, -616, 1332, -2960, 10246,
		10246, -2960, 1332, -616, 266, -103, 35, -10, 2, -1
	};

	static constexpr uint32_t REGISTER_COUNT = 0x100; // 512 bytes of 16-bit registers (0x1F801C00 - 0x1F801DFF).
	static constexpr uint32_t SOUND_RAM_SIZE = 512 * 1024;
	static constexpr uint32_t VOICE_COUNT = 24;
	static constexpr uint32_t CYCLES_PER_SAMPLE = 768; // CPU cycles per audio sample (33.8688 MHz / 44100 Hz).

	static constexpr uint32_t FIFO_SIZE = 32; // Holds 32 halfwords (one DMA4 block / 16 words).
	static constexpr uint32_t DMA_REQUEST_THRESHOLD = 16;
	static constexpr uint32_t TRANSFER_CYCLES_PER_HALFWORD = 16; // Hardware throughput approximation.

	static constexpr size_t MAX_BUFFERED_SAMPLES = 44100 * 2; // Caps undrained output buffer at ~1 second of stereo.

	
	std::vector<uint8_t> soundRam;
	uint16_t registers[REGISTER_COUNT] = {};
	uint32_t transferAddress = 0; // In bytes (hardware register uses 8-byte units).


	Voice voices[VOICE_COUNT];
	VolumeSweep mainVolumeLeft;
	VolumeSweep mainVolumeRight;
	uint32_t endx = 0; // Bit 'n' set = voice 'n' reached the end of its sample.

	uint16_t noiseLevel = 1; // Approximate noise generator state.
	int32_t noiseTimer = 0;

	std::vector<int16_t> outputSamples;


	InterruptCallback raiseInterrupt;
	uint32_t irqAddress = 0; // In bytes. Triggers IRQ9 if read/written while SPUCNT bit 15 & 6 are set.
	bool irqFlag = false; // SPUSTAT bit 6.
	uint32_t cycleAccumulator = 0;
	uint32_t captureOffset = 0; // Byte offset inside each 0x200-byte capture buffer (advances 2 bytes/sample).


	uint16_t transferFifo[FIFO_SIZE] = {};
	uint32_t fifoHead = 0;
	uint32_t fifoCount = 0;
	bool transferActive = false; // Skips transfer logic in advanceClock() when false.
	uint32_t transferCycleAccumulator = 0;


	CDAudioSource cdAudioSource;
	int16_t cdInputLeft = 0; // Raw CD sample before SPU volume (captured by capture buffers).
	int16_t cdInputRight = 0;


	uint32_t reverbBaseAddress = 0; // mBASE in bytes.
	uint32_t reverbCurrentAddress = 0; // Moves through work area one halfword per reverb step.
	bool reverbOddSample = false;
	uint32_t reverbInputPosition = 0;

	int32_t reverbInputLeft[64] = {};
	int32_t reverbInputRight[64] = {};
	int32_t reverbOutputHistoryLeft[20] = {};
	int32_t reverbOutputHistoryRight[20] = {};

	int32_t reverbPendingLeft = 0; // Second output sample of the pair, produced one sample later.
	int32_t reverbPendingRight = 0;



	/// @brief Processes pending audio samples based on the cycle accumulator.
	void runSamples();


	/// @brief Generates a single 44.1 kHz stereo audio sample from all active voices, CD audio, and reverb.
	void generateSample();


	/// @brief Triggers the ADSR attack phase for a specific voice.
	/// @param voiceIndex The hardware index (0-23) of the voice to start.
	void keyOn(uint32_t voiceIndex);


	/// @brief Triggers the ADSR release phase for a specific voice.
	/// @param voiceIndex The hardware index (0-23) of the voice to release.
	void keyOff(uint32_t voiceIndex);

	
	/// @brief Decodes a 16-byte ADPCM block for a voice into 28 PCM samples.
	/// @param voice The voice state machine to update.
	void decodeBlock(Voice& voice);


	/// @brief Handles voice state transitions (looping or stopping) when an ADPCM block ends.
	/// @param voiceIndex The hardware index (0-23) of the voice.
	void finishBlock(uint32_t voiceIndex);


	/// @brief Updates the ADSR envelope state for a specific voice.
	/// @param voice The voice state machine to update.
	/// @param voiceIndex The hardware index (0-23) of the voice.
	void updateEnvelope(Voice& voice, uint32_t voiceIndex);


	/// @brief Steps the internal noise generator for voices utilizing noise playback.
	void updateNoise();


	/// @brief Checks if a Sound RAM access triggers the SPU IRQ (IRQ9).
	/// @param byteAddress The absolute Sound RAM address being accessed.
	void checkIRQ(uint32_t byteAddress);


	/// @brief Calculates the internal array index for a given hardware register address.
	/// @param address The absolute 32-bit hardware address (0x1F801C00+).
	/// @return The mapped index in the registers array.
	static uint32_t registerIndex(uint32_t address) { return ((address & ~1u) - 0x1F801C00) >> 1; }


	/// @brief Reads a 32-bit bitmask combined from two consecutive 16-bit registers (e.g., Key On/Off flags).
	/// @param lowAddress The hardware address of the lower 16 bits.
	/// @return The combined 32-bit mask.
	uint32_t readMask(uint32_t lowAddress) const { return static_cast<uint32_t>(registers[registerIndex(lowAddress)]) | (static_cast<uint32_t>(registers[registerIndex(lowAddress + 2)]) << 16); }



	// Transfer Engine

	/// @brief Executes pending Sound RAM transfers based on elapsed CPU cycles.
	void runTransfer();


	/// @brief Updates internal state flags when the RAM transfer FIFO changes.
	void updateTransferState();


	/// @brief Handles side effects of writes to the SPU Control register.
	/// @param previous The old value of the control register.
	/// @param current The new value written.
	void onControlWrite(uint16_t previous, uint16_t current);


	/// @brief Pushes a halfword into the RAM transfer FIFO.
	/// @param value The 16-bit value to push.
	void pushTransferFifo(uint16_t value);


	/// @brief Pops a halfword from the RAM transfer FIFO.
	/// @return The 16-bit value popped.
	uint16_t popTransferFifo();


	/// @brief Reads a halfword from Sound RAM at the current transfer address and handles IRQ checks.
	/// @return The 16-bit value read.
	uint16_t readRamHalfword();


	/// @brief Writes a halfword to Sound RAM at the current transfer address and handles IRQ checks.
	/// @param value The 16-bit value to write.
	void writeRamHalfword(uint16_t value);
	
	
	/// @brief Extracts the current RAM transfer mode from the Control register.
	/// @return The 2-bit transfer mode (0=Stop, 1=Manual Write, 2=DMA Write, 3=DMA Read).
	uint32_t transferMode() const { return (registers[registerIndex(0x1F801DAA)] >> 4) & 0x3; }


	/// @brief Checks if the given transfer mode is a write operation.
	/// @param mode The 2-bit transfer mode.
	/// @return True if the mode is Manual Write or DMA Write.
	static bool isWriteMode(uint32_t mode) { return mode == TRANSFER_MANUAL_WRITE || mode == TRANSFER_DMA_WRITE; }



	// Reverb

	/// @brief Handles the downsampling, core reverb processing, and upsampling pipeline.
	/// @param leftIn Raw left audio sample input.
	/// @param rightIn Raw right audio sample input.
	/// @param leftOut Processed left reverb output.
	/// @param rightOut Processed right reverb output.
	void runReverb(int32_t leftIn, int32_t rightIn, int32_t& leftOut, int32_t& rightOut);


	/// @brief Executes the core 22.05 kHz DSP hardware reverb reflection algorithms.
	/// @param leftIn Downsampled left audio input.
	/// @param rightIn Downsampled right audio input.
	/// @param leftOut Raw left reverb output.
	/// @param rightOut Raw right reverb output.
	void runReverbCore(int32_t leftIn, int32_t rightIn, int32_t& leftOut, int32_t& rightOut);


	/// @brief Calculates an absolute Sound RAM address for reverb operations, wrapping around the work area.
	/// @param registerValue The relative offset from the reverb base.
	/// @param byteOffset Additional byte offset applied before wrapping.
	/// @return The absolute Sound RAM address.
	uint32_t reverbAddress(uint32_t registerValue, int32_t byteOffset) const;


	/// @brief Reads a 16-bit sample from the reverb work area in Sound RAM.
	/// @param byteAddress The absolute Sound RAM address.
	/// @return The 16-bit signed sample.
	int32_t reverbRead(uint32_t byteAddress);


	/// @brief Writes a 16-bit sample to the reverb work area in Sound RAM.
	/// @param byteAddress The absolute Sound RAM address.
	/// @param value The 16-bit signed sample.
	void reverbWrite(uint32_t byteAddress, int32_t value);
	
	
	/// @brief Reads a raw reverb configuration register.
	/// @param address The absolute hardware address of the reverb register.
	/// @return The 16-bit register value.
	uint32_t reverbRegisterRaw(uint32_t address) const { return registers[registerIndex(address)]; }


	/// @brief Reads a specific configuration register for a given voice.
	/// @param voiceIndex The hardware index (0-23) of the voice.
	/// @param offset The byte offset (0x00 to 0x0E) within the voice's register block.
	/// @return The 16-bit register value.
	uint16_t voiceRegister(uint32_t voiceIndex, uint32_t offset) const { return registers[(voiceIndex * 0x10 + offset) >> 1]; }



	// Helpers

	/// @brief Advances an ADPCM envelope math phase by one sample.
	/// @param counter The envelope cycle countdown timer.
	/// @param level The current amplitude level.
	/// @param shift The shift value determining envelope speed.
	/// @param magnitudeStep The base magnitude of the level change.
	/// @param exponential True for exponential curves, false for linear.
	/// @param negativePhase True if the envelope is decaying into negative amplitude.
	/// @return The new amplitude level.
	static int32_t stepEnvelope(int32_t& counter, int32_t level, uint32_t shift, int32_t magnitudeStep, bool exponential, bool negativePhase);


	/// @brief Performs 4-point Catmull-Rom interpolation for audio resampling.
	/// @param point0 Sample at t-1.
	/// @param point1 Sample at t0.
	/// @param point2 Sample at t1.
	/// @param point3 Sample at t2.
	/// @param fraction The fractional interpolation position (0-255).
	/// @return The interpolated sample value.
	int32_t interpolate(int32_t point0, int32_t point1, int32_t point2, int32_t point3, uint32_t fraction);


	/// @brief Performs signed 16-bit fixed-point multiplication where 0x7FFF represents ~1.0.
	/// @param a First operand.
	/// @param b Second operand.
	/// @return The fixed-point product.
	static int32_t multiplyQ15(int32_t a, int32_t b) { return static_cast<int32_t>((static_cast<int64_t>(a) * b) >> 15); }
};