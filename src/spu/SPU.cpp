#include <algorithm>
#include "SPU.h"


SPU::SPU() {
	soundRam.resize(SOUND_RAM_SIZE, 0);
	outputSamples.reserve(MAX_BUFFERED_SAMPLES);
	registers[registerIndex(0x1F801DAC)] = 0x0004;
}


uint8_t SPU::read8(uint32_t address) {
	uint16_t value = read16(address & ~1u);
	return static_cast<uint8_t>((address & 1) ? (value >> 8) : (value & 0xFF));
}


void SPU::write8(uint32_t address, uint8_t value) {
	write16(address & ~1u, static_cast<uint16_t>(value | (value << 8)));
}


uint16_t SPU::read16(uint32_t address) {
	address &= ~1u;

	// Current volume of each voice (read-only: 0x1F801E00 to 0x1F801F7F)
	if (address >= 0x1F801E00) {
		uint32_t offset = address - 0x1F801E00;

		if (offset < VOICE_COUNT * 4) {
			const Voice& voice = voices[offset >> 2];
			return static_cast<uint16_t>((offset & 2) ? voice.volumeRight.level : voice.volumeLeft.level);
		}

		return 0;
	}

	// Voice Registers (0x1F801C00 - 0x1F801D7F)
	if (address < 0x1F801D80) {
		uint32_t voiceIndex = (address - 0x1F801C00) >> 4;
		uint32_t offset = (address - 0x1F801C00) & 0x0F;
		if (offset == 0x0C) return static_cast<uint16_t>(voices[voiceIndex].adsrLevel);
		if (offset == 0x0E) return static_cast<uint16_t>(voices[voiceIndex].repeatAddress >> 3);

		return registers[registerIndex(address)];
	}


	switch (address) {
		case 0x1F801D9C: { // Voice Status Low
			return static_cast<uint16_t>(endx & 0xFFFF);
		}
		case 0x1F801D9E: { // Voice Status High
			return static_cast<uint16_t>((endx >> 16) & 0xFF);
		}
		case 0x1F801DB8: { // Current Main Volume Left
			return static_cast<uint16_t>(mainVolumeLeft.level);
		}
		case 0x1F801DBA: { // Current Main Volume Right
			return static_cast<uint16_t>(mainVolumeRight.level);
		}
		case 0x1F801DAE: { // Status
			uint16_t status = registers[registerIndex(0x1F801DAA)] & 0x003F;
			if (irqFlag) status |= 0x0040;

			uint32_t mode = transferMode();
			bool writeRequest = mode == TRANSFER_DMA_WRITE && DMARequest();
			bool readRequest = mode == TRANSFER_DMA_READ && DMARequest();

			if (writeRequest || readRequest) status |= 0x0080;
			if (writeRequest) status |= 0x0100;
			if (readRequest) status |= 0x0200;

			if (isTransferBusy()) status |= 0x0400;
			if (captureOffset & 0x100) status |= 0x0800;
			return status;
		}
		default: {
			return registers[registerIndex(address)];
		}
	}	
}


