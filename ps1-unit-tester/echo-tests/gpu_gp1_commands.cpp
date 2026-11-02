// GP1 display control commands and the GPU status register, against the stable psx-spx page (graphicsprocessingunitgpu.md:
// "GPU Display Control Commands (GP1)" and "GPU Status Register"). The GPU is built directly: no Bus, no BIOS, no SDL.
//
// The emulator reports itself as a v2 GPU (GP1(10h) index 7 reads 2), so the v2 column of the page's version table is what counts.
// Left out because the page gives no value or the emulator has no way to read it: the horizontal display range GP1(06h) (no getter),
// GP1(09h) and GP1(0Bh), the interlace/odd-even bit 31 (it depends on the video timing), the screen flip bit of GP1(08h).
#include "gpu/GPU.h"
#include "support/echo_check.h"
#include "support/gpu_access.h"
#include "support/isolated_probe.h"
#include <cstdint>
#include <string>

using namespace gpu_access;
using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;

namespace {

std::string differingBits(uint32_t actual, uint32_t expected) {
	return "got " + hex(actual) + ", expected " + hex(expected) + ", differing bits " + hex(actual ^ expected);
}

// GP1(00h): everything goes back to its documented reset value, and GPUSTAT reads 14802000h
void checkReset() {
	GPU gpu;
	// Move everything away from its default first, so the reset has something to undo
	gpu.writeGP1(0x08000000 | 0x3F);
	gpu.writeGP1(0x04000003);
	gpu.writeGP1(0x05000000 | 0x120 | (0x0A5u << 10));
	gpu.writeGP1(0x07000000 | 0x30 | (0x200u << 10));
	gpu.writeGP0(0xE1000000 | 0x7FF);
	gpu.writeGP0(0xE6000003);
	gpu.writeGP0(0x1F000000);
	gpu.writeGP1(0x00000000);

	uint32_t status = idleStatus(gpu);
	check(status == 0x14802000, "GP1(00h): GPUSTAT reads 0x14802000 after a reset", differingBits(status, 0x14802000));
	// The same value, one cause at a time, so each fix shows up on its own
	check((status & STATUS_INTERLACE_FIELD) != 0, "GP1(00h): GPUSTAT bit 13 is 1 (the interlace field is always 1 while interlace is off)", "status " + hex(status));
	check((status & STATUS_DMA_REQUEST) == 0, "GP1(00h): GPUSTAT bit 25 is 0 (the DMA direction is back to off)", "status " + hex(status));
	check((status & STATUS_READY_TO_SEND_VRAM) == 0, "GP1(00h): GPUSTAT bit 27 is 0 (no VRAM read is pending)", "status " + hex(status));
	const uint32_t otherBits = ~(STATUS_INTERLACE_FIELD | STATUS_DMA_REQUEST | STATUS_READY_TO_SEND_VRAM);
	check((status & otherBits) == (0x14802000 & otherBits), "GP1(00h): every other GPUSTAT bit has its reset value", differingBits(status & otherBits, 0x14802000 & otherBits));
	checkEqual(gpu.getDisplayAreaX(), 0, "GP1(00h): display area X is 0");
	checkEqual(gpu.getDisplayAreaY(), 0, "GP1(00h): display area Y is 0");
	checkEqual(gpu.getVerticalDisplayStart(), 0x010, "GP1(00h): vertical display range starts at 010h");
	checkEqual(gpu.getVerticalDisplayEnd(), 0x010 + 240, "GP1(00h): vertical display range ends at 010h + 240");

	// The page says a reset does what GP1(08h) = 0 does
	GPU modeZero;
	modeZero.writeGP1(0x08000000);
	check(gpu.getDisplayWidth() == modeZero.getDisplayWidth() && gpu.getDisplayHeight() == modeZero.getDisplayHeight(),
		"GP1(00h): the display mode is the one GP1(08h) = 0 gives",
		"got " + std::to_string(gpu.getDisplayWidth()) + "x" + std::to_string(gpu.getDisplayHeight()) + ", GP1(08h) = 0 gives " +
			std::to_string(modeZero.getDisplayWidth()) + "x" + std::to_string(modeZero.getDisplayHeight()));
}

// GP1(03h): bit 0 is "display off"; GPUSTAT bit 23 shows it
void checkDisplayEnable() {
	GPU gpu;
	gpu.writeGP1(0x00000000);
	gpu.writeGP1(0x03000000);
	check((idleStatus(gpu) & STATUS_DISPLAY_DISABLED) == 0, "GP1(03h) = 0 turns the display on (GPUSTAT bit 23 clear)");
	gpu.writeGP1(0x03000001);
	check((idleStatus(gpu) & STATUS_DISPLAY_DISABLED) != 0, "GP1(03h) = 1 turns the display off (GPUSTAT bit 23 set)");
}

// GP1(04h): the direction goes to GPUSTAT bits 29-30, and bit 25 means what the direction says it means
void checkDmaDirection() {
	for (uint32_t direction = 0; direction < 4; direction++) {
		GPU gpu;
		gpu.writeGP1(0x00000000);
		gpu.writeGP1(0x04000000 | direction);
		uint32_t status = idleStatus(gpu);
		checkEqual((status & STATUS_DMA_DIRECTION_BITS) >> 29, direction, "GP1(04h) = " + std::to_string(direction) + " reads back in GPUSTAT bits 29-30");
	}

	{   // direction 0: the request bit is always zero
		GPU gpu;
		gpu.writeGP1(0x00000000);
		check((idleStatus(gpu) & STATUS_DMA_REQUEST) == 0, "DMA direction 0: GPUSTAT bit 25 is zero");
	}
	{   // direction 1: "GP0 write FIFO not full"; an idle GPU has room
		GPU gpu;
		gpu.writeGP1(0x00000000);
		gpu.writeGP1(0x04000001);
		check((idleStatus(gpu) & STATUS_DMA_REQUEST) != 0, "DMA direction 1: GPUSTAT bit 25 is set while the write FIFO has room");
	}
	{   // direction 2: the same as bit 28 ("write FIFO empty"), which is set when idle
		GPU gpu;
		gpu.writeGP1(0x00000000);
		gpu.writeGP1(0x04000002);
		uint32_t status = idleStatus(gpu);
		check((status & STATUS_READY_FOR_DMA_BLOCK) != 0 && (status & STATUS_DMA_REQUEST) != 0, "DMA direction 2: GPUSTAT bit 25 follows bit 28 (set when idle)", "status " + hex(status));
	}
	{   // direction 3: the same as bit 27 ("read FIFO full"), which is clear when no VRAM read is pending
		GPU gpu;
		gpu.writeGP1(0x00000000);
		gpu.writeGP1(0x04000003);
		uint32_t status = idleStatus(gpu);
		check((status & STATUS_DMA_REQUEST) == 0, "DMA direction 3: GPUSTAT bit 25 is clear when there is no VRAM read to send", "status " + hex(status));
	}
	{   // direction 3 with a read pending: both bits set
		GPU gpu;
		gpu.writeGP1(0x00000000);
		gpu.writeGP1(0x04000003);
		send(gpu, {0xC0000000, 0x00000000, 0x00020002});
		uint32_t status = idleStatus(gpu);
		check((status & STATUS_READY_TO_SEND_VRAM) != 0 && (status & STATUS_DMA_REQUEST) != 0, "DMA direction 3: bits 27 and 25 are set while a VRAM read is pending", "status " + hex(status));
	}
}

// GP1(08h): bits 0-5 go to GPUSTAT bits 17-22, bit 6 to bit 16, and the resolution follows
void checkDisplayMode() {
	for (uint32_t mode : {0x00u, 0x01u, 0x02u, 0x03u, 0x08u, 0x10u, 0x20u, 0x24u, 0x3Fu}) {
		GPU gpu;
		gpu.writeGP1(0x00000000);
		gpu.writeGP1(0x08000000 | mode);
		uint32_t status = idleStatus(gpu);
		checkEqual((status & STATUS_DISPLAY_MODE_BITS) >> 17, mode & 0x3F, "GP1(08h) = " + hex(mode, 2) + ": bits 0-5 are in GPUSTAT bits 17-22");
	}
	{
		GPU gpu;
		gpu.writeGP1(0x00000000);
		gpu.writeGP1(0x08000040);
		check((idleStatus(gpu) & STATUS_HORIZONTAL_RESOLUTION_2) != 0, "GP1(08h) bit 6 (368 pixels wide) is in GPUSTAT bit 16");
	}

	struct Resolution {
		uint32_t mode;
		uint32_t width;
		uint32_t height;
	};
	const Resolution resolutions[] = {{0x00, 256, 240}, {0x01, 320, 240}, {0x02, 512, 240}, {0x03, 640, 240}, {0x40, 368, 240}, {0x24, 256, 480}, {0x27, 640, 480}};
	for (const Resolution& resolution : resolutions) {
		GPU gpu;
		gpu.writeGP1(0x00000000);
		gpu.writeGP1(0x08000000 | resolution.mode);
		check(gpu.getDisplayWidth() == resolution.width && gpu.getDisplayHeight() == resolution.height,
			"GP1(08h) = " + hex(resolution.mode, 2) + " is " + std::to_string(resolution.width) + "x" + std::to_string(resolution.height),
			"got " + std::to_string(gpu.getDisplayWidth()) + "x" + std::to_string(gpu.getDisplayHeight()));
	}
}

// GP1(05h) start of the display area, GP1(07h) vertical display range
void checkDisplayPosition() {
	GPU gpu;
	gpu.writeGP1(0x00000000);
	gpu.writeGP1(0x05000000 | 0x120 | (0x0A5u << 10));
	checkEqual(gpu.getDisplayAreaX(), 0x120, "GP1(05h): display area X");
	checkEqual(gpu.getDisplayAreaY(), 0x0A5, "GP1(05h): display area Y");
	gpu.writeGP1(0x05000000 | 0x3FE | (0x1FFu << 10));
	checkEqual(gpu.getDisplayAreaX(), 0x3FE, "GP1(05h): the largest X");
	checkEqual(gpu.getDisplayAreaY(), 0x1FF, "GP1(05h): the largest Y");

	gpu.writeGP1(0x07000000 | 0x088 | (0x1A8u << 10));
	checkEqual(gpu.getVerticalDisplayStart(), 0x088, "GP1(07h): first line");
	checkEqual(gpu.getVerticalDisplayEnd(), 0x1A8, "GP1(07h): last line");
	gpu.writeGP1(0x07000000 | 0x3FF | (0x3FFu << 10));
	checkEqual(gpu.getVerticalDisplayStart(), 0x3FF, "GP1(07h): the largest first line (10 bits)");
	checkEqual(gpu.getVerticalDisplayEnd(), 0x3FF, "GP1(07h): the largest last line (10 bits)");
}

// GP0(1Fh) requests IRQ1 (GPUSTAT bit 24); GP1(02h) acknowledges it
void checkInterruptRequest() {
	GPU gpu;
	gpu.writeGP1(0x00000000);
	check((idleStatus(gpu) & STATUS_IRQ) == 0, "GPU interrupt: GPUSTAT bit 24 starts clear");
	gpu.writeGP0(0x1F000000);
	check((idleStatus(gpu) & STATUS_IRQ) != 0, "GP0(1Fh) sets GPUSTAT bit 24");
	check(gpu.consumeInterruptRequest(), "GP0(1Fh) hands one interrupt request to the interrupt controller");
	check(!gpu.consumeInterruptRequest(), "the interrupt request is delivered once");
	check((idleStatus(gpu) & STATUS_IRQ) != 0, "GPUSTAT bit 24 stays set until it is acknowledged");
	gpu.writeGP1(0x02000000);
	check((idleStatus(gpu) & STATUS_IRQ) == 0, "GP1(02h) clears GPUSTAT bit 24");
}

// GP1(01h) and GP1(00h) throw away a command that is only partly sent
void checkCommandBufferReset() {
	const uint32_t firstRow = 20;
	const uint32_t secondRow = 40;
	for (uint32_t resetCommand : {0x01000000u, 0x00000000u}) {
		std::string name = resetCommand == 0 ? "GP1(00h)" : "GP1(01h)";

		{   // a fill with its size word missing
			GPU gpu;
			resetAndOpenDrawingArea(gpu);
			send(gpu, {0x02000000 | color24(0xF8, 0, 0), (firstRow << 16) | 0});
			gpu.writeGP1(resetCommand);
			fill(gpu, 0, secondRow, 16, 1, color24(0, 0xF8, 0));
			check(pixel(gpu, 0, firstRow) == 0 && pixel(gpu, 0, secondRow) == color15(0, 31, 0),
				name + " drops a fill that is missing its last word",
				"first fill row " + hex(pixel(gpu, 0, firstRow), 4) + " (expected 0x0000), next fill row " + hex(pixel(gpu, 0, secondRow), 4) + " (expected 0x03E0)");
		}
		{   // a CPU to VRAM transfer whose data has not arrived
			GPU gpu;
			resetAndOpenDrawingArea(gpu);
			send(gpu, {0xA0000000, (firstRow << 16) | 0, (1u << 16) | 4});
			gpu.writeGP1(resetCommand);
			fill(gpu, 0, secondRow, 16, 1, color24(0, 0xF8, 0));
			check(pixel(gpu, 0, secondRow) == color15(0, 31, 0), name + " drops a CPU to VRAM transfer that is waiting for its data",
				"next fill row " + hex(pixel(gpu, 0, secondRow), 4) + " (expected 0x03E0)");
		}
	}
}

// Commands above 3Fh mirror GP1(00h..3Fh) according to the page; the core prints FATAL and exits on one it does not know, so each runs in its own process
void checkCommandMirrors() {
	isolated_probe::Outcome mirror = isolated_probe::run([] {
		GPU gpu;
		gpu.writeGP1(0x00000000);
		gpu.writeGP1(0x43000000);   // GP1(03h) with the display switched on
		return gpu.readGP1() & STATUS_DISPLAY_DISABLED;
	});
	if (!mirror.finished) {
		check(false, "GP1(43h) is a mirror of GP1(03h)", isolated_probe::describeFailure(mirror));
	} else {
		check(mirror.value == 0, "GP1(43h) is a mirror of GP1(03h)", "the display is still off");
	}
}

} // namespace

int main() {
	checkReset();
	checkDisplayEnable();
	checkDmaDirection();
	checkDisplayMode();
	checkDisplayPosition();
	checkInterruptRequest();
	checkCommandBufferReset();
	checkCommandMirrors();
	return echo_check::finish();
}
