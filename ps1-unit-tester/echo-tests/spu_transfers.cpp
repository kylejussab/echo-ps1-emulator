// Moving data into and out of sound RAM through the SPU's own interface: the manual-write procedure (FIFO at 1F801DA8h), the DMA-write and DMA-read
// word interface that the DMA controller's channel 4 uses, the transfer address, the status bits a game waits on (busy, DMA requests), and the SPU
// interrupt that data transfers trap. The DMA controller's side of channel 4 is in dma_transfers. Needs no BIOS and no other device.
//
// What is checked is what the psx-spx page "Sound Processing Unit (SPU)" documents:
//   - Manual write: set ATTR to Stop, set the transfer address, write 1..32 halfwords to the FIFO, set ATTR to Manual Write, wait until STATX bit 10
//     (transfer busy) goes off. There is no way to read sound RAM except DMA: set DMA Read, wait for the SPU to ask (it first collects data in its FIFO,
//     so STATX bits 9 and 7 are not set at once), take the words.
//   - TSA (transfer start address, in units of 8 bytes) stores the written value, which does NOT increment during transfers; a separate internal
//     address does. Data goes to sound RAM as little-endian halfwords at increasing addresses.
//   - STATX bit 8 is the DMA-write request and bit 9 the DMA-read request; bit 7 is "the same as ATTR bit 5" (set in both DMA modes).
//   - IRQA (IRQ address, in units of 8 bytes) plus ATTR bit 15 (SPU enable) and bit 6 (IRQ9 enable) make a data transfer to or from that address raise
//     IRQ9 (STATX bit 6); writing ATTR bit 6 as 0 acknowledges it.
// Left out on purpose because the page says "recommended" / gives no value: how long a transfer takes, the FIFO threshold at which the DMA request
// comes and goes, the unstable-read effects with DEV4_CTRL, and the erratic writes with RAM_CTRL other than 0004h.
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

std::vector<uint8_t> pattern(size_t bytes, uint8_t seed) {
	std::vector<uint8_t> data(bytes);
	for (size_t index = 0; index < bytes; index++) data[index] = static_cast<uint8_t>(seed + index * 7 + (index >> 4) * 13);
	return data;
}

std::string firstDifference(const std::vector<uint8_t>& got, const std::vector<uint8_t>& expected) {
	for (size_t index = 0; index < expected.size(); index++) {
		if (index >= got.size() || got[index] != expected[index]) return "byte " + std::to_string(index) + " is " + (index < got.size() ? hex(got[index], 2) : "missing") + ", expected " + hex(expected[index], 2);
	}
	return "";
}

// Data goes in through the FIFO and comes back out through DMA read, unchanged and in order
void checkManualWriteAndReadBack() {
	{
		SPU spu;
		std::vector<uint8_t> data = pattern(64, 0x11);
		upload(spu, 0x1000, data);
		std::vector<uint8_t> back = download(spu, 0x1000, 64);
		std::string difference = firstDifference(back, data);
		check(difference.empty(), "manual write of 32 halfwords (the FIFO's size) reads back unchanged through DMA read", difference);
	}
	{
		SPU spu;
		std::vector<uint8_t> data = pattern(200, 0x42);
		upload(spu, 0x2000, data);
		std::vector<uint8_t> back = download(spu, 0x2000, 200);
		std::string difference = firstDifference(back, data);
		check(difference.empty(), "manual write of 100 halfwords in several blocks of the FIFO reads back unchanged", difference);
	}
	{
		// Two separate uploads do not disturb each other, and a read at one address does not see the other's data
		SPU spu;
		std::vector<uint8_t> first = pattern(32, 0x01), second = pattern(32, 0xA0);
		upload(spu, 0x4000, first);
		upload(spu, 0x4040, second);
		check(firstDifference(download(spu, 0x4000, 32), first).empty() && firstDifference(download(spu, 0x4040, 32), second).empty(), "two uploads to different addresses each read back at their own address");
	}
	{
		// Little-endian halfwords at increasing addresses
		SPU spu;
		upload(spu, 0x0800, {0x34, 0x12, 0x78, 0x56});
		std::vector<uint8_t> back = download(spu, 0x0800, 4);
		check(back == std::vector<uint8_t>({0x34, 0x12, 0x78, 0x56}), "halfword 1234h then 5678h sit at consecutive even addresses, low byte first");
	}
}

