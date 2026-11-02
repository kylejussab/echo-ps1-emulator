// The VRAM commands: GP0(02h) quick fill, GP0(A0h) CPU to VRAM, GP0(C0h) VRAM to CPU, GP0(80h) VRAM to VRAM, against the stable psx-spx page
// (graphicsprocessingunitgpu.md: "Quick Rectangle Fill", "GPU Memory Transfer Commands", "Masking and Rounding", "Wrapping", "GP0(E6h) - Mask Bit
// Setting"). The GPU is built directly: no Bus, no BIOS, no SDL.
//
// Where a transfer would go wrong the commands that follow it are swallowed or misread, so after each transfer a marker fill is sent and
// checked: it proves the GPU took exactly the words the command needs.
// Left out because the page gives no value: overlapping copies, how long a transfer takes, the old v0 fill bug for x = 0Fh.
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

const uint16_t MARKER_15 = color15(0, 31, 0);

// A fill that only works if the GPU is ready for a new command; returns whether its first pixel appeared
bool markerWorks(GPU& gpu, uint32_t row) {
	fill(gpu, 512, row, 16, 1, color24(0, 0xF8, 0));
	return pixel(gpu, 512, row) == MARKER_15;
}

std::string pixelText(uint16_t value) {
	return hex(value, 4);
}

// 16-bit pattern for pixel number `index`: never zero, bit 15 clear
uint16_t patternPixel(uint32_t index) {
	return static_cast<uint16_t>(0x0100 + (index * 37) % 0x7000);
}

void checkFill() {
	GPU gpu;
	resetAndOpenDrawingArea(gpu);

	fill(gpu, 0, 0, 16, 1, color24(0xF8, 0x80, 0x08));
	checkEqual(pixel(gpu, 0, 0), color15(31, 16, 1), "fill: 8-bit color becomes 5-bit with bit 15 clear");
	fill(gpu, 0, 1, 16, 1, color24(0x07, 0x0F, 0x17));
	checkEqual(pixel(gpu, 0, 1), color15(0, 1, 2), "fill: the low 3 bits of each channel are dropped");

	// x counts in steps of 16 (the low 4 bits are ignored), width rounds up to a multiple of 16
	fill(gpu, 0x13, 5, 1, 1, color24(0xF8, 0, 0));
	check(pixel(gpu, 15, 5) == 0 && pixel(gpu, 16, 5) == color15(31, 0, 0) && pixel(gpu, 31, 5) == color15(31, 0, 0) && pixel(gpu, 32, 5) == 0,
		"fill: x = 13h starts at 10h, width 1 fills 16 pixels");
	fill(gpu, 64, 6, 17, 1, color24(0xF8, 0, 0));
	check(pixel(gpu, 64, 6) != 0 && pixel(gpu, 95, 6) != 0 && pixel(gpu, 96, 6) == 0, "fill: width 17 fills 32 pixels");
	fill(gpu, 128, 7, 16, 1, color24(0xF8, 0, 0));
	check(pixel(gpu, 143, 7) != 0 && pixel(gpu, 144, 7) == 0, "fill: width 16 fills 16 pixels");

	// width 0, width 400h and height 0 fill nothing; 3F1h-3FFh round up to 400h (the whole row)
	fill(gpu, 0, 20, 0, 1, color24(0xF8, 0, 0));
	check(pixel(gpu, 0, 20) == 0, "fill: width 0 fills nothing");
	fill(gpu, 0, 21, 0x400, 1, color24(0xF8, 0, 0));
	check(pixel(gpu, 0, 21) == 0, "fill: width 400h is handled as width 0 and fills nothing");
	fill(gpu, 0, 22, 16, 0, color24(0xF8, 0, 0));
	check(pixel(gpu, 0, 22) == 0, "fill: height 0 fills nothing");
	fill(gpu, 0, 23, 0x3F1, 1, color24(0xF8, 0, 0));
	check(pixel(gpu, 0, 23) != 0 && pixel(gpu, 1023, 23) != 0, "fill: width 3F1h rounds up to 400h and fills the whole row");

	// positions and sizes are masked: x to 3F0h, y to 1FFh, height to 1FFh
	fill(gpu, 0x403, 24, 16, 1, color24(0xF8, 0, 0));
	check(pixel(gpu, 0, 24) != 0, "fill: x = 403h is masked to 3F0h-aligned 0");
	fill(gpu, 0, 0x205, 16, 1, color24(0xF8, 0, 0));
	check(pixel(gpu, 0, 5) != 0, "fill: y = 205h is masked to 5");
	fill(gpu, 256, 100, 16, 0x205, color24(0xF8, 0, 0));
	check(pixel(gpu, 256, 104) != 0 && pixel(gpu, 256, 105) == 0, "fill: height 205h is masked to 5 rows");

	// wrapping, without a carry from X to Y or from Y to X
	fill(gpu, 0x3F0, 30, 32, 1, color24(0xF8, 0, 0));
	check(pixel(gpu, 0x3FF, 30) != 0 && pixel(gpu, 0, 30) != 0 && pixel(gpu, 15, 30) != 0 && pixel(gpu, 16, 30) == 0 && pixel(gpu, 0, 31) == 0,
		"fill: a fill past the right edge wraps to the left edge of the same line");
	fill(gpu, 600, 510, 16, 4, color24(0xF8, 0, 0));
	check(pixel(gpu, 600, 510) != 0 && pixel(gpu, 600, 511) != 0 && pixel(gpu, 600, 0) != 0 && pixel(gpu, 600, 1) != 0 && pixel(gpu, 600, 2) == 0,
		"fill: a fill past the bottom edge wraps to the top edge");
}