void SPU::write16(uint32_t address, uint16_t value) {
	address &= ~1u;

	// Current Volume is read-only (0x1F801E00+)
	if (address >= 0x1F801E00) return;

	const uint16_t previous = registers[registerIndex(address)];
	registers[registerIndex(address)] = value;

	// Voice Registers (0x1F801C00 - 0x1F801D7F)
	if (address < 0x1F801D80) {
		uint32_t voiceIndex = (address - 0x1F801C00) >> 4;
		switch ((address - 0x1F801C00) & 0x0F) {
			case 0x00: {
				voices[voiceIndex].volumeLeft.write(value); break;
			}
			case 0x02:{
				voices[voiceIndex].volumeRight.write(value); break;
			}
			case 0x0E: {
				voices[voiceIndex].repeatAddress = (static_cast<uint32_t>(value) * 8) % SOUND_RAM_SIZE; break;
			}
		}

		return;
	}

	switch (address) {
		case 0x1F801D80: { // Main Volume Left
			mainVolumeLeft.write(value);
			break;
		}
		case 0x1F801D82: { // Main Volume Right
			mainVolumeRight.write(value);
			break;
		}	
		case 0x1F801D88: { // Key On Low
			uint32_t firstVoice = (address == 0x1F801D8A) ? 16 : 0;

			for (uint32_t bit = 0; bit < 16 && firstVoice + bit < VOICE_COUNT; bit++) {
				if (value & (1u << bit)) keyOn(firstVoice + bit);
			}

			break;
		}
		case 0x1F801D8A: { // Key On High
			uint32_t firstVoice = (address == 0x1F801D8A) ? 16 : 0;
			
			for (uint32_t bit = 0; bit < 16 && firstVoice + bit < VOICE_COUNT; bit++) {
				if (value & (1u << bit)) keyOn(firstVoice + bit);
			}

			break;
		}
		case 0x1F801D8C: { // Key Off Low
			uint32_t firstVoice = (address == 0x1F801D8E) ? 16 : 0;

			for (uint32_t bit = 0; bit < 16 && firstVoice + bit < VOICE_COUNT; bit++) {
				if (value & (1u << bit)) keyOff(firstVoice + bit);
			}
			
			break;
		}
		case 0x1F801D8E: { // Key Off High
			uint32_t firstVoice = (address == 0x1F801D8E) ? 16 : 0;

			for (uint32_t bit = 0; bit < 16 && firstVoice + bit < VOICE_COUNT; bit++) {
				if (value & (1u << bit)) keyOff(firstVoice + bit);
			}

			break;
		}
		case 0x1F801DA2: { // Reverb Base Address
			reverbBaseAddress = (static_cast<uint32_t>(value) * 8) % SOUND_RAM_SIZE;
			reverbCurrentAddress = reverbBaseAddress;
			break;
		}
		case 0x1F801DA4: { // IRQ Address
			irqAddress = (static_cast<uint32_t>(value) * 8) % SOUND_RAM_SIZE;
			break;
		}	
		case 0x1F801DAA: { // Control
			onControlWrite(previous, value);
			break;
		}	
		case 0x1F801DA6: { // Transfer Address
			transferAddress = (static_cast<uint32_t>(value) * 8) % SOUND_RAM_SIZE;
			break;
		}	
		case 0x1F801DA8: { // Transfer FIFO
			pushTransferFifo(value);
			updateTransferState();
			break;
		}	
		default: {
			break; // Other registers just sit in the array via the write-through at the top
		}
	}
}


uint32_t SPU::read32(uint32_t address) {
	uint32_t lowHalf = read16(address);
	uint32_t highHalf = read16(address + 2);
	return lowHalf | (highHalf << 16);
}


void SPU::write32(uint32_t address, uint32_t value) {
	write16(address, value & 0xFFFF);
	write16(address + 2, value >> 16);
}


uint32_t SPU::readDMAWord() {
	uint16_t low = fifoCount > 0 ? popTransferFifo() : readRamHalfword();
	uint16_t high = fifoCount > 0 ? popTransferFifo() : readRamHalfword();
	updateTransferState();
	return static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 16);
}


void SPU::writeDMAWord(uint32_t word) {
	pushTransferFifo(static_cast<uint16_t>(word & 0xFFFF));
	pushTransferFifo(static_cast<uint16_t>(word >> 16));
	updateTransferState();
}


bool SPU::DMARequest() const {
	switch (transferMode()) {
		case TRANSFER_DMA_WRITE: {
			return (FIFO_SIZE - fifoCount) >= DMA_REQUEST_THRESHOLD;
		}
		case TRANSFER_DMA_READ: {
			return fifoCount >= DMA_REQUEST_THRESHOLD;
		}
		default: {
			return false;
		}
	}
}



void SPU::advanceClock(int cycles) {
	if (transferActive) {
		transferCycleAccumulator += static_cast<uint32_t>(cycles);
		if (transferCycleAccumulator >= TRANSFER_CYCLES_PER_HALFWORD) runTransfer();
	}

	cycleAccumulator += static_cast<uint32_t>(cycles);

	if (cycleAccumulator >= CYCLES_PER_SAMPLE) runSamples();
}


void SPU::drainOutput(std::vector<int16_t>& destination) {
	destination.clear();
	destination.swap(outputSamples);
}



// Privates
void SPU::VolumeSweep::write(uint16_t value) {
	reg = value;
	counter = 0;
	if ((value & 0x8000) == 0) {
		level = static_cast<int16_t>(value << 1);
	}
}