// Data written to the FIFO in Stop mode waits there; selecting Manual Write is what sends it to sound RAM
void checkFifoWaitsForManualWriteMode() {
	SPU spu;
	upload(spu, 0x3000, std::vector<uint8_t>(32, 0x00));
	spu.write16(CONTROL, CONTROL_ON | MODE_STOP);
	spu.write16(TRANSFER_ADDRESS, 0x3000 / 8);
	for (uint16_t halfword = 0; halfword < 8; halfword++) spu.write16(TRANSFER_FIFO, 0xA5A5);
	runCycles(spu, 4000);
	check(download(spu, 0x3000, 16) == std::vector<uint8_t>(16, 0x00), "halfwords written to the FIFO in Stop mode do not reach sound RAM");
	spu.write16(CONTROL, CONTROL_ON | MODE_STOP);
	spu.write16(TRANSFER_ADDRESS, 0x3000 / 8);
	for (uint16_t halfword = 0; halfword < 8; halfword++) spu.write16(TRANSFER_FIFO, 0xA5A5);
	spu.write16(CONTROL, CONTROL_ON | MODE_MANUAL_WRITE);
	runCycles(spu, 4000);
	check(download(spu, 0x3000, 16) == std::vector<uint8_t>(16, 0xA5), "selecting Manual Write sends the FIFO's halfwords to sound RAM");
}

// STATX bit 10 is on while the FIFO is being written out and off when it is done
void checkBusyFlag() {
	SPU spu;
	spu.write16(CONTROL, CONTROL_ON | MODE_STOP);
	spu.write16(TRANSFER_ADDRESS, 0x1000 / 8);
	for (uint16_t halfword = 0; halfword < 32; halfword++) spu.write16(TRANSFER_FIFO, halfword);
	spu.write16(CONTROL, CONTROL_ON | MODE_MANUAL_WRITE);
	check((spu.read16(STATUS) & STATUS_BUSY) != 0, "STATX bit 10: the transfer is busy right after Manual Write is selected with a full FIFO", "STATX is " + hex(spu.read16(STATUS), 4));
	runCycles(spu, 4000);
	check((spu.read16(STATUS) & STATUS_BUSY) == 0, "STATX bit 10: the transfer is no longer busy once the FIFO has been written out", "STATX is " + hex(spu.read16(STATUS), 4));
}

// The transfer start address register keeps what was written; transfers move an internal address
void checkTransferAddressRegister() {
	SPU spu;
	spu.write16(TRANSFER_ADDRESS, 0x0234);
	check(spu.read16(TRANSFER_ADDRESS) == 0x0234, "TSA reads back what was written");
	upload(spu, 0x1000, pattern(64, 0x05));
	spu.write16(TRANSFER_ADDRESS, 0x0234);
	spu.write16(CONTROL, CONTROL_ON | MODE_MANUAL_WRITE);
	for (uint16_t halfword = 0; halfword < 16; halfword++) spu.write16(TRANSFER_FIFO, halfword);
	runCycles(spu, 4000);
	check(spu.read16(TRANSFER_ADDRESS) == 0x0234, "TSA does not increment during a transfer", "TSA is " + hex(spu.read16(TRANSFER_ADDRESS), 4));
}

// The DMA requests in STATX
void checkDmaRequestBits() {
	{
		SPU spu;
		spu.write16(CONTROL, CONTROL_ON | MODE_DMA_WRITE);
		runSamples(spu, 2);
		uint16_t status = spu.read16(STATUS);
		check((status & (STATUS_DMA_WRITE_REQUEST | STATUS_DMA_REQUEST)) == (STATUS_DMA_WRITE_REQUEST | STATUS_DMA_REQUEST) && (status & STATUS_DMA_READ_REQUEST) == 0,
			"DMA write mode with an empty FIFO: STATX bit 8 (write request) and bit 7 are set, bit 9 is not", "STATX is " + hex(status, 4));
	}
	{
		SPU spu;
		spu.write16(TRANSFER_ADDRESS, 0x1000 / 8);
		spu.write16(CONTROL, CONTROL_ON | MODE_DMA_READ);
		uint16_t immediately = spu.read16(STATUS);
		check((immediately & (STATUS_DMA_READ_REQUEST | STATUS_DMA_REQUEST)) == 0, "DMA read mode: STATX bits 9 and 7 are not set at once (the SPU first collects data in its FIFO)", "STATX is " + hex(immediately, 4));
		runCycles(spu, 4000);
		uint16_t later = spu.read16(STATUS);
		check((later & STATUS_DMA_READ_REQUEST) != 0 && (later & STATUS_DMA_REQUEST) != 0 && (later & STATUS_DMA_WRITE_REQUEST) == 0,
			"DMA read mode: once the FIFO holds data, STATX bits 9 and 7 are set and bit 8 is not", "STATX is " + hex(later, 4));
	}
	{
		SPU spu;
		spu.write16(CONTROL, CONTROL_ON | MODE_STOP);
		runSamples(spu, 2);
		check((spu.read16(STATUS) & (STATUS_DMA_REQUEST | STATUS_DMA_WRITE_REQUEST | STATUS_DMA_READ_REQUEST)) == 0, "Stop mode: no DMA request is shown in STATX");
	}
}

