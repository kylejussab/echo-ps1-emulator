// Rectangles (GP0 60h-7Fh) drawn into VRAM and checked pixel by pixel against the stable psx-spx page (graphicsprocessingunitgpu.md: "GPU Render
// Rectangle Commands", "GPU Rendering Attributes", "Texture Bitmaps", "Semi-transparency", "Modulation", "GP0(E6h) - Mask Bit Setting").
// Rectangles are the simplest primitive to predict exactly: no edge rules, no shading, no dither. Triangles and lines are left to the
// real-hardware VRAM references of the public tests (tier 3 and 6).
// The GPU is built directly: no Bus, no BIOS, no SDL.
//
// Left out because the page does not give an exact value: the rounding of semi-transparent and modulated 8-bit to 5-bit conversions
// (the inputs below are chosen so that every result is exact), bit 15 of a blended textured pixel, the X/Y-flip bits (v2 only, and the
// emulator has evidence-based quirks for them), sub-pixel sampling of odd texture coordinates.
#include "gpu/GPU.h"
#include "support/echo_check.h"
#include "support/gpu_access.h"
#include <cstdint>
#include <string>
#include <vector>

using namespace gpu_access;
using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;

namespace {

const uint32_t WHITE = color24(0xF8, 0xF8, 0xF8);
const uint16_t WHITE_15 = color15(31, 31, 31);

std::string pixelText(uint16_t value) {
	return hex(value, 4);
}

void drawRectangle(GPU& gpu, uint32_t command, int x, int y, uint32_t width, uint32_t height, uint32_t color) {
	send(gpu, {(command << 24) | color, vertex(x, y), (height << 16) | width});
}

// Every pixel of the window around the rectangle is checked: drawn inside, untouched outside
bool exactlyCovers(const GPU& gpu, int x, int y, uint32_t width, uint32_t height, uint16_t color, std::string& description) {
	for (int row = y - 3; row < y + static_cast<int>(height) + 3; row++) {
		for (int column = x - 3; column < x + static_cast<int>(width) + 3; column++) {
			bool inside = column >= x && column < x + static_cast<int>(width) && row >= y && row < y + static_cast<int>(height);
			uint16_t expected = inside ? color : 0;
			if (pixel(gpu, column, row) != expected) {
				description = "pixel (" + std::to_string(column) + ", " + std::to_string(row) + ") is " + pixelText(pixel(gpu, column, row)) + ", expected " + pixelText(expected);
				return false;
			}
		}
	}
	return true;
}

void checkSizes() {
	struct Size {
		uint32_t command;
		uint32_t width;
		uint32_t height;
		const char* name;
	};
	const Size sizes[] = {{0x68, 1, 1, "1x1"}, {0x70, 8, 8, "8x8"}, {0x78, 16, 16, "16x16"}};
	for (const Size& size : sizes) {
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		send(gpu, {(size.command << 24) | WHITE, vertex(50, 60)});
		std::string description;
		check(exactlyCovers(gpu, 50, 60, size.width, size.height, WHITE_15, description), std::string("GP0(") + hex(size.command, 2).substr(2) + "h) draws a " + size.name + " rectangle, the position being its upper left pixel", description);
	}
	{
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		drawRectangle(gpu, 0x60, 50, 60, 5, 3, WHITE);
		std::string description;
		check(exactlyCovers(gpu, 50, 60, 5, 3, WHITE_15, description), "GP0(60h) draws exactly the width and height of its size word", description);
	}
	{
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		drawRectangle(gpu, 0x60, 0, 60, 1023, 2, WHITE);
		check(pixel(gpu, 0, 60) == WHITE_15 && pixel(gpu, 1022, 60) == WHITE_15 && pixel(gpu, 1023, 60) == 0 && pixel(gpu, 1022, 61) == WHITE_15 && pixel(gpu, 0, 62) == 0,
			"GP0(60h) takes a width of 1023");
		GPU tall;
		resetAndOpenDrawingArea(tall);
		drawRectangle(tall, 0x60, 60, 0, 2, 511, WHITE);
		check(pixel(tall, 60, 0) == WHITE_15 && pixel(tall, 61, 510) == WHITE_15 && pixel(tall, 60, 511) == 0,
			"GP0(60h) takes a height of 511");
	}
}

void checkColor() {
	GPU gpu;
	resetAndOpenDrawingArea(gpu);
	drawRectangle(gpu, 0x60, 20, 20, 2, 2, color24(0xFF, 0x80, 0x07));
	checkEqual(pixel(gpu, 20, 20), color15(31, 16, 0), "rectangle: 8-bit channels become 5-bit by dropping the low 3 bits, bit 15 clear");

	// Rectangles are never dithered. 87h is 16.9 in 5-bit units: a dither would push some pixels to 17.
	GPU dithering;
	resetAndOpenDrawingArea(dithering);
	dithering.writeGP0(0xE1000000 | 0x200);
	drawRectangle(dithering, 0x60, 20, 20, 4, 4, color24(0x87, 0x87, 0x87));
	bool allSame = true;
	for (int y = 20; y < 24; y++) {
		for (int x = 20; x < 24; x++) {
			allSame = allSame && pixel(dithering, x, y) == color15(16, 16, 16);
		}
	}
	check(allSame, "rectangle: the dither bit of GP0(E1h) does not dither rectangles");
}

// Semi-transparency modes (GP0(E1h) bits 5-6). The background and foreground channels are chosen so every result is exact.
void checkSemiTransparency() {
	// 5-bit channels: background (16, 8, 24), foreground (8, 24, 4)
	struct Mode {
		uint32_t value;
		uint16_t expected;
		const char* name;
	};
	const Mode modes[] = {
		{0, color15(12, 16, 14), "B/2 + F/2"},
		{1, color15(24, 31, 28), "B + F (clamped to 31)"},
		{2, color15(8, 0, 20), "B - F (clamped to 0)"},
		{3, color15(18, 14, 25), "B + F/4"},
	};
	for (const Mode& mode : modes) {
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		fill(gpu, 64, 64, 16, 4, color24(16 * 8, 8 * 8, 24 * 8));
		gpu.writeGP0(0xE1000000 | (mode.value << 5));
		drawRectangle(gpu, 0x62, 64, 64, 4, 4, color24(8 * 8, 24 * 8, 4 * 8));
		checkEqual(pixel(gpu, 65, 65), mode.expected, std::string("semi-transparent rectangle, mode ") + std::to_string(mode.value) + " (" + mode.name + ")");
	}
	{   // an opaque rectangle ignores the mode
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		fill(gpu, 64, 64, 16, 4, color24(16 * 8, 8 * 8, 24 * 8));
		gpu.writeGP0(0xE1000000 | (1u << 5));
		drawRectangle(gpu, 0x60, 64, 64, 4, 4, color24(8 * 8, 24 * 8, 4 * 8));
		checkEqual(pixel(gpu, 65, 65), color15(8, 24, 4), "an opaque rectangle replaces the pixel whatever the semi-transparency mode");
	}
}

// GP0(E6h) acts on rendering commands too
void checkMaskBit() {
	{
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		gpu.writeGP0(0xE6000001);
		drawRectangle(gpu, 0x60, 30, 30, 2, 2, WHITE);
		checkEqual(pixel(gpu, 30, 30), 0xFFFF, "mask bit: a rectangle sets bit 15 when GP0(E6h) bit 0 is set");
	}
	{
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, 30, 30, 1, 1, {0x8123});
		gpu.writeGP0(0xE6000002);
		drawRectangle(gpu, 0x60, 30, 30, 2, 1, WHITE);
		check(pixel(gpu, 30, 30) == 0x8123 && pixel(gpu, 31, 30) == WHITE_15, "mask bit: a rectangle does not overwrite a pixel whose bit 15 is set when GP0(E6h) bit 1 is set",
			"pixels " + pixelText(pixel(gpu, 30, 30)) + " " + pixelText(pixel(gpu, 31, 30)));
	}
}