// The mask setting does not affect fills
void checkFillIgnoresMask() {
	GPU gpu;
	resetAndOpenDrawingArea(gpu);
	upload(gpu, 300, 50, 1, 1, {0x8123});
	gpu.writeGP0(0xE6000003);
	fill(gpu, 288, 50, 16, 1, color24(0xF8, 0, 0));
	checkEqual(pixel(gpu, 300, 50), color15(31, 0, 0), "fill: GP0(E6h) does not protect pixels and does not set bit 15");
}

void checkCpuToVram() {
	{
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, 100, 200, 2, 2, {0x1111, 0x2222, 0x3333, 0x4444});
		check(pixel(gpu, 100, 200) == 0x1111 && pixel(gpu, 101, 200) == 0x2222 && pixel(gpu, 100, 201) == 0x3333 && pixel(gpu, 101, 201) == 0x4444,
			"A0: pixels arrive row by row, the low halfword of each word first");
		check(markerWorks(gpu, 300), "A0: the GPU is ready for the next command after a 2x2 transfer");
	}
	{   // an odd pixel count is padded to a whole word; the padding is not written
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, 100, 200, 3, 1, {0x1111, 0x2222, 0x3333});
		check(pixel(gpu, 102, 200) == 0x3333 && pixel(gpu, 103, 200) == 0, "A0: an odd pixel count writes only the pixels asked for");
		check(markerWorks(gpu, 300), "A0: the GPU is ready for the next command after a 3x1 transfer");
	}
	{   // x and y are masked to 10 and 9 bits
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, 0x405, 0x207, 1, 1, {0x1357});
		checkEqual(pixel(gpu, 5, 7), 0x1357, "A0: position (405h, 207h) is masked to (5, 7)");
		check(markerWorks(gpu, 300), "A0: the GPU is ready for the next command after a masked position");
	}
	{   // wrapping at the right and bottom edge, no carry between X and Y
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, 1022, 40, 4, 1, {0x1111, 0x2222, 0x3333, 0x4444});
		check(pixel(gpu, 1022, 40) == 0x1111 && pixel(gpu, 1023, 40) == 0x2222 && pixel(gpu, 0, 40) == 0x3333 && pixel(gpu, 1, 40) == 0x4444 && pixel(gpu, 0, 41) == 0,
			"A0: a transfer past the right edge wraps to the left edge of the same line");
		upload(gpu, 60, 510, 1, 4, {0x1111, 0x2222, 0x3333, 0x4444});
		check(pixel(gpu, 60, 510) == 0x1111 && pixel(gpu, 60, 511) == 0x2222 && pixel(gpu, 60, 0) == 0x3333 && pixel(gpu, 60, 1) == 0x4444,
			"A0: a transfer past the bottom edge wraps to the top edge");
	}
	{   // a size of 0 means the maximum; sizes wrap (1025 is 1)
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		send(gpu, {0xA0000000, (100u << 16) | 0, (1u << 16) | 0});   // 1024 x 1: 512 words of data
		for (uint32_t word = 0; word < 512; word++) {
			gpu.writeGP0(0x1000 + word);   // even pixel 1000h + n, odd pixel 0
		}
		check(pixel(gpu, 0, 100) == 0x1000 && pixel(gpu, 1022, 100) == 0x1000 + 511 && pixel(gpu, 1023, 100) == 0, "A0: width 0 transfers 1024 pixels",
			"pixel 0 is " + pixelText(pixel(gpu, 0, 100)) + ", pixel 1022 is " + pixelText(pixel(gpu, 1022, 100)));
		check(markerWorks(gpu, 300), "A0: the GPU is ready for the next command after a width 0 transfer");
	}
	{
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		send(gpu, {0xA0000000, (100u << 16) | 40, (0u << 16) | 1});   // 1 x 512: 256 words
		for (uint32_t word = 0; word < 256; word++) {
			gpu.writeGP0(0x2000 + word);   // even row 2000h + n, odd row 0
		}
		check(pixel(gpu, 40, 100) == 0x2000 && pixel(gpu, 40, 100 + 510) == 0x2000 + 255 && pixel(gpu, 40, 100 + 511) == 0, "A0: height 0 transfers 512 lines",
			"line 0 is " + pixelText(pixel(gpu, 40, 100)));
		check(markerWorks(gpu, 300), "A0: the GPU is ready for the next command after a height 0 transfer");
	}
	{
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		send(gpu, {0xA0000000, (100u << 16) | 40, (1u << 16) | 1025});   // 1025 wraps to 1: one pixel, one word
		gpu.writeGP0(0x00005555);
		check(pixel(gpu, 40, 100) == 0x5555 && pixel(gpu, 41, 100) == 0, "A0: width 1025 wraps to 1");
		check(markerWorks(gpu, 300), "A0: the GPU is ready for the next command after a width 1025 transfer");
	}
}

