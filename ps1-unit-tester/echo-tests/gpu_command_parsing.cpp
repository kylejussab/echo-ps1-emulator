// How many words each GP0 command takes, against the stable psx-spx page (graphicsprocessingunitgpu.md: "GPU Render Polygon Commands",
// "GPU Render Line Commands", "GPU Render Rectangle Commands", "GPU Other Commands"). A GPU that takes one word too many swallows the next
// command; one that takes one word too few reads a leftover parameter as a command, and everything after it is garbage.
//
// Every command is sent with its documented parameter words, followed by a marker fill. Two things must hold afterwards:
//   - the marker fill ran (so the GPU took no more words than the command needs), and
//   - GPUSTAT bits 0-10 are still zero. The parameter words are built so that, read as a command, each one would be GP0(E1h) with its low
//     11 bits non-zero (top byte E1h: the bits above bit 26 of a vertex are ignored by the hardware, a colour word's top byte is "don't
//     care", the unused high half of a CLUT word is free), so a leftover word shows up as a changed draw mode.
// Each case runs in its own process, so a command that confuses the GPU into exiting is one failed check and not the end of the test.
// The GPU is built directly: no Bus, no BIOS, no SDL.
#include "gpu/GPU.h"
#include "support/echo_check.h"
#include "support/gpu_access.h"
#include "support/isolated_probe.h"
#include <cstdint>
#include <string>
#include <vector>

using namespace gpu_access;
using echo_check::check;
using echo_check::hex;

