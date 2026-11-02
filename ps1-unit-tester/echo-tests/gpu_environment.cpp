// The GP0 environment commands E1h-E6h and what they do, against the stable psx-spx page (graphicsprocessingunitgpu.md: "GPU Rendering
// Attributes", "GPU Status Register", "GP1(10h) - Read GPU internal register"): how they show up in GPUSTAT, how GP1(10h) reads them back,
// and how the drawing area and drawing offset act on rectangles (and do not act on fills and VRAM transfers).
// The GPU is built directly: no Bus, no BIOS, no SDL.
//
// The emulator reports itself as a v2 GPU, so the v2 column of the page applies: 20-bit drawing area reads, version 2, index 8 = 0.
// Left out because the page says "not sure" or "unknown": GP0(E1h) bit 10 (drawing to the display area), the texture-disable bit 15.
#include "gpu/GPU.h"
#include "support/echo_check.h"
#include "support/gpu_access.h"
#include <cstdint>
#include <string>

using namespace gpu_access;
using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;

namespace {

const uint32_t WHITE = color24(0xF8, 0xF8, 0xF8);   // 5-bit 31 per channel
const uint16_t WHITE_15 = color15(31, 31, 31);

// GP0(60h): a flat rectangle with its size in the third word
void drawRectangle(GPU& gpu, int x, int y, uint32_t width, uint32_t height, uint32_t color) {
	send(gpu, {0x60000000u | color, vertex(x, y), (height << 16) | width});
}

// The first pixel inside [x0, x0 + width) x [y0, y0 + height) whose "drawn" state differs from what the rule predicts; returns false when all agree
bool firstMismatch(const GPU& gpu, uint32_t x0, uint32_t y0, uint32_t width, uint32_t height, uint16_t drawnColor,
				bool (*shouldBeDrawn)(uint32_t, uint32_t), std::string& description) {
	for (uint32_t y = y0; y < y0 + height; y++) {
		for (uint32_t x = x0; x < x0 + width; x++) {
			bool drawn = pixel(gpu, x, y) == drawnColor;
			if (drawn != shouldBeDrawn(x, y)) {
				description = "pixel (" + std::to_string(x) + ", " + std::to_string(y) + ") is " + (drawn ? "drawn" : "not drawn") + ", should be " + (drawn ? "not drawn" : "drawn");
				return true;
			}
		}
	}
	return false;
}

// GPUSTAT bits 0-10 are a copy of GP0(E1h) bits 0-10, and nothing else of E1 reaches the status
void checkDrawModeInStatus() {
	for (uint32_t drawMode : {0x000u, 0x7FFu, 0x155u, 0x2AAu, 0x1E0u, 0x41Fu}) {
		GPU gpu;
		gpu.writeGP1(0x00000000);
		gpu.writeGP0(0xE1000000 | drawMode);
		uint32_t status = idleStatus(gpu);
		checkEqual(status & STATUS_DRAW_MODE_BITS, drawMode, "GP0(E1h) = " + hex(drawMode, 3) + ": bits 0-10 are in GPUSTAT bits 0-10");
	}

	GPU gpu;
	gpu.writeGP1(0x00000000);
	uint32_t before = idleStatus(gpu);
	gpu.writeGP0(0xE1000000 | 0x3800);   // E1 bits 11, 12, 13
	uint32_t after = idleStatus(gpu);
	check((after & ~STATUS_DRAW_MODE_BITS) == (before & ~STATUS_DRAW_MODE_BITS), "GP0(E1h) bits 11-13 do not appear in GPUSTAT bits 11-13", "status went from " + hex(before) + " to " + hex(after));
}

// GP0(E6h): bit 0 shows in GPUSTAT bit 11, bit 1 in bit 12
void checkMaskBitInStatus() {
	for (uint32_t setting = 0; setting < 4; setting++) {
		GPU gpu;
		gpu.writeGP1(0x00000000);
		gpu.writeGP0(0xE6000000 | setting);
		uint32_t status = idleStatus(gpu);
		uint32_t expected = (setting & 1 ? STATUS_MASK_SET : 0) | (setting & 2 ? STATUS_MASK_CHECK : 0);
		check((status & (STATUS_MASK_SET | STATUS_MASK_CHECK)) == expected, "GP0(E6h) = " + std::to_string(setting) + ": bits 0-1 are in GPUSTAT bits 11-12",
			"status bits 11-12 are " + hex(status & (STATUS_MASK_SET | STATUS_MASK_CHECK)) + ", expected " + hex(expected));
	}

	GPU gpu;
	gpu.writeGP1(0x00000000);
	gpu.writeGP0(0xE6000003);
	gpu.writeGP0(0xE1000000 | 0x7FF);
	check((idleStatus(gpu) & (STATUS_MASK_SET | STATUS_MASK_CHECK)) == (STATUS_MASK_SET | STATUS_MASK_CHECK), "a later GP0(E1h) leaves the mask bits of GPUSTAT alone");
}

// GP1(10h): the selected register is latched and read back from GP0
uint32_t readInfo(GPU& gpu, uint32_t index) {
	gpu.writeGP1(0x10000000 | index);
	return gpu.readGP0();
}

void checkGpuInfo() {
	GPU gpu;
	gpu.writeGP1(0x00000000);

	checkEqual(readInfo(gpu, 7), 2, "GP1(10h) index 7 reads the GPU version (2)");
	checkEqual(readInfo(gpu, 8), 0, "GP1(10h) index 8 reads zero");
	checkEqual(gpu.readGP0(), 0, "the latched value can be read again");

	readInfo(gpu, 7);
	gpu.writeGP1(0x10000006);   // "returns nothing": the old value stays
	checkEqual(gpu.readGP0(), 2, "GP1(10h) index 6 leaves the latched value alone");
	gpu.writeGP1(0x10000000);
	checkEqual(gpu.readGP0(), 2, "GP1(10h) index 0 leaves the latched value alone");

	gpu.writeGP0(0xE2000000 | 0x12345);
	checkEqual(readInfo(gpu, 2), 0x12345, "GP1(10h) index 2 reads the texture window (GP0(E2h), 20 bits)");

	gpu.writeGP0(0xE3000000 | 0x123 | (0x0A5u << 10));
	checkEqual(readInfo(gpu, 3), 0x123u | (0x0A5u << 10), "GP1(10h) index 3 reads the drawing area top left (GP0(E3h), 20 bits)");

	gpu.writeGP0(0xE4000000 | 0x2F0 | (0x1E0u << 10));
	checkEqual(readInfo(gpu, 4), 0x2F0u | (0x1E0u << 10), "GP1(10h) index 4 reads the drawing area bottom right (GP0(E4h), 20 bits)");

	gpu.writeGP0(0xE5000000 | 0x7FD | (17u << 11));
	checkEqual(readInfo(gpu, 5), 0x7FDu | (17u << 11), "GP1(10h) index 5 reads the drawing offset (GP0(E5h), 22 bits)");

	checkEqual(readInfo(gpu, 0x12), 0x12345, "GP1(10h) index 12h is a mirror of index 2");
	checkEqual(readInfo(gpu, 0x17), 2, "GP1(10h) index 17h is a mirror of index 7");
}

bool insideTenByTen(uint32_t x, uint32_t y) {
	return x >= 10 && x <= 19 && y >= 20 && y <= 29;
}

// The drawing area clips rectangles, and its right and bottom edge are inside the area
void checkDrawingArea() {
	{
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		gpu.writeGP0(0xE3000000 | 10 | (20u << 10));
		gpu.writeGP0(0xE4000000 | 19 | (29u << 10));
		drawRectangle(gpu, 0, 0, 40, 40, WHITE);
		std::string description;
		check(!firstMismatch(gpu, 0, 0, 40, 40, WHITE_15, insideTenByTen, description), "a rectangle is clipped to the drawing area (corners (10, 20) and (19, 29) included)", description);
	}
	{   // a one pixel area
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		gpu.writeGP0(0xE3000000 | 50 | (60u << 10));
		gpu.writeGP0(0xE4000000 | 50 | (60u << 10));
		drawRectangle(gpu, 40, 50, 20, 20, WHITE);
		check(pixel(gpu, 50, 60) == WHITE_15 && pixel(gpu, 51, 60) == 0 && pixel(gpu, 50, 61) == 0 && pixel(gpu, 49, 60) == 0 && pixel(gpu, 50, 59) == 0,
			"a drawing area of one pixel lets exactly that pixel through");
	}
}

bool rectangleAtThirtyForty(uint32_t x, uint32_t y) {
	return x >= 30 && x <= 33 && y >= 40 && y <= 43;
}

bool rectangleAtFiveFive(uint32_t x, uint32_t y) {
	return x >= 5 && x <= 7 && y >= 5 && y <= 7;
}

bool rectangleAtNineTen(uint32_t x, uint32_t y) {
	return x >= 9 && x <= 11 && y >= 10 && y <= 12;
}

// The drawing offset (signed, 11 bits) is added to the coordinates of rectangles
void checkDrawingOffset() {
	std::string description;
	{
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		gpu.writeGP0(0xE5000000 | 30 | (40u << 11));
		drawRectangle(gpu, 0, 0, 4, 4, WHITE);
		check(!firstMismatch(gpu, 0, 0, 64, 64, WHITE_15, rectangleAtThirtyForty, description), "the drawing offset (30, 40) moves a rectangle at (0, 0) to (30, 40)", description);
	}
	{
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		gpu.writeGP0(0xE5000000 | (static_cast<uint32_t>(-5) & 0x7FF) | ((static_cast<uint32_t>(-5) & 0x7FF) << 11));
		drawRectangle(gpu, 10, 10, 3, 3, WHITE);
		check(!firstMismatch(gpu, 0, 0, 32, 32, WHITE_15, rectangleAtFiveFive, description), "a negative drawing offset (-5, -5) moves a rectangle at (10, 10) to (5, 5)", description);
	}
	{   // all eleven bits set is -1
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		gpu.writeGP0(0xE5000000 | 0x7FF);
		drawRectangle(gpu, 10, 10, 3, 3, WHITE);
		check(!firstMismatch(gpu, 0, 0, 32, 32, WHITE_15, rectangleAtNineTen, description), "a drawing offset X of 7FFh is -1", description);
	}
}

// Fills and VRAM transfers use absolute VRAM coordinates: not clipped to the drawing area, not moved by the drawing offset
void checkTransfersIgnoreTheEnvironment() {
	GPU gpu;
	resetAndOpenDrawingArea(gpu);
	gpu.writeGP0(0xE3000000 | 10 | (10u << 10));
	gpu.writeGP0(0xE4000000 | 20 | (20u << 10));
	gpu.writeGP0(0xE5000000 | 30 | (40u << 11));

	fill(gpu, 96, 100, 16, 2, color24(0xF8, 0, 0));
	check(pixel(gpu, 96, 100) == color15(31, 0, 0) && pixel(gpu, 111, 101) == color15(31, 0, 0) && pixel(gpu, 126, 140) == 0,
		"a fill ignores the drawing area and the drawing offset");

	upload(gpu, 200, 200, 2, 1, {0x1234, 0x5678});
	check(pixel(gpu, 200, 200) == 0x1234 && pixel(gpu, 201, 200) == 0x5678 && pixel(gpu, 230, 240) == 0,
		"a CPU to VRAM transfer ignores the drawing area and the drawing offset");

	send(gpu, {0x80000000, (200u << 16) | 200, (300u << 16) | 300, (1u << 16) | 2});
	check(pixel(gpu, 300, 300) == 0x1234 && pixel(gpu, 301, 300) == 0x5678 && pixel(gpu, 330, 340) == 0,
		"a VRAM to VRAM copy ignores the drawing area and the drawing offset");
}

} // namespace

int main() {
	checkDrawModeInStatus();
	checkMaskBitInStatus();
	checkGpuInfo();
	checkDrawingArea();
	checkDrawingOffset();
	checkTransfersIgnoreTheEnvironment();
	return echo_check::finish();
}