void checkVramToCpu() {
	{
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, 100, 200, 3, 1, {0x1111, 0x2222, 0x3333});
		send(gpu, {0xC0000000, (200u << 16) | 100, (1u << 16) | 3});
		check((idleStatus(gpu) & STATUS_READY_TO_SEND_VRAM) != 0, "C0: GPUSTAT bit 27 is set while the data is waiting");
		uint32_t first = gpu.readGP0();
		uint32_t second = gpu.readGP0();
		check(first == 0x22221111 && (second & 0xFFFF) == 0x3333, "C0: a word carries two pixels, the left one in the low halfword",
			"words " + hex(first) + " " + hex(second));
		check((idleStatus(gpu) & STATUS_READY_TO_SEND_VRAM) == 0, "C0: GPUSTAT bit 27 clears when the last word has been read", "status " + hex(idleStatus(gpu)));
	}
	{   // what A0 wrote comes back, for several shapes
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		const uint32_t shapes[][2] = {{1, 1}, {2, 3}, {7, 5}, {16, 16}, {33, 2}};
		for (const auto& shape : shapes) {
			std::vector<uint16_t> pixels;
			for (uint32_t index = 0; index < shape[0] * shape[1]; index++) {
				pixels.push_back(patternPixel(index));
			}
			upload(gpu, 200, 100, shape[0], shape[1], pixels);
			check(download(gpu, 200, 100, shape[0], shape[1]) == pixels, "C0: a " + std::to_string(shape[0]) + "x" + std::to_string(shape[1]) + " block reads back what A0 wrote");
		}
	}
	{   // wrap and masks
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, 1022, 40, 4, 1, {0x1111, 0x2222, 0x3333, 0x4444});
		check(download(gpu, 1022, 40, 4, 1) == std::vector<uint16_t>({0x1111, 0x2222, 0x3333, 0x4444}), "C0: a read past the right edge wraps to the left edge of the same line");
		upload(gpu, 5, 7, 1, 1, {0x1357});
		check(download(gpu, 0x405, 0x207, 1, 1) == std::vector<uint16_t>({0x1357}), "C0: position (405h, 207h) is masked to (5, 7)");
	}
	{   // a size of 0 means the maximum
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		std::vector<uint16_t> row;
		for (uint32_t index = 0; index < 1024; index++) {
			row.push_back(patternPixel(index));
		}
		upload(gpu, 0, 100, 1024, 1, row);
		send(gpu, {0xC0000000, (100u << 16) | 0, (1u << 16) | 0});
		uint32_t first = gpu.readGP0();
		uint32_t last = 0;
		for (int word = 1; word < 512; word++) {
			last = gpu.readGP0();
		}
		check(first == (static_cast<uint32_t>(row[1]) << 16 | row[0]) && last == (static_cast<uint32_t>(row[1023]) << 16 | row[1022]), "C0: width 0 reads 1024 pixels",
			"first word " + hex(first) + ", last word " + hex(last));
	}
}

