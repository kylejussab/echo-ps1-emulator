#pragma once
#include <cstdint>
#include <vector>

class SPU {
public:
	SPU();

	uint16_t read16(uint32_t address);
	void write16(uint32_t address, uint16_t value);
	uint32_t read32(uint32_t address);
	void write32(uint32_t address, uint32_t value);

	void writeDMAWord(uint32_t word);
	uint32_t readDMAWord();

private:
	static constexpr uint32_t REGISTER_BASE_ADDRESS = 0x1F801C00;
	static constexpr uint32_t REGISTER_COUNT = 0x100; // 512 bytes of 16-bit registers
	static constexpr uint32_t SOUND_RAM_SIZE = 512 * 1024;

	static constexpr uint32_t REGISTER_TRANSFER_ADDRESS = 0x1F801DA6;
	static constexpr uint32_t REGISTER_TRANSFER_FIFO = 0x1F801DA8;
	static constexpr uint32_t REGISTER_CONTROL = 0x1F801DAA;
	static constexpr uint32_t REGISTER_STATUS = 0x1F801DAE;

	std::vector<uint8_t> soundRam;
	uint16_t registers[REGISTER_COUNT] = {};
	uint32_t transferAddress = 0; // In bytes, not the 8-byte units the register uses

	static uint32_t registerIndex(uint32_t address);
	void writeTransferFifo(uint16_t value);
};