void SPU::VolumeSweep::step() {
	if ((reg & 0x8000) == 0) return;

	bool exponential = (reg & 0x4000) != 0;
	bool decreasing = (reg & 0x2000) != 0;
	bool negativePhase = (reg & 0x1000) != 0;
	uint32_t shift = (reg >> 2) & 0x1F;
	int32_t stepBits = reg & 0x3;
	int32_t magnitudeStep = decreasing ? -(8 - stepBits) : (7 - stepBits);

	level = SPU::stepEnvelope(counter, level, shift, magnitudeStep, exponential, negativePhase);
	level = std::clamp(level, -0x7FFF, 0x7FFF);
}


void SPU::runSamples() {
	while (cycleAccumulator >= CYCLES_PER_SAMPLE) {
		cycleAccumulator -= CYCLES_PER_SAMPLE;

		generateSample();

		const int16_t captured[4] = { cdInputLeft, cdInputRight, voices[1].lastOutput, voices[3].lastOutput };

		for (uint32_t buffer = 0; buffer < 4; buffer++) {
			uint32_t address = buffer * 0x200 + captureOffset;
			soundRam[address] = static_cast<uint8_t>(captured[buffer] & 0xFF);
			soundRam[address + 1] = static_cast<uint8_t>((captured[buffer] >> 8) & 0xFF);
			checkIRQ(address);
		}

		captureOffset = (captureOffset + 2) & 0x1FF;
	}
}


void SPU::generateSample() {
	const uint32_t pitchModulation = readMask(0x1F801D90);
	const uint32_t noiseEnable = readMask(0x1F801D94);
	const uint32_t reverbEnable = readMask(0x1F801D98);
	const uint16_t control = registers[registerIndex(0x1F801DAA)];
	updateNoise();

	int32_t mixLeft = 0;
	int32_t mixRight = 0;
	int32_t reverbInLeft = 0;
	int32_t reverbInRight = 0;

	for (uint32_t voiceIndex = 0; voiceIndex < VOICE_COUNT; voiceIndex++) {
		Voice& voice = voices[voiceIndex];

		voice.volumeLeft.step();
		voice.volumeRight.step();

		int32_t output = 0;

		if (voice.phase != ADSRPhase::Off) {
			uint32_t index = voice.pitchCounter >> 12;
			uint32_t fraction = (voice.pitchCounter >> 4) & 0xFF;

			int32_t sample;
			if (noiseEnable & (1u << voiceIndex)) {
				sample = static_cast<int16_t>(noiseLevel);
			}
			else {
				sample = interpolate(voice.samples[index], voice.samples[index + 1], voice.samples[index + 2], voice.samples[index + 3], fraction);
			}

			updateEnvelope(voice, voiceIndex);
			output = (sample * voice.adsrLevel) >> 15;

			int32_t step = voiceRegister(voiceIndex, 0x4);

			if (voiceIndex > 0 && (pitchModulation & (1u << voiceIndex))) {
				int32_t factor = voices[voiceIndex - 1].lastOutput + 0x8000;
				step = (step * factor) >> 15;
			}

			if (step > 0x3FFF) step = 0x3FFF;

			voice.pitchCounter += static_cast<uint32_t>(step);

			if ((voice.pitchCounter >> 12) >= 28) {
				voice.pitchCounter -= 28u << 12;
				finishBlock(voiceIndex);

				if (voice.phase != ADSRPhase::Off) decodeBlock(voice);
			}
		}

		voice.lastOutput = static_cast<int16_t>(output);

		const int32_t voiceLeft = (output * voice.volumeLeft.level) >> 15;
		const int32_t voiceRight = (output * voice.volumeRight.level) >> 15;
		mixLeft += voiceLeft;
		mixRight += voiceRight;

		if (reverbEnable & (1u << voiceIndex)) {
			reverbInLeft += voiceLeft;
			reverbInRight += voiceRight;
		}
	}

	int16_t cdLeft = 0;
	int16_t cdRight = 0;

	if (!cdAudioSource || !cdAudioSource(cdLeft, cdRight)) {
		cdLeft = 0;
		cdRight = 0;
	}

	cdInputLeft = cdLeft;
	cdInputRight = cdRight;

	if (control & 0x0001) {
		const int32_t volumeLeft = static_cast<int16_t>(registers[registerIndex(0x1F801DB0)]);
		const int32_t volumeRight = static_cast<int16_t>(registers[registerIndex(0x1F801DB2)]);
		const int32_t scaledLeft = multiplyQ15(cdLeft, volumeLeft);
		const int32_t scaledRight = multiplyQ15(cdRight, volumeRight);

		mixLeft += scaledLeft;
		mixRight += scaledRight;

		if (control & 0x0004) {
			reverbInLeft += scaledLeft;
			reverbInRight += scaledRight;
		}
	}

	/// @note External audio input (SPUCNT bits 1 and 3, volumes at 0x1F801DB4/6) is unimplemented. Nothing is wired to it on a retail console.

	int32_t reverbOutLeft = 0;
	int32_t reverbOutRight = 0;
	runReverb(reverbInLeft, reverbInRight, reverbOutLeft, reverbOutRight);
	mixLeft += reverbOutLeft;
	mixRight += reverbOutRight;

	mainVolumeLeft.step();
	mainVolumeRight.step();
	int32_t left = (std::clamp(mixLeft, -0x8000, 0x7FFF) * mainVolumeLeft.level) >> 15;
	int32_t right = (std::clamp(mixRight, -0x8000, 0x7FFF) * mainVolumeRight.level) >> 15;

	bool audible = (control & 0x8000) != 0 && (control & 0x4000) != 0;

	if (!audible) {
		left = 0;
		right = 0;
	}

	if (outputSamples.size() + 2 <= MAX_BUFFERED_SAMPLES) {
		outputSamples.push_back(static_cast<int16_t>(std::clamp(left, -0x8000, 0x7FFF)));
		outputSamples.push_back(static_cast<int16_t>(std::clamp(right, -0x8000, 0x7FFF)));
	}
}