void checkVramToVram() {
	GPU gpu;
	resetAndOpenDrawingArea(gpu);
	upload(gpu, 10, 20, 4, 2, {0x0101, 0x0202, 0x0303, 0x0404, 0x0505, 0x0606, 0x0707, 0x0808});
	send(gpu, {0x80000000, (20u << 16) | 10, (120u << 16) | 300, (2u << 16) | 4});
	check(download(gpu, 300, 120, 4, 2) == std::vector<uint16_t>({0x0101, 0x0202, 0x0303, 0x0404, 0x0505, 0x0606, 0x0707, 0x0808}), "80: a 4x2 block is copied");
	check(pixel(gpu, 10, 20) == 0x0101, "80: the source is left as it was");
	check(markerWorks(gpu, 300), "80: the GPU is ready for the next command after a copy");

	// wrap at the destination
	send(gpu, {0x80000000, (20u << 16) | 10, (130u << 16) | 1022, (1u << 16) | 4});
	check(pixel(gpu, 1022, 130) == 0x0101 && pixel(gpu, 1023, 130) == 0x0202 && pixel(gpu, 0, 130) == 0x0303 && pixel(gpu, 1, 130) == 0x0404 && pixel(gpu, 0, 131) == 0,
		"80: a copy past the right edge wraps to the left edge of the same line");

	// size 0 is the maximum, 1025 wraps to 1, positions are masked
	std::vector<uint16_t> row;
	for (uint32_t index = 0; index < 1024; index++) {
		row.push_back(patternPixel(index));
	}
	upload(gpu, 0, 140, 1024, 1, row);
	send(gpu, {0x80000000, (140u << 16) | 0, (150u << 16) | 0, (1u << 16) | 0});
	check(pixel(gpu, 0, 150) == row[0] && pixel(gpu, 1023, 150) == row[1023], "80: width 0 copies 1024 pixels");
	send(gpu, {0x80000000, (140u << 16) | 0, (160u << 16) | 0, (1u << 16) | 1025});
	check(pixel(gpu, 0, 160) == row[0] && pixel(gpu, 1, 160) == 0, "80: width 1025 wraps to 1");
	send(gpu, {0x80000000, (140u << 16) | 0x400, (0x2AAu << 16) | 0x405, (1u << 16) | 1});
	check(pixel(gpu, 5, 0x0AA) == row[0], "80: positions are masked to 10 and 9 bits", "pixel is " + pixelText(pixel(gpu, 5, 0x0AA)));
	check(markerWorks(gpu, 310), "80: the GPU is ready for the next command after the odd sizes");
}

// GP0(E6h): bit 0 sets bit 15 of every pixel written, bit 1 protects pixels whose bit 15 is set. Affects A0 and 80, not fills.
void checkMaskBit() {
	{   // force bit 15
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		gpu.writeGP0(0xE6000001);
		upload(gpu, 100, 100, 1, 1, {0x0456});
		checkEqual(pixel(gpu, 100, 100), 0x8456, "mask bit: A0 sets bit 15 when GP0(E6h) bit 0 is set");
		gpu.writeGP0(0xE6000000);
		upload(gpu, 100, 101, 1, 1, {0x0789});
		send(gpu, {0x80000000, (101u << 16) | 100, (110u << 16) | 100, (1u << 16) | 1});
		checkEqual(pixel(gpu, 100, 110), 0x0789, "mask bit: A0 and 80 leave bit 15 alone when GP0(E6h) bit 0 is clear");
		gpu.writeGP0(0xE6000001);
		send(gpu, {0x80000000, (101u << 16) | 100, (111u << 16) | 100, (1u << 16) | 1});
		checkEqual(pixel(gpu, 100, 111), 0x8789, "mask bit: 80 sets bit 15 when GP0(E6h) bit 0 is set");
	}
	{   // protect pixels with bit 15 set
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, 100, 100, 2, 1, {0x8123, 0x0123});
		gpu.writeGP0(0xE6000002);
		upload(gpu, 100, 100, 2, 1, {0x0456, 0x0456});
		check(pixel(gpu, 100, 100) == 0x8123 && pixel(gpu, 101, 100) == 0x0456, "mask bit: A0 does not overwrite a pixel whose bit 15 is set when GP0(E6h) bit 1 is set",
			"pixels " + pixelText(pixel(gpu, 100, 100)) + " " + pixelText(pixel(gpu, 101, 100)));
		upload(gpu, 100, 120, 2, 1, {0x0777, 0x0777});
		send(gpu, {0x80000000, (120u << 16) | 100, (100u << 16) | 100, (1u << 16) | 2});
		check(pixel(gpu, 100, 100) == 0x8123 && pixel(gpu, 101, 100) == 0x0777, "mask bit: 80 does not overwrite a pixel whose bit 15 is set when GP0(E6h) bit 1 is set",
			"pixels " + pixelText(pixel(gpu, 100, 100)) + " " + pixelText(pixel(gpu, 101, 100)));
	}
	{   // both
		GPU gpu;
		resetAndOpenDrawingArea(gpu);
		upload(gpu, 100, 100, 2, 1, {0x8123, 0x0123});
		gpu.writeGP0(0xE6000003);
		upload(gpu, 100, 100, 2, 1, {0x0456, 0x0456});
		check(pixel(gpu, 100, 100) == 0x8123 && pixel(gpu, 101, 100) == 0x8456, "mask bit: with both bits set, protected pixels stay and new pixels get bit 15",
			"pixels " + pixelText(pixel(gpu, 100, 100)) + " " + pixelText(pixel(gpu, 101, 100)));
	}
}

} // namespace

int main() {
	checkFill();
	checkFillIgnoresMask();
	checkCpuToVram();
	checkVramToCpu();
	checkVramToVram();
	checkMaskBit();
	return echo_check::finish();
}