const uint32_t TEXTURE_PAGE_X = 640;                      // texture page 10, in halfwords
const uint32_t TEXTURE_PAGE_15_BIT = 0x00A | (2 << 7);    // page 10, 15-bit colours
const uint32_t TEXTURE_PAGE_8_BIT = 0x00A | (1 << 7);
const uint32_t TEXTURE_PAGE_4_BIT = 0x00A;

// GP0(60h) with bit 26 (textured) and bit 24 (raw): the UV word has the CLUT in its upper half
void drawTexturedRectangle(GPU& gpu, uint32_t command, int x, int y, uint32_t width, uint32_t height, uint32_t clutWord, uint32_t color = WHITE) {
	send(gpu, {(command << 24) | color, vertex(x, y), clutWord, (height << 16) | width});
}

uint32_t clutPosition(uint32_t x, uint32_t y) {
	return ((y << 6) | (x / 16)) << 16;
}

void checkRawTextures() {
	{   // 15-bit texture: the texel is copied; 0000h is transparent; 8000h is opaque black for an opaque command
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		std::vector<uint16_t> texels = {0x1111, 0x2222, 0x3333, 0x0000, 0x8000, 0x7FFF, 0x0421, 0x5555};
		upload(gpu, TEXTURE_PAGE_X, 0, 8, 1, texels);
		fill(gpu, 96, 100, 16, 1, color24(0x40, 0x40, 0x40));
		gpu.writeGP0(0xE1000000 | TEXTURE_PAGE_15_BIT);
		drawTexturedRectangle(gpu, 0x65, 96, 100, 8, 1, 0);
		bool copied = true;
		std::string description;
		for (uint32_t index = 0; index < 8; index++) {
			uint16_t expected = texels[index] == 0 ? color15(8, 8, 8) : texels[index];
			if (pixel(gpu, 96 + index, 100) != expected) {
				copied = false;
				description = "pixel " + std::to_string(index) + " is " + pixelText(pixel(gpu, 96 + index, 100)) + ", expected " + pixelText(expected);
				break;
			}
		}
		check(copied, "raw 15-bit texture: texels are copied, 0000h is transparent, 8000h is drawn as opaque black", description);
	}
	{   // 4-bit texture with a 16-entry palette (the CLUT)
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		std::vector<uint16_t> palette;
		for (uint32_t index = 0; index < 16; index++) {
			palette.push_back(index == 0 ? 0 : static_cast<uint16_t>(0x0100 + index * 0x0111));
		}
		upload(gpu, 0, 480, 16, 1, palette);
		upload(gpu, TEXTURE_PAGE_X, 0, 4, 1, {0x4321, 0x8765, 0xCBA9, 0x0FED});   // palette indices 1..15 then 0
		fill(gpu, 96, 100, 16, 1, color24(0x40, 0x40, 0x40));
		gpu.writeGP0(0xE1000000 | TEXTURE_PAGE_4_BIT);
		drawTexturedRectangle(gpu, 0x65, 96, 100, 16, 1, clutPosition(0, 480));
		bool copied = true;
		std::string description;
		for (uint32_t index = 0; index < 16; index++) {
			uint16_t expected = index == 15 ? color15(8, 8, 8) : palette[index + 1];
			if (pixel(gpu, 96 + index, 100) != expected) {
				copied = false;
				description = "pixel " + std::to_string(index) + " is " + pixelText(pixel(gpu, 96 + index, 100)) + ", expected " + pixelText(expected);
				break;
			}
		}
		check(copied, "raw 4-bit texture: four texels per halfword, low nibble first, looked up in the CLUT; index 0 (colour 0000h) is transparent", description);
	}
	{   // 8-bit texture with a 256-entry palette
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		std::vector<uint16_t> palette;
		for (uint32_t index = 0; index < 256; index++) {
			palette.push_back(index == 0 ? 0 : static_cast<uint16_t>(0x0100 + index * 0x0021));
		}
		upload(gpu, 0, 481, 256, 1, palette);
		upload(gpu, TEXTURE_PAGE_X, 0, 2, 1, {0x0B0A, 0xFF0C});   // indices 0Ah, 0Bh, 0Ch, FFh
		gpu.writeGP0(0xE1000000 | TEXTURE_PAGE_8_BIT);
		drawTexturedRectangle(gpu, 0x65, 100, 100, 4, 1, clutPosition(0, 481));
		check(pixel(gpu, 100, 100) == palette[0x0A] && pixel(gpu, 101, 100) == palette[0x0B] && pixel(gpu, 102, 100) == palette[0x0C] && pixel(gpu, 103, 100) == palette[0xFF],
			"raw 8-bit texture: two texels per halfword, low byte first, looked up in the CLUT",
			"pixels " + pixelText(pixel(gpu, 100, 100)) + " " + pixelText(pixel(gpu, 101, 100)) + " " + pixelText(pixel(gpu, 102, 100)) + " " + pixelText(pixel(gpu, 103, 100)));
	}
}