void SPU::keyOn(uint32_t voiceIndex) {
	Voice& voice = voices[voiceIndex];

	voice.currentAddress = (static_cast<uint32_t>(voiceRegister(voiceIndex, 0x6)) * 8) % SOUND_RAM_SIZE;
	voice.repeatAddress = voice.currentAddress;
	voice.pitchCounter = 0;
	std::fill(std::begin(voice.samples), std::end(voice.samples), static_cast<int16_t>(0));
	voice.adpcmOld = 0;
	voice.adpcmOlder = 0;

	voice.phase = ADSRPhase::Attack;
	voice.adsrLevel = 0;
	voice.adsrCounter = 0;

	endx &= ~(1u << voiceIndex);

	decodeBlock(voice);
}


void SPU::keyOff(uint32_t voiceIndex) {
	Voice& voice = voices[voiceIndex];

	if (voice.phase == ADSRPhase::Off) return;

	voice.phase = ADSRPhase::Release;
	voice.adsrCounter = 0;
}


void SPU::decodeBlock(Voice& voice) {
	voice.currentAddress %= SOUND_RAM_SIZE;

	for (uint32_t halfword = 0; halfword < 8; halfword++) {
		checkIRQ(voice.currentAddress + halfword * 2);
	}

	voice.samples[0] = voice.samples[28];
	voice.samples[1] = voice.samples[29];
	voice.samples[2] = voice.samples[30];

	auto byteAt = [&](uint32_t offset) -> uint8_t {
		return soundRam[(voice.currentAddress + offset) % SOUND_RAM_SIZE];
	};

	uint8_t header = byteAt(0);
	voice.blockFlags = byteAt(1);

	uint32_t shift = header & 0xF;
	uint32_t filter = (header >> 4) & 0x7;
	if (shift > 12) shift = 9;
	if (filter > 4) filter = 4;

	if (voice.blockFlags & 0x04) {
		voice.repeatAddress = voice.currentAddress;
	}

	for (uint32_t index = 0; index < 28; index++) {
		uint8_t byte = byteAt(2 + index / 2);
		int32_t nibble = (index & 1) ? (byte >> 4) : (byte & 0xF);

		int32_t sample = static_cast<int16_t>(nibble << 12) >> shift;
		sample += (voice.adpcmOld * ADPCM_FILTER_0[filter] + voice.adpcmOlder * ADPCM_FILTER_1[filter] + 32) >> 6;
		sample = std::clamp(sample, -0x8000, 0x7FFF);

		voice.adpcmOlder = voice.adpcmOld;
		voice.adpcmOld = sample;
		voice.samples[3 + index] = static_cast<int16_t>(sample);
	}
}