// The DMA word interface: what channel 4 hands over word by word. A word's low halfword goes to the lower address.
void checkDmaWordInterface() {
	SPU spu;
	spu.write16(CONTROL, CONTROL_ON | MODE_STOP);
	spu.write16(TRANSFER_ADDRESS, 0x1000 / 8);
	spu.write16(CONTROL, CONTROL_ON | MODE_DMA_WRITE);
	std::vector<uint8_t> expected;
	for (uint32_t word = 0; word < 16; word++) {
		uint32_t value = 0x11223344u * (word + 1) + word;
		spu.writeDMAWord(value);
		for (int byte = 0; byte < 4; byte++) expected.push_back(static_cast<uint8_t>(value >> (8 * byte)));
	}
	runCycles(spu, 4000);
	std::vector<uint8_t> back = download(spu, 0x1000, 64);
	std::string difference = firstDifference(back, expected);
	check(difference.empty(), "DMA write of 16 words lands in sound RAM in order, each word's low halfword first", difference);
}

// The SPU interrupt that data transfers trap
void checkTransferInterrupt() {
	auto transferTo = [](SPU& spu, uint32_t address, uint16_t control) {
		spu.write16(CONTROL, control | MODE_STOP);
		spu.write16(TRANSFER_ADDRESS, static_cast<uint16_t>(address / 8));
		for (uint16_t halfword = 0; halfword < 16; halfword++) spu.write16(TRANSFER_FIFO, halfword);
		spu.write16(CONTROL, control | MODE_MANUAL_WRITE);
		runCycles(spu, 4000);
	};
	{
		SPU spu;
		std::vector<uint16_t> raised;
		spu.setInterruptCallback([&raised](uint16_t bit) { raised.push_back(bit); });
		spu.write16(IRQ_ADDRESS, 0x1010 / 8);
		transferTo(spu, 0x1000, CONTROL_ON | CONTROL_IRQ_ENABLE);
		check(raised == std::vector<uint16_t>({0x0200}) && (spu.read16(STATUS) & STATUS_IRQ) != 0, "IRQ: a manual write that passes the IRQ address raises IRQ9 once and sets STATX bit 6",
			"raised " + std::to_string(raised.size()) + " times, STATX is " + hex(spu.read16(STATUS), 4));
		spu.write16(CONTROL, CONTROL_ON | MODE_STOP);   // bit 6 written as 0 acknowledges
		check((spu.read16(STATUS) & STATUS_IRQ) == 0, "IRQ: writing ATTR with bit 6 clear acknowledges STATX bit 6", "STATX is " + hex(spu.read16(STATUS), 4));
	}
	{
		SPU spu;
		std::vector<uint16_t> raised;
		spu.setInterruptCallback([&raised](uint16_t bit) { raised.push_back(bit); });
		spu.write16(IRQ_ADDRESS, 0x1010 / 8);
		transferTo(spu, 0x1000, CONTROL_ON);   // IRQ9 not enabled
		check(raised.empty() && (spu.read16(STATUS) & STATUS_IRQ) == 0, "IRQ: with ATTR bit 6 clear, a transfer over the IRQ address raises nothing");
	}
	{
		SPU spu;
		std::vector<uint16_t> raised;
		spu.setInterruptCallback([&raised](uint16_t bit) { raised.push_back(bit); });
		spu.write16(IRQ_ADDRESS, 0x1010 / 8);
		transferTo(spu, 0x1000, CONTROL_UNMUTE | CONTROL_IRQ_ENABLE);   // SPU enable (bit 15) clear
		check(raised.empty(), "IRQ: bit 6 only counts while bit 15 (SPU enable) is set");
	}
	{
		SPU spu;
		std::vector<uint16_t> raised;
		spu.setInterruptCallback([&raised](uint16_t bit) { raised.push_back(bit); });
		spu.write16(IRQ_ADDRESS, 0x2000 / 8);
		transferTo(spu, 0x1000, CONTROL_ON | CONTROL_IRQ_ENABLE);   // does not reach 2000h
		check(raised.empty(), "IRQ: a transfer that does not touch the IRQ address raises nothing");
	}
	{
		SPU spu;
		upload(spu, 0x1000, pattern(64, 0x33));
		std::vector<uint16_t> raised;
		spu.setInterruptCallback([&raised](uint16_t bit) { raised.push_back(bit); });
		spu.write16(IRQ_ADDRESS, 0x1010 / 8);
		download(spu, 0x1000, 64, CONTROL_ON | CONTROL_IRQ_ENABLE);
		check(!raised.empty(), "IRQ: a DMA read that passes the IRQ address raises IRQ9 as well");
	}
}

} // namespace

int main() {
	checkManualWriteAndReadBack();
	checkFifoWaitsForManualWriteMode();
	checkBusyFlag();
	checkTransferAddressRegister();
	checkDmaRequestBits();
	checkDmaWordInterface();
	checkTransferInterrupt();
	return echo_check::finish();
}