// GP0(E2h): texcoord = (texcoord AND NOT (mask * 8)) OR ((offset AND mask) * 8)
void checkTextureWindow() {
	std::vector<uint16_t> texels;
	for (uint32_t index = 0; index < 16; index++) {
		texels.push_back(static_cast<uint16_t>(0x0100 + index * 0x0101));
	}
	{   // X: mask 1 (the 8-texel bit), offset 1: every U gets bit 3 set, so the second half of the 16 texels repeats
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, TEXTURE_PAGE_X, 0, 16, 1, texels);
		gpu.writeGP0(0xE1000000 | TEXTURE_PAGE_15_BIT);
		gpu.writeGP0(0xE2000000 | 1 | (3u << 10));   // offset 3, but only the bit inside the mask counts
		drawTexturedRectangle(gpu, 0x65, 100, 100, 16, 1, 0);
		bool right = true;
		std::string description;
		for (uint32_t index = 0; index < 16 && right; index++) {
			uint16_t expected = texels[index | 8];
			if (pixel(gpu, 100 + index, 100) != expected) {
				right = false;
				description = "pixel " + std::to_string(index) + " is " + pixelText(pixel(gpu, 100 + index, 100)) + ", expected " + pixelText(expected);
			}
		}
		check(right, "texture window: mask X = 1, offset X = 3 repeats texels 8-15 (the offset is ANDed with the mask)", description);
	}
	{   // Y: the same on the vertical axis
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		for (uint32_t row = 0; row < 16; row++) {
			upload(gpu, TEXTURE_PAGE_X, row, 1, 1, {texels[row]});
		}
		gpu.writeGP0(0xE1000000 | TEXTURE_PAGE_15_BIT);
		gpu.writeGP0(0xE2000000 | (1u << 5) | (3u << 15));
		drawTexturedRectangle(gpu, 0x65, 100, 100, 1, 16, 0);
		bool right = true;
		std::string description;
		for (uint32_t row = 0; row < 16 && right; row++) {
			uint16_t expected = texels[row | 8];
			if (pixel(gpu, 100, 100 + row) != expected) {
				right = false;
				description = "line " + std::to_string(row) + " is " + pixelText(pixel(gpu, 100, 100 + row)) + ", expected " + pixelText(expected);
			}
		}
		check(right, "texture window: mask Y = 1, offset Y = 3 repeats lines 8-15 (the offset is ANDed with the mask)", description);
	}
}