void SPU::finishBlock(uint32_t voiceIndex) {
	Voice& voice = voices[voiceIndex];

	if (voice.blockFlags & 0x01) {
		endx |= (1u << voiceIndex);
		voice.currentAddress = voice.repeatAddress;

		if ((voice.blockFlags & 0x02) == 0) {
			voice.phase = ADSRPhase::Off;
			voice.adsrLevel = 0;
		}
	}
	else {
		voice.currentAddress = (voice.currentAddress + 16) % SOUND_RAM_SIZE;
	}
}


void SPU::updateEnvelope(Voice& voice, uint32_t voiceIndex) {
	const uint16_t adsr1 = voiceRegister(voiceIndex, 0x8);
	const uint16_t adsr2 = voiceRegister(voiceIndex, 0xA);

	switch (voice.phase) {
	case ADSRPhase::Attack: {
		uint32_t shift = (adsr1 >> 10) & 0x1F;
		int32_t step = 7 - ((adsr1 >> 8) & 0x3);
		bool exponential = (adsr1 & 0x8000) != 0;

		voice.adsrLevel = stepEnvelope(voice.adsrCounter, voice.adsrLevel, shift, step, exponential, false);
		if (voice.adsrLevel >= 0x7FFF) {
			voice.adsrLevel = 0x7FFF;
			voice.phase = ADSRPhase::Decay;
			voice.adsrCounter = 0;
		}
		break;
	}
	case ADSRPhase::Decay: {
		uint32_t shift = (adsr1 >> 4) & 0xF;
		int32_t sustainLevel = std::min(static_cast<int32_t>(((adsr1 & 0xF) + 1) * 0x800), 0x7FFF);

		voice.adsrLevel = stepEnvelope(voice.adsrCounter, voice.adsrLevel, shift, -8, true, false);
		voice.adsrLevel = std::max(voice.adsrLevel, 0);
		
		if (voice.adsrLevel <= sustainLevel) {
			voice.phase = ADSRPhase::Sustain;
			voice.adsrCounter = 0;
		}
		break;
	}
	case ADSRPhase::Sustain: {
		uint32_t shift = (adsr2 >> 8) & 0x1F;
		int32_t stepBits = (adsr2 >> 6) & 0x3;
		bool decreasing = (adsr2 & 0x4000) != 0;
		bool exponential = (adsr2 & 0x8000) != 0;
		int32_t step = decreasing ? -(8 - stepBits) : (7 - stepBits);

		voice.adsrLevel = stepEnvelope(voice.adsrCounter, voice.adsrLevel, shift, step, exponential, false);
		voice.adsrLevel = std::max(0, std::min(0x7FFF, voice.adsrLevel));
		break;
	}
	case ADSRPhase::Release: {
		uint32_t shift = adsr2 & 0x1F;
		bool exponential = (adsr2 & 0x20) != 0;

		voice.adsrLevel = stepEnvelope(voice.adsrCounter, voice.adsrLevel, shift, -8, exponential, false);
		if (voice.adsrLevel <= 0) {
			voice.adsrLevel = 0;
			voice.phase = ADSRPhase::Off;
		}
		break;
	}
	case ADSRPhase::Off:
		break;
	}
}


void SPU::updateNoise() {
	uint16_t control = registers[registerIndex(0x1F801DAA)];
	uint32_t shift = (control >> 10) & 0xF;
	int32_t step = ((control >> 8) & 0x3) + 4;

	noiseTimer -= step;
	if (noiseTimer < 0) {
		noiseTimer += 0x20000 >> shift;
		uint32_t parity = ((noiseLevel >> 15) ^ (noiseLevel >> 12) ^ (noiseLevel >> 11) ^ (noiseLevel >> 10) ^ 1) & 1;
		noiseLevel = static_cast<uint16_t>((noiseLevel << 1) | parity);
	}
}


void SPU::checkIRQ(uint32_t byteAddress) {
	uint16_t control = registers[registerIndex(0x1F801DAA)];

	if ((control & 0x8040) != 0x8040) return;
	
	if ((byteAddress % SOUND_RAM_SIZE & ~1u) != irqAddress) return;

	if (irqFlag) return;

	irqFlag = true;

	if (raiseInterrupt) raiseInterrupt(0x0200);
}


