// Helpers for talking to the GPU the way a game does: words to GP0, commands to GP1, VRAM read back through the VRAM pointer.
// Used by the gpu_*.cpp tests. They construct a GPU directly (no Bus, no BIOS, no SDL).
#pragma once
#include "gpu/GPU.h"
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace gpu_access {

// The hardware's VRAM size, written out here instead of using the emulator's constants
constexpr uint32_t VRAM_WIDTH = 1024;
constexpr uint32_t VRAM_HEIGHT = 512;

// GPUSTAT bits (psx-spx "GPU Status Register")
constexpr uint32_t STATUS_DRAW_MODE_BITS = 0x000007FF;   // bits 0-10, copied from GP0(E1h)
constexpr uint32_t STATUS_MASK_SET = 1u << 11;           // GP0(E6h) bit 0
constexpr uint32_t STATUS_MASK_CHECK = 1u << 12;         // GP0(E6h) bit 1
constexpr uint32_t STATUS_INTERLACE_FIELD = 1u << 13;
constexpr uint32_t STATUS_TEXTURE_DISABLE = 1u << 15;
constexpr uint32_t STATUS_HORIZONTAL_RESOLUTION_2 = 1u << 16;
constexpr uint32_t STATUS_DISPLAY_MODE_BITS = 0x007E0000; // bits 17-22, copied from GP1(08h) bits 0-5
constexpr uint32_t STATUS_DISPLAY_DISABLED = 1u << 23;
constexpr uint32_t STATUS_IRQ = 1u << 24;
constexpr uint32_t STATUS_DMA_REQUEST = 1u << 25;
constexpr uint32_t STATUS_READY_FOR_COMMAND = 1u << 26;
constexpr uint32_t STATUS_READY_TO_SEND_VRAM = 1u << 27;
constexpr uint32_t STATUS_READY_FOR_DMA_BLOCK = 1u << 28;
constexpr uint32_t STATUS_DMA_DIRECTION_BITS = 0x60000000;

inline uint32_t vertex(int x, int y) {
	return (static_cast<uint32_t>(y & 0x7FF) << 16) | static_cast<uint32_t>(x & 0x7FF);
}

// 24-bit command color 0x00BBGGRR from 8-bit channels
inline uint32_t color24(uint32_t red, uint32_t green, uint32_t blue) {
	return red | (green << 8) | (blue << 16);
}

// The 15-bit VRAM value for 5-bit channels (bit 15 clear)
inline uint16_t color15(uint32_t red, uint32_t green, uint32_t blue) {
	return static_cast<uint16_t>(red | (green << 5) | (blue << 10));
}

inline void send(GPU& gpu, std::initializer_list<uint32_t> words) {
	for (uint32_t word : words) {
		gpu.writeGP0(word);
	}
}

inline void sendWords(GPU& gpu, const std::vector<uint32_t>& words) {
	for (uint32_t word : words) {
		gpu.writeGP0(word);
	}
}

inline uint16_t pixel(const GPU& gpu, uint32_t x, uint32_t y) {
	return gpu.getVRAMRawPointer()[(y % VRAM_HEIGHT) * VRAM_WIDTH + (x % VRAM_WIDTH)];
}

// Let any drawing in progress finish, so the ready bits read as idle
inline void waitUntilIdle(GPU& gpu) {
	for (int step = 0; step < 4; step++) {
		gpu.advanceClock(500000000);
	}
}

// GPUSTAT after the GPU has gone idle
inline uint32_t idleStatus(GPU& gpu) {
	waitUntilIdle(gpu);
	return gpu.readGP1();
}

// Reset the GPU and open the drawing area to all of VRAM, so a drawing test is not clipped by accident
inline void resetAndOpenDrawingArea(GPU& gpu) {
	gpu.writeGP1(0x00000000);
	gpu.writeGP0(0xE3000000);                               // top left (0, 0)
	gpu.writeGP0(0xE4000000 | 1023u | (511u << 10));        // bottom right (1023, 511)
	gpu.writeGP0(0xE5000000);                               // drawing offset (0, 0)
	waitUntilIdle(gpu);
}

// GP0(02h) quick fill. x counts in steps of 16, width is rounded up to a multiple of 16.
inline void fill(GPU& gpu, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
	send(gpu, {0x02000000u | color, (y << 16) | x, (height << 16) | width});
}

// GP0(A0h): write a block of pixels (row by row). An odd pixel count is padded with one extra halfword, as the hardware expects.
inline void upload(GPU& gpu, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const std::vector<uint16_t>& pixels) {
	send(gpu, {0xA0000000u, (y << 16) | x, (height << 16) | width});
	for (size_t index = 0; index < pixels.size(); index += 2) {
		uint32_t low = pixels[index];
		uint32_t high = index + 1 < pixels.size() ? pixels[index + 1] : 0;
		gpu.writeGP0(low | (high << 16));
	}
}

// GP0(C0h): read a block of pixels back through the GP0 port
inline std::vector<uint16_t> download(GPU& gpu, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
	send(gpu, {0xC0000000u, (y << 16) | x, (height << 16) | width});
	uint32_t pixelCount = width * height;
	std::vector<uint16_t> pixels;
	for (uint32_t index = 0; index < pixelCount; index += 2) {
		uint32_t word = gpu.readGP0();
		pixels.push_back(static_cast<uint16_t>(word & 0xFFFF));
		if (index + 1 < pixelCount) {
			pixels.push_back(static_cast<uint16_t>(word >> 16));
		}
	}
	return pixels;
}

} // namespace gpu_access