// Modulation: channel = texel * colour / 128 in 8-bit units, so colour 80h leaves the texel as it is
void checkModulation() {
	struct Case {
		uint32_t colorChannel;
		uint16_t texel;
		uint16_t expected;
		const char* name;
	};
	const Case cases[] = {
		{0x80, color15(16, 16, 16), color15(16, 16, 16), "colour 80h leaves the texel as it is"},
		{0x40, color15(16, 16, 16), color15(8, 8, 8), "colour 40h halves the texel"},
		{0xC0, color15(16, 16, 16), color15(24, 24, 24), "colour C0h makes the texel 1.5 times brighter"},
		{0xFF, color15(20, 20, 20), color15(31, 31, 31), "colour FFh saturates at 31"},
	};
	for (const Case& testCase : cases) {
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, TEXTURE_PAGE_X, 0, 1, 1, {testCase.texel});
		gpu.writeGP0(0xE1000000 | TEXTURE_PAGE_15_BIT);
		drawTexturedRectangle(gpu, 0x64, 100, 100, 1, 1, 0, color24(testCase.colorChannel, testCase.colorChannel, testCase.colorChannel));
		checkEqual(pixel(gpu, 100, 100), testCase.expected, std::string("modulation: ") + testCase.name);
	}
	{   // each channel on its own
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, TEXTURE_PAGE_X, 0, 1, 1, {color15(16, 20, 10)});
		gpu.writeGP0(0xE1000000 | TEXTURE_PAGE_15_BIT);
		drawTexturedRectangle(gpu, 0x64, 100, 100, 1, 1, 0, color24(0x40, 0xFF, 0x80));
		checkEqual(pixel(gpu, 100, 100), color15(8, 31, 10), "modulation: red, green and blue are each modulated by their own colour channel");
	}
}

// A textured semi-transparent command blends only the texels whose bit 15 is set
void checkTexturedSemiTransparency() {
	GPU gpu;
	resetAndOpenDrawingArea(gpu);
	uint16_t blended = color15(8, 24, 4) | 0x8000;
	uint16_t opaque = color15(8, 24, 4);
	upload(gpu, TEXTURE_PAGE_X, 0, 2, 1, {blended, opaque});
	fill(gpu, 96, 100, 16, 1, color24(16 * 8, 8 * 8, 24 * 8));
	gpu.writeGP0(0xE1000000 | TEXTURE_PAGE_15_BIT | (1u << 5));   // B + F
	drawTexturedRectangle(gpu, 0x67, 96, 100, 2, 1, 0);
	check((pixel(gpu, 96, 100) & 0x7FFF) == color15(24, 31, 28), "textured semi-transparent: a texel with bit 15 set is blended with the background",
		"pixel " + pixelText(pixel(gpu, 96, 100)));
	check((pixel(gpu, 97, 100) & 0x7FFF) == color15(8, 24, 4), "textured semi-transparent: a texel with bit 15 clear is drawn as it is",
		"pixel " + pixelText(pixel(gpu, 97, 100)));
}

} // namespace

int main() {
	checkSizes();
	checkColor();
	checkSemiTransparency();
	checkMaskBit();
	checkRawTextures();
	checkTextureWindow();
	checkModulation();
	checkTexturedSemiTransparency();
	return echo_check::finish();
}