//Private: Transfer Engine
void SPU::runTransfer() {
	while (transferActive && transferCycleAccumulator >= TRANSFER_CYCLES_PER_HALFWORD) {
		transferCycleAccumulator -= TRANSFER_CYCLES_PER_HALFWORD;

		if (transferMode() == TRANSFER_DMA_READ) {
			transferFifo[(fifoHead + fifoCount) % FIFO_SIZE] = readRamHalfword();
			fifoCount++;
		}
		else {
			writeRamHalfword(popTransferFifo());
		}
		updateTransferState();
	}
}


void SPU::updateTransferState() {
	uint32_t mode = transferMode();
	transferActive = (isWriteMode(mode) && fifoCount > 0) || (mode == TRANSFER_DMA_READ && fifoCount < FIFO_SIZE);
	if (!transferActive) transferCycleAccumulator = 0;
}


void SPU::onControlWrite(uint16_t previous, uint16_t current) {
	if ((current & 0x0040) == 0) irqFlag = false;

	uint32_t previousMode = (previous >> 4) & 0x03;
	uint32_t currentMode = (current >> 4) & 0x03;

	if (previousMode != currentMode && (previousMode == TRANSFER_DMA_READ || currentMode == TRANSFER_DMA_READ)) {
		fifoHead = 0;
		fifoCount = 0;
	}
	updateTransferState();
}


void SPU::pushTransferFifo(uint16_t value) {
	if (fifoCount == FIFO_SIZE) {
		uint16_t oldest = popTransferFifo();
		if (isWriteMode(transferMode())) writeRamHalfword(oldest);
	}

	transferFifo[(fifoHead + fifoCount) % FIFO_SIZE] = value;
	fifoCount++;
}


uint16_t SPU::popTransferFifo() {
	uint16_t value = transferFifo[fifoHead];
	fifoHead = (fifoHead + 1) % FIFO_SIZE;
	fifoCount--;
	return value;
}


uint16_t SPU::readRamHalfword() {
	checkIRQ(transferAddress);
	uint16_t value = static_cast<uint16_t>(soundRam[transferAddress] | (soundRam[transferAddress + 1] << 8));
	transferAddress = (transferAddress + 2) % SOUND_RAM_SIZE;
	return value;
}


void SPU::writeRamHalfword(uint16_t value) {
	checkIRQ(transferAddress);
	soundRam[transferAddress] = value & 0xFF;
	soundRam[transferAddress + 1] = value >> 8;
	transferAddress = (transferAddress + 2) % SOUND_RAM_SIZE;
}


//Private: Reverb
void SPU::runReverb(int32_t leftIn, int32_t rightIn, int32_t& leftOut, int32_t& rightOut) {
	reverbInputLeft[reverbInputPosition & 63] = std::clamp(leftIn, -0x8000, 0x7FFF);
	reverbInputRight[reverbInputPosition & 63] = std::clamp(rightIn, -0x8000, 0x7FFF);
	reverbInputPosition++;

	if (reverbOddSample) {
		reverbOddSample = false;
		leftOut = reverbPendingLeft;
		rightOut = reverbPendingRight;
		return;
	}
	reverbOddSample = true;

	int32_t sumLeft = 0;
	int32_t sumRight = 0;

	for (uint32_t tap = 0; tap < 39; tap++) {
		uint32_t index = (reverbInputPosition - 1 - tap) & 63;
		sumLeft += REVERB_FIR[tap] * reverbInputLeft[index];
		sumRight += REVERB_FIR[tap] * reverbInputRight[index];
	}

	int32_t stepLeft = 0;
	int32_t stepRight = 0;
	runReverbCore(std::clamp(sumLeft >> 15, -0x8000, 0x7FFF), std::clamp(sumRight >> 15, -0x8000, 0x7FFF), stepLeft, stepRight);

	for (uint32_t index = 19; index > 0; index--) {
		reverbOutputHistoryLeft[index] = reverbOutputHistoryLeft[index - 1];
		reverbOutputHistoryRight[index] = reverbOutputHistoryRight[index - 1];
	}

	reverbOutputHistoryLeft[0] = stepLeft;
	reverbOutputHistoryRight[0] = stepRight;

	int32_t interpolatedLeft = 0;
	int32_t interpolatedRight = 0;

	for (uint32_t tap = 0; tap < 20; tap++) {
		interpolatedLeft += REVERB_FIR_EVEN[tap] * reverbOutputHistoryLeft[tap];
		interpolatedRight += REVERB_FIR_EVEN[tap] * reverbOutputHistoryRight[tap];
	}
	leftOut = std::clamp(interpolatedLeft >> 14, -0x8000, 0x7FFF);
	rightOut = std::clamp(interpolatedRight >> 14, -0x8000, 0x7FFF);

	reverbPendingLeft = reverbOutputHistoryLeft[9];
	reverbPendingRight = reverbOutputHistoryRight[9];
}