namespace {

const uint32_t TRAP = 0xE0000000;   // with the Y bits 8-10 = 1 of the vertices below, the top byte becomes E1h

// Vertex words have Y in 300..330, so bits 24-26 are 001; the free bits 27-31 carry the rest of the E1h
uint32_t trapVertex(int index) {
	return vertex(200 + index * 8, 300 + index * 8) | TRAP;
}

uint32_t trapColor(int index) {
	return 0xE1000000u | color24(0x20 + index, 0x30 + index, 0x40 + index);
}

// First UV word carries the CLUT (free to be anything), the second the texture page (kept clean so GPUSTAT stays at zero), the rest are free
uint32_t textureWord(int index) {
	uint32_t coordinates = (static_cast<uint32_t>(0x20 + index) << 8) | static_cast<uint32_t>(0x10 + index);
	return index == 1 ? coordinates : (0xE1000000u | coordinates);
}

struct Case {
	std::string name;
	std::vector<uint32_t> words;
};

// 0 = fine, otherwise a bit mask of what went wrong: 1 = the marker fill did not run, 2 = a parameter was read as a command
uint32_t runCase(const std::vector<uint32_t>& words) {
	GPU gpu;
	resetAndOpenDrawingArea(gpu);
	sendWords(gpu, words);
	fill(gpu, 512, 400, 16, 1, color24(0, 0xF8, 0));
	uint32_t problems = 0;
	if (pixel(gpu, 512, 400) != color15(0, 31, 0)) {
		problems |= 1;
	}
	if ((idleStatus(gpu) & STATUS_DRAW_MODE_BITS) != 0) {
		problems |= 2;
	}
	return problems;
}

void checkCases(const std::vector<Case>& cases) {
	for (const Case& testCase : cases) {
		std::vector<uint32_t> words = testCase.words;
		isolated_probe::Outcome outcome = isolated_probe::run([words] { return runCase(words); });
		if (!outcome.finished) {
			check(false, testCase.name, isolated_probe::describeFailure(outcome));
		} else {
			std::string details = std::string((outcome.value & 1) ? "the GPU took more words than the command needs (the next command was lost)" : "") +
								((outcome.value & 1) && (outcome.value & 2) ? "; " : "") +
								((outcome.value & 2) ? "the GPU took fewer words than the command needs (a parameter was read as a command)" : "");
			check(outcome.value == 0, testCase.name, details);
		}
	}
}

std::string commandName(uint32_t command) {
	return "GP0(" + hex(command, 2).substr(2) + "h)";
}

// 20h-3Fh: triangle or quad (bit 27), flat or gouraud (bit 28), textured (bit 26): per vertex a position, plus a UV word if textured,
// plus a colour word for every vertex but the first if gouraud (the first colour is in the command word)
std::vector<Case> polygonCases() {
	std::vector<Case> cases;
	for (uint32_t command = 0x20; command <= 0x3F; command++) {
		bool quad = (command & 0x08) != 0;
		bool textured = (command & 0x04) != 0;
		bool gouraud = (command & 0x10) != 0;
		int vertices = quad ? 4 : 3;
		std::vector<uint32_t> words = {(command << 24) | color24(0x80, 0x80, 0x80)};
		for (int index = 0; index < vertices; index++) {
			if (gouraud && index > 0) {
				words.push_back(trapColor(index));
			}
			words.push_back(trapVertex(index));
			if (textured) {
				words.push_back(textureWord(index));
			}
		}
		int expected = vertices * (textured ? 2 : 1) + (gouraud ? vertices - 1 : 0);
		cases.push_back({commandName(command) + std::string(gouraud ? " gouraud" : " flat") + (textured ? " textured" : "") + (quad ? " quad" : " triangle") + " takes " + std::to_string(expected) + " words",
						words});
	}
	return cases;
}

// 40h-5Fh single lines: two positions, plus one colour word if gouraud
std::vector<Case> lineCases() {
	std::vector<Case> cases;
	for (uint32_t command : {0x40u, 0x42u, 0x50u, 0x52u}) {
		bool gouraud = (command & 0x10) != 0;
		std::vector<uint32_t> words = {(command << 24) | color24(0x80, 0x80, 0x80), trapVertex(0)};
		if (gouraud) {
			words.push_back(trapColor(1));
		}
		words.push_back(trapVertex(1));
		cases.push_back({commandName(command) + (gouraud ? " gouraud" : " flat") + " line takes " + std::to_string(gouraud ? 3 : 2) + " words", words});
	}
	return cases;
}

// 48h-5Fh polylines: any number of vertices (at least two), ended by a word whose bits 12-15 and 28-31 are 5
std::vector<Case> polylineCases() {
	std::vector<Case> cases;
	for (uint32_t command : {0x48u, 0x4Au, 0x58u, 0x5Au}) {
		bool gouraud = (command & 0x10) != 0;
		for (int vertices : {2, 3, 5}) {
			std::vector<uint32_t> words = {(command << 24) | color24(0x80, 0x80, 0x80), trapVertex(0)};
			for (int index = 1; index < vertices; index++) {
				if (gouraud) {
					words.push_back(trapColor(index));
				}
				words.push_back(trapVertex(index));
			}
			words.push_back(0x55555555);
			cases.push_back({commandName(command) + (gouraud ? " gouraud" : " flat") + " polyline of " + std::to_string(vertices) + " vertices ends at the 5xxx5xxx word", words});
		}
	}
	{   // the terminator only has to match bits 12-15 and 28-31
		std::vector<uint32_t> words = {0x48808080, trapVertex(0), trapVertex(1), 0x5ABC5DEF};
		cases.push_back({"GP0(48h) polyline: the terminator 5ABC5DEFh counts, only bits 12-15 and 28-31 matter", words});
	}
	return cases;
}

// 60h-7Fh rectangles: a position, a UV word if textured (bit 26), a size word if the size is variable (bits 27-28 = 0)
std::vector<Case> rectangleCases() {
	std::vector<Case> cases;
	for (uint32_t command = 0x60; command <= 0x7F; command++) {
		bool textured = (command & 0x04) != 0;
		bool variable = (command & 0x18) == 0;
		std::vector<uint32_t> words = {(command << 24) | color24(0x80, 0x80, 0x80), trapVertex(0)};
		if (textured) {
			words.push_back(textureWord(0));
		}
		if (variable) {
			words.push_back((3u << 16) | 4);   // 4 x 3
		}
		int expected = 1 + (textured ? 1 : 0) + (variable ? 1 : 0);
		cases.push_back({commandName(command) + " rectangle takes " + std::to_string(expected) + " words after the command", words});
	}
	return cases;
}

// GP0(00h), GP0(04h..1Eh), GP0(E0h), GP0(E7h..EFh) take no parameters (and GP0(01h) clears the texture cache): the word is the whole command
std::vector<Case> noOperationCases() {
	std::vector<Case> cases;
	std::vector<uint32_t> commands = {0x00, 0x01, 0xE0};
	for (uint32_t command = 0x04; command <= 0x1E; command++) {
		commands.push_back(command);
	}
	for (uint32_t command = 0xE7; command <= 0xEF; command++) {
		commands.push_back(command);
	}
	for (uint32_t command : commands) {
		// The marker fill that follows is what shows whether the word swallowed anything
		cases.push_back({commandName(command) + " takes no parameters", {(command << 24) | 0x000123}});
	}
	return cases;
}

} // namespace

int main() {
	checkCases(polygonCases());
	checkCases(lineCases());
	checkCases(polylineCases());
	checkCases(rectangleCases());
	checkCases(noOperationCases());
	return echo_check::finish();
}