void SPU::runReverbCore(int32_t leftIn, int32_t rightIn, int32_t& leftOut, int32_t& rightOut) {
	auto signedRegister = [&](uint32_t address) {
		return static_cast<int32_t>(static_cast<int16_t>(reverbRegisterRaw(address)));
	};

	auto readRegister = [&](uint32_t address) {
		return reverbRead(reverbAddress(reverbRegisterRaw(address), 0));
	};

	const int32_t vIIR   = signedRegister(0x1F801DC4);
	const int32_t vWALL  = signedRegister(0x1F801DCE);
	const int32_t vCOMB1 = signedRegister(0x1F801DC6);
	const int32_t vCOMB2 = signedRegister(0x1F801DC8);
	const int32_t vCOMB3 = signedRegister(0x1F801DCA);
	const int32_t vCOMB4 = signedRegister(0x1F801DCC);
	const int32_t vAPF1  = signedRegister(0x1F801DD0);
	const int32_t vAPF2  = signedRegister(0x1F801DD2);
	const int32_t vLIN   = signedRegister(0x1F801DFC);
	const int32_t vRIN   = signedRegister(0x1F801DFE);
	const int32_t vLOUT  = static_cast<int16_t>(registers[registerIndex(0x1F801D84)]);
	const int32_t vROUT  = static_cast<int16_t>(registers[registerIndex(0x1F801D86)]);

	const int32_t left = multiplyQ15(leftIn, vLIN);
	const int32_t right = multiplyQ15(rightIn, vRIN);

	// Hardware reflection formula: (In + [dXSAME] * vWALL - [mXSAME - 2]) * vIIR + [mXSAME - 2]
	auto reflection = [&](int32_t input, uint32_t dRegister, uint32_t mRegister) {
		int32_t previous = reverbRead(reverbAddress(reverbRegisterRaw(mRegister), -2));
		int32_t target = input + multiplyQ15(readRegister(dRegister), vWALL);
		int32_t value = std::clamp(previous + multiplyQ15(target - previous, vIIR), -0x8000, 0x7FFF);
		reverbWrite(reverbAddress(reverbRegisterRaw(mRegister), 0), value);
	};

	reflection(left,  0x1F801DE0, 0x1F801DD4); // left to left   (dLSAME, mLSAME)
	reflection(right, 0x1F801DE2, 0x1F801DD6); // right to right (dRSAME, mRSAME)
	reflection(left,  0x1F801DF2, 0x1F801DE4); // right to left  (dRDIFF, mLDIFF)
	reflection(right, 0x1F801DF0, 0x1F801DE6); // left to right  (dLDIFF, mRDIFF)

	int32_t outLeft = std::clamp(multiplyQ15(vCOMB1, readRegister(0x1F801DD8)) // mLCOMB1
		+ multiplyQ15(vCOMB2, readRegister(0x1F801DDC)) // mLCOMB2
		+ multiplyQ15(vCOMB3, readRegister(0x1F801DE8)) // mLCOMB3
		+ multiplyQ15(vCOMB4, readRegister(0x1F801DEC)), -0x8000, 0x7FFF); // mLCOMB4
		
	int32_t outRight = std::clamp(multiplyQ15(vCOMB1, readRegister(0x1F801DDA)) // mRCOMB1
		+ multiplyQ15(vCOMB2, readRegister(0x1F801DDE)) // mRCOMB2
		+ multiplyQ15(vCOMB3, readRegister(0x1F801DEA)) // mRCOMB3
		+ multiplyQ15(vCOMB4, readRegister(0x1F801DEE)), -0x8000, 0x7FFF); // mRCOMB4

	// Hardware all-pass formula: v = v - vAPF * [m - d];  [m] = v;  v = v * vAPF + [m - d]
	auto allPass = [&](int32_t value, uint32_t mRegister, uint32_t dRegister, int32_t vAPF) {
		uint32_t delayedAddress = reverbAddress(static_cast<uint16_t>(reverbRegisterRaw(mRegister) - reverbRegisterRaw(dRegister)), 0);
		value = std::clamp(value - multiplyQ15(vAPF, reverbRead(delayedAddress)), -0x8000, 0x7FFF);
		reverbWrite(reverbAddress(reverbRegisterRaw(mRegister), 0), value);
		return std::clamp(multiplyQ15(value, vAPF) + reverbRead(delayedAddress), -0x8000, 0x7FFF);
	};
	outLeft  = allPass(outLeft,  0x1F801DF4, 0x1F801DC0, vAPF1); // mLAPF1, dAPF1
	outRight = allPass(outRight, 0x1F801DF6, 0x1F801DC0, vAPF1); // mRAPF1, dAPF1
	outLeft  = allPass(outLeft,  0x1F801DF8, 0x1F801DC2, vAPF2); // mLAPF2, dAPF2
	outRight = allPass(outRight, 0x1F801DFA, 0x1F801DC2, vAPF2); // mRAPF2, dAPF2

	leftOut = std::clamp(multiplyQ15(outLeft, vLOUT), -0x8000, 0x7FFF);
	rightOut = std::clamp(multiplyQ15(outRight, vROUT), -0x8000, 0x7FFF);

	reverbCurrentAddress = (reverbCurrentAddress + 2) & (SOUND_RAM_SIZE - 2);
	if (reverbCurrentAddress < reverbBaseAddress) reverbCurrentAddress = reverbBaseAddress;
}


uint32_t SPU::reverbAddress(uint32_t registerValue, int32_t byteOffset) const {
	uint32_t offset = reverbCurrentAddress + ((registerValue * 8u + static_cast<uint32_t>(byteOffset)) & (SOUND_RAM_SIZE - 1));
	if (offset >= SOUND_RAM_SIZE) offset = offset - SOUND_RAM_SIZE + reverbBaseAddress;

	return offset & (SOUND_RAM_SIZE - 1) & ~1u;
}


int32_t SPU::reverbRead(uint32_t byteAddress) {
	checkIRQ(byteAddress);
	return static_cast<int16_t>(soundRam[byteAddress] | (soundRam[byteAddress + 1] << 8));
}


void SPU::reverbWrite(uint32_t byteAddress, int32_t value) {
	checkIRQ(byteAddress);
	if ((registers[registerIndex(0x1F801DAA)] & 0x0080) == 0) return;

	soundRam[byteAddress] = static_cast<uint8_t>(value & 0xFF);
	soundRam[byteAddress + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}


//Private: Helpers
int32_t SPU::stepEnvelope(int32_t& counter, int32_t level, uint32_t shift, int32_t magnitudeStep, bool exponential, bool negativePhase) {
	int32_t cycles = 1 << (shift > 11 ? shift - 11 : 0);
	int32_t step = magnitudeStep * (1 << (shift < 11 ? 11 - shift : 0));
	int32_t magnitude = (level < 0) ? -level : level;

	if (exponential) {
		if (magnitudeStep > 0 && magnitude > 0x6000) cycles *= 4;
		if (magnitudeStep < 0) step = (step * magnitude) >> 15;
	}

	if (negativePhase) step = -step;

	counter--;

	if (counter > 0) return level;

	counter = cycles;
	return level + step;
}


int32_t SPU::interpolate(int32_t point0, int32_t point1, int32_t point2, int32_t point3, uint32_t fraction) {
	float time = static_cast<float>(fraction) / 256.0f;
	float result = 0.5f * ((2.0f * point1)
		+ (-point0 + point2) * time
		+ (2.0f * point0 - 5.0f * point1 + 4.0f * point2 - point3) * time * time
		+ (-point0 + 3.0f * point1 - 3.0f * point2 + point3) * time * time * time);
		
	return std::clamp(static_cast<int32_t>(result), -0x8000, 0x7FFF);
}

