// echo-headless: runs ONE PS-EXE test program on the emulator core with no window, then writes out what the test said.
//
// This is deliberately a separate program from the emulator. It builds a Bus and a CPU (exactly like PlayStation does),
// steps the CPU, and does the test-harness work from the outside:
//   - when the BIOS is about to enter the shell (0x80030000) it copies the EXE into RAM and jumps to it,
//   - when the BIOS is about to run a printf/puts/putchar call (A0/B0 vectors) it captures the text,
//   - when the run ends it dumps the TTY text and VRAM.
// Nothing here changes how the emulator behaves. The driver script (run_tests.py) decides pass/fail.
//
// Some tests are menus that wait for a button. --press plugs a virtual controller into the emulator (through SDL, exactly like a real
// pad would be) and presses buttons at chosen frames.
//
// usage: echo-headless --bios <bios> --exe <program> [--frames N] [--quiet-exit N] [--tty-log file] [--vram-png file]
//                      [--press <button>@<frame>[+<heldFrames>] ...]
#include <SDL2/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include "core/Bus.h"
#include "core/Constants.h"
#include "cpu/CPU.h"

namespace {

// ---------------------------------------------------------------------------------------------
// PS-EXE loading
// ---------------------------------------------------------------------------------------------

struct ExeImage {
	uint32_t entryPoint = 0;
	uint32_t globalPointer = 0;
	uint32_t loadAddress = 0;
	uint32_t stackBase = 0;
	uint32_t stackOffset = 0;
	std::vector<uint8_t> payload;
};

uint32_t readLittleEndian32(const std::vector<uint8_t>& bytes, size_t offset) {
	return bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

bool loadExe(const std::string& path, ExeImage& image) {
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		std::cout << "HEADLESS: could not open EXE: " << path << std::endl;
		return false;
	}
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	if (bytes.size() < 0x800 || std::string(bytes.begin(), bytes.begin() + 8) != "PS-X EXE") {
		std::cout << "HEADLESS: not a PS-X EXE: " << path << std::endl;
		return false;
	}

	image.entryPoint = readLittleEndian32(bytes, 0x10);
	image.globalPointer = readLittleEndian32(bytes, 0x14);
	image.loadAddress = readLittleEndian32(bytes, 0x18);
	uint32_t textSize = readLittleEndian32(bytes, 0x1C);
	image.stackBase = readLittleEndian32(bytes, 0x30);
	image.stackOffset = readLittleEndian32(bytes, 0x34);

	size_t available = bytes.size() - 0x800;
	if (textSize > available) textSize = static_cast<uint32_t>(available);
	image.payload.assign(bytes.begin() + 0x800, bytes.begin() + 0x800 + textSize);
	return true;
}

// ---------------------------------------------------------------------------------------------
// TTY capture: collects what the test prints, line by line, and counts pass/fail results
// ---------------------------------------------------------------------------------------------

struct TTYCapture {
	std::string currentLine;
	FILE* logFile = nullptr;
	uint64_t characterCount = 0;
	int passLines = 0;
	int failLines = 0;
	int cesterPassing = 0;
	int cesterFailing = 0;
	bool synthesisSeen = false;

	// Removes ANSI colour escapes (ESC [ ... m) that libcester emits
	static std::string stripAnsi(const std::string& text) {
		std::string clean;
		for (size_t index = 0; index < text.size(); index++) {
			if (text[index] == '\x1b' && index + 1 < text.size() && text[index + 1] == '[') {
				while (index < text.size() && text[index] != 'm') index++;
				continue;
			}
			clean.push_back(text[index]);
		}
		return clean;
	}

	void endLine() {
		std::string clean = stripAnsi(currentLine);
		// Some ps1-tests print their results with a leading "% "
		const std::string body = (clean.rfind("% ", 0) == 0) ? clean.substr(2) : clean;
		if (body.rfind("pass - ", 0) == 0) passLines++;
		else if (body.rfind("fail - ", 0) == 0) failLines++;

		// libcester summary: "Synthesis: <result> Tests: N | Passing: P | Failing: F"
		size_t synthesis = clean.find("Synthesis:");
		if (synthesis != std::string::npos) {
			synthesisSeen = true;
			size_t passing = clean.find("Passing:", synthesis);
			size_t failing = clean.find("Failing:", synthesis);
			if (passing != std::string::npos) cesterPassing += std::atoi(clean.c_str() + passing + 8);
			if (failing != std::string::npos) cesterFailing += std::atoi(clean.c_str() + failing + 8);
		}

		if (logFile) {
			std::fputs(currentLine.c_str(), logFile);
			std::fputc('\n', logFile);
			std::fflush(logFile); // keep the text even if the emulator later hits a FATAL and exits
		}
		currentLine.clear();
	}

	void addCharacter(char character) {
		characterCount++;
		if (character == '\r') return;
		if (character == '\n') { endLine(); return; }
		currentLine.push_back(character);
		if (currentLine.size() > 4096) endLine();
	}

	void addText(const std::string& text) {
		for (char character : text) addCharacter(character);
	}

	void finish() {
		if (!currentLine.empty()) endLine();
		if (logFile) { std::fclose(logFile); logFile = nullptr; }
	}

	int totalPassing() const { return passLines + cesterPassing; }
	int totalFailing() const { return failLines + cesterFailing; }
};

// ---------------------------------------------------------------------------------------------
// VRAM -> PNG (uncompressed deflate blocks, so no zlib needed). 5-bit channels expand with (c << 3),
// which is what the reference vram.png files in ps1-tests use.
// ---------------------------------------------------------------------------------------------

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0xFFFFFFFFu) {
	static uint32_t table[256];
	static bool ready = false;
	if (!ready) {
		for (uint32_t index = 0; index < 256; index++) {
			uint32_t value = index;
			for (int bit = 0; bit < 8; bit++) value = (value & 1) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
			table[index] = value;
		}
		ready = true;
	}
	for (size_t index = 0; index < size; index++) crc = table[(crc ^ data[index]) & 0xFF] ^ (crc >> 8);
	return crc;
}

void appendBigEndian32(std::vector<uint8_t>& output, uint32_t value) {
	output.push_back(value >> 24); output.push_back(value >> 16); output.push_back(value >> 8); output.push_back(value);
}

void appendChunk(std::vector<uint8_t>& output, const char type[4], const std::vector<uint8_t>& data) {
	appendBigEndian32(output, static_cast<uint32_t>(data.size()));
	std::vector<uint8_t> typeAndData(type, type + 4);
	typeAndData.insert(typeAndData.end(), data.begin(), data.end());
	output.insert(output.end(), typeAndData.begin(), typeAndData.end());
	appendBigEndian32(output, ~crc32(typeAndData.data(), typeAndData.size()));
}

bool writeVramPng(const std::string& path, const uint16_t* vram) {
	constexpr uint32_t width = 1024, height = 512;

	std::vector<uint8_t> raw; // scanlines: filter byte 0 + RGB triplets
	raw.reserve(height * (1 + width * 3));
	for (uint32_t row = 0; row < height; row++) {
		raw.push_back(0);
		for (uint32_t column = 0; column < width; column++) {
			uint16_t pixel = vram[row * width + column];
			raw.push_back(static_cast<uint8_t>((pixel & 0x1F) << 3));
			raw.push_back(static_cast<uint8_t>(((pixel >> 5) & 0x1F) << 3));
			raw.push_back(static_cast<uint8_t>(((pixel >> 10) & 0x1F) << 3));
		}
	}

	std::vector<uint8_t> zlibStream = {0x78, 0x01};
	size_t position = 0;
	while (position < raw.size()) {
		size_t chunkSize = std::min<size_t>(65535, raw.size() - position);
		bool isLast = (position + chunkSize == raw.size());
		zlibStream.push_back(isLast ? 1 : 0);
		zlibStream.push_back(chunkSize & 0xFF); zlibStream.push_back(chunkSize >> 8);
		zlibStream.push_back(~chunkSize & 0xFF); zlibStream.push_back((~chunkSize >> 8) & 0xFF);
		zlibStream.insert(zlibStream.end(), raw.begin() + position, raw.begin() + position + chunkSize);
		position += chunkSize;
	}
	uint32_t sumA = 1, sumB = 0;
	for (uint8_t byte : raw) { sumA = (sumA + byte) % 65521; sumB = (sumB + sumA) % 65521; }
	appendBigEndian32(zlibStream, (sumB << 16) | sumA);

	std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	std::vector<uint8_t> header;
	appendBigEndian32(header, width); appendBigEndian32(header, height);
	header.push_back(8);  // bit depth
	header.push_back(2);  // colour type: RGB
	header.push_back(0); header.push_back(0); header.push_back(0);
	appendChunk(png, "IHDR", header);
	appendChunk(png, "IDAT", zlibStream);
	appendChunk(png, "IEND", {});

	std::ofstream file(path, std::ios::binary);
	if (!file.is_open()) return false;
	file.write(reinterpret_cast<const char*>(png.data()), png.size());
	return true;
}

// ---------------------------------------------------------------------------------------------
// Reading the guest's memory and formatting its printf calls
// ---------------------------------------------------------------------------------------------

bool isSafeGuestAddress(uint32_t address) {
	uint32_t physical = address & 0x1FFFFFFF;
	return physical < 0x00800000
		|| (physical >= 0x1F800000 && physical < 0x1F800400)
		|| (physical >= Hardware::BIOS_STARTING_ADDRESS && physical < Hardware::BIOS_STARTING_ADDRESS + Hardware::BIOS_SIZE);
}

uint32_t safeReadGuest32(Bus& bus, uint32_t address) {
	if (!isSafeGuestAddress(address) || !isSafeGuestAddress(address + 3)) return 0;
	return bus.read32(address & ~3u);
}

std::string readGuestString(Bus& bus, uint32_t address) {
	std::string text;
	for (int offset = 0; offset < 2048; offset++) {
		if (!isSafeGuestAddress(address + offset)) break;
		char character = static_cast<char>(bus.read8(address + offset));
		if (character == '\0') break;
		text.push_back(character);
	}
	return text;
}

// Minimal printf: %d %i %u %x %X %o %c %s %p %% with flags, width, precision and the l/h length modifiers.
// Arguments follow the MIPS o32 convention: a1-a3, then the stack at sp+16 onwards.
std::string formatGuestPrintf(Bus& bus, CPU& cpu) {
	std::string format = readGuestString(bus, cpu.getRegister(4));
	int argumentIndex = 0;
	auto nextArgument = [&]() -> uint32_t {
		uint32_t value = (argumentIndex < 3) ? cpu.getRegister(5 + argumentIndex)
		                                     : safeReadGuest32(bus, cpu.getRegister(29) + 16 + (argumentIndex - 3) * 4);
		argumentIndex++;
		return value;
	};

	std::string output;
	for (size_t index = 0; index < format.size(); index++) {
		if (format[index] != '%') { output.push_back(format[index]); continue; }
		index++;
		if (index >= format.size()) break;
		if (format[index] == '%') { output.push_back('%'); continue; }

		std::string specification = "%";
		while (index < format.size() && std::string("-+ #0").find(format[index]) != std::string::npos) specification.push_back(format[index++]);

		if (index < format.size() && format[index] == '*') { specification += std::to_string(static_cast<int32_t>(nextArgument())); index++; }
		else while (index < format.size() && isdigit(static_cast<unsigned char>(format[index]))) specification.push_back(format[index++]);

		if (index < format.size() && format[index] == '.') {
			specification.push_back(format[index++]);
			if (index < format.size() && format[index] == '*') { specification += std::to_string(static_cast<int32_t>(nextArgument())); index++; }
			else while (index < format.size() && isdigit(static_cast<unsigned char>(format[index]))) specification.push_back(format[index++]);
		}

		while (index < format.size() && (format[index] == 'l' || format[index] == 'h' || format[index] == 'z')) index++; // all ints are 32-bit here
		if (index >= format.size()) break;

		char conversion = format[index];
		char buffer[512];
		switch (conversion) {
			case 'd': case 'i': {
				specification += "d";
				snprintf(buffer, sizeof(buffer), specification.c_str(), static_cast<int>(nextArgument()));
				output += buffer;
				break;
			}
			case 'u': case 'x': case 'X': case 'o': {
				specification.push_back(conversion);
				snprintf(buffer, sizeof(buffer), specification.c_str(), static_cast<unsigned int>(nextArgument()));
				output += buffer;
				break;
			}
			case 'c': {
				specification += "c";
				snprintf(buffer, sizeof(buffer), specification.c_str(), static_cast<int>(nextArgument() & 0xFF));
				output += buffer;
				break;
			}
			case 's': {
				specification += "s";
				std::string text = readGuestString(bus, nextArgument());
				snprintf(buffer, sizeof(buffer), specification.c_str(), text.c_str());
				output += buffer;
				break;
			}
			case 'p': {
				snprintf(buffer, sizeof(buffer), "0x%08x", nextArgument());
				output += buffer;
				break;
			}
			default: { // unknown conversion, keep it visible
				output += specification;
				output.push_back(conversion);
				break;
			}
		}
	}
	return output;
}

// ---------------------------------------------------------------------------------------------
// Scripted controller input
// ---------------------------------------------------------------------------------------------

struct ScriptedPress {
	uint64_t pressFrame = 0;
	uint64_t releaseFrame = 0;
	int index = 0;          // SDL_GameControllerButton, or SDL_GameControllerAxis for L2/R2
	bool isAxis = false;
	bool isDown = false;
};

// "start@300" or "cross@120+10" (button@frame, optionally held for that many frames; default 5)
bool parseScriptedPress(const std::string& text, ScriptedPress& press) {
	size_t at = text.find('@');
	if (at == std::string::npos) return false;
	std::string name = text.substr(0, at);
	std::string timing = text.substr(at + 1);
	uint64_t heldFrames = 5;
	size_t plus = timing.find('+');
	if (plus != std::string::npos) {
		heldFrames = std::strtoull(timing.c_str() + plus + 1, nullptr, 10);
		timing = timing.substr(0, plus);
	}
	press.pressFrame = std::strtoull(timing.c_str(), nullptr, 10);
	press.releaseFrame = press.pressFrame + heldFrames;

	struct NamedButton { const char* name; int index; bool isAxis; };
	static const NamedButton buttons[] = {
		{"start", SDL_CONTROLLER_BUTTON_START, false}, {"select", SDL_CONTROLLER_BUTTON_BACK, false},
		{"up", SDL_CONTROLLER_BUTTON_DPAD_UP, false}, {"down", SDL_CONTROLLER_BUTTON_DPAD_DOWN, false},
		{"left", SDL_CONTROLLER_BUTTON_DPAD_LEFT, false}, {"right", SDL_CONTROLLER_BUTTON_DPAD_RIGHT, false},
		{"cross", SDL_CONTROLLER_BUTTON_A, false}, {"circle", SDL_CONTROLLER_BUTTON_B, false},
		{"square", SDL_CONTROLLER_BUTTON_X, false}, {"triangle", SDL_CONTROLLER_BUTTON_Y, false},
		{"l1", SDL_CONTROLLER_BUTTON_LEFTSHOULDER, false}, {"r1", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, false},
		{"l3", SDL_CONTROLLER_BUTTON_LEFTSTICK, false}, {"r3", SDL_CONTROLLER_BUTTON_RIGHTSTICK, false},
		{"l2", SDL_CONTROLLER_AXIS_TRIGGERLEFT, true}, {"r2", SDL_CONTROLLER_AXIS_TRIGGERRIGHT, true},
	};
	for (const NamedButton& button : buttons) {
		if (name == button.name) { press.index = button.index; press.isAxis = button.isAxis; return true; }
	}
	return false;
}

void applyScriptedPress(SDL_Joystick* virtualPad, ScriptedPress& press, bool down) {
	press.isDown = down;
	if (press.isAxis) SDL_JoystickSetVirtualAxis(virtualPad, press.index, down ? 32767 : -32768);
	else SDL_JoystickSetVirtualButton(virtualPad, press.index, down ? 1 : 0);
}

void printUsage() {
	std::cout << "usage: echo-headless --bios <bios> --exe <program> [options]\n"
	          << "  --frames <n>       stop after n frames (default 3000)\n"
	          << "  --quiet-exit <n>   stop n frames after the test's last TTY character (0 = off)\n"
	          << "  --tty-log <file>   write the test's TTY output here\n"
	          << "  --vram-png <file>  write VRAM as a PNG at the end of the run\n"
	          << "  --press <b>@<f>[+<n>]  press button b (start select up down left right cross circle square triangle l1 r1 l2 r2 l3 r3)\n"
	          << "                     at frame f, holding it n frames (default 5). May be given several times.\n";
}

} // namespace


int main(int argumentCount, char* arguments[]) {
	std::string biosPath, exePath, ttyLogPath, vramPngPath;
	uint64_t maximumFrames = 3000;
	uint64_t quietExitFrames = 0;
	std::vector<ScriptedPress> scriptedPresses;

	for (int index = 1; index < argumentCount; index++) {
		std::string argument = arguments[index];
		auto needValue = [&]() -> const char* {
			if (index + 1 >= argumentCount) { std::cout << "Missing value for " << argument << std::endl; printUsage(); exit(2); }
			return arguments[++index];
		};
		if (argument == "--bios") biosPath = needValue();
		else if (argument == "--exe") exePath = needValue();
		else if (argument == "--frames") maximumFrames = std::strtoull(needValue(), nullptr, 10);
		else if (argument == "--quiet-exit") quietExitFrames = std::strtoull(needValue(), nullptr, 10);
		else if (argument == "--tty-log") ttyLogPath = needValue();
		else if (argument == "--vram-png") vramPngPath = needValue();
		else if (argument == "--press") {
			ScriptedPress press;
			std::string value = needValue();
			if (!parseScriptedPress(value, press)) { std::cout << "Bad --press value: " << value << std::endl; printUsage(); return 2; }
			scriptedPresses.push_back(press);
		}
		else { printUsage(); return 2; }
	}
	if (biosPath.empty() || exePath.empty()) { printUsage(); return 2; }

	ExeImage exeImage;
	if (!loadExe(exePath, exeImage)) return 1;

	Bus bus;
	if (!bus.loadBIOS(biosPath)) {
		std::cout << "HEADLESS: failed to load BIOS: " << biosPath << std::endl;
		return 1;
	}
	CPU cpu(&bus);

	// A virtual game controller, plugged in the same way SDL announces a real one (the Bus's DualShock has already started SDL's controller support)
	SDL_Joystick* virtualPad = nullptr;
	if (!scriptedPresses.empty()) {
		int deviceIndex = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
		virtualPad = deviceIndex >= 0 ? SDL_JoystickOpen(deviceIndex) : nullptr;
		if (!virtualPad) {
			std::cout << "HEADLESS: could not create a virtual controller: " << SDL_GetError() << " (--press needs SDL 2.0.14 or newer)" << std::endl;
			return 1;
		}
		SDL_JoystickSetVirtualAxis(virtualPad, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);   // triggers rest at the bottom of the range
		SDL_JoystickSetVirtualAxis(virtualPad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
		SDL_Event addedEvent = {};
		addedEvent.type = SDL_CONTROLLERDEVICEADDED;
		addedEvent.cdevice.which = deviceIndex;
		bus.getSIO().getController().handleEvent(addedEvent);
	}

	TTYCapture tty;
	if (!ttyLogPath.empty()) {
		tty.logFile = std::fopen(ttyLogPath.c_str(), "wb");
		if (!tty.logFile) std::cout << "HEADLESS: could not open TTY log " << ttyLogPath << std::endl;
	}

	bool sideloaded = false;
	uint64_t stepCounter = 0;

	// While the BIOS's own printf is running it calls putchar/puts itself; those calls are already covered by the printf capture
	bool insidePrintf = false;
	uint32_t printfReturnAddress = 0;
	uint64_t printfStartStep = 0;

	uint64_t frameCounter = 0;
	uint64_t lastTTYCharacterCount = 0;
	uint64_t lastTTYChangeFrame = 0;
	const char* stopReason = "frame-limit";

	while (frameCounter < maximumFrames) {
		if (virtualPad) {
			for (ScriptedPress& press : scriptedPresses) {
				if (!press.isDown && frameCounter == press.pressFrame) applyScriptedPress(virtualPad, press, true);
				else if (press.isDown && frameCounter == press.releaseFrame) applyScriptedPress(virtualPad, press, false);
			}
			SDL_GameControllerUpdate();
		}

		for (int stepIndex = 0; stepIndex < Hardware::CYCLES_PER_FRAME; stepIndex++) {
			uint32_t nextAddress = cpu.getNextInstructionAddress();
			uint32_t physicalAddress = nextAddress & 0x1FFFFFFF;

			// The BIOS is about to enter the shell: start the test program instead
			if (!sideloaded && physicalAddress == 0x00030000) {
				bus.loadRAMBlock(exeImage.loadAddress, exeImage.payload.data(), exeImage.payload.size());
				cpu.jumpTo(exeImage.entryPoint);
				cpu.setRegister(28, exeImage.globalPointer);
				uint32_t stackPointer = exeImage.stackBase != 0 ? (exeImage.stackBase + exeImage.stackOffset) : 0x801FFFF0;
				cpu.setRegister(29, stackPointer);
				cpu.setRegister(30, stackPointer);
				cpu.setRegister(4, 0); // argc
				cpu.setRegister(5, 0); // argv
				sideloaded = true;
				nextAddress = cpu.getNextInstructionAddress();
				physicalAddress = nextAddress & 0x1FFFFFFF;
				std::cout << "HEADLESS: sideloaded EXE (" << exeImage.payload.size() << " bytes) at frame " << frameCounter << std::endl;
			}

			if (insidePrintf && (physicalAddress == printfReturnAddress || stepCounter - printfStartStep > 500000)) {
				insidePrintf = false;
			}

			// A BIOS call is about to run (A0/B0/C0 dispatcher entry). Work out what text it would print BEFORE stepping,
			// because that is when its arguments are in the registers, but only keep it if the step really executed it
			// (a pending interrupt can be taken first, in which case the call runs again after the interrupt returns).
			bool hasPendingText = false;
			bool pendingIsPrintf = false;
			std::string pendingText;
			uint32_t pendingReturnAddress = 0;
			if (sideloaded && (physicalAddress == 0xA0 || physicalAddress == 0xB0 || physicalAddress == 0xC0)) {
				uint32_t function = cpu.getRegister(9);
				if (physicalAddress == 0xA0 && function == 0x3F) { // printf
					pendingText = formatGuestPrintf(bus, cpu);
					pendingIsPrintf = true;
					pendingReturnAddress = cpu.getRegister(31) & 0x1FFFFFFF;
					hasPendingText = true;
				}
				else if (!insidePrintf) {
					if ((physicalAddress == 0xA0 && function == 0x3C) || (physicalAddress == 0xB0 && function == 0x3D)) { // putchar
						pendingText = std::string(1, static_cast<char>(cpu.getRegister(4) & 0xFF));
						hasPendingText = true;
					}
					else if ((physicalAddress == 0xA0 && function == 0x3E) || (physicalAddress == 0xB0 && function == 0x3F)) { // puts
						pendingText = readGuestString(bus, cpu.getRegister(4)) + "\n";
						hasPendingText = true;
					}
				}
			}

			cpu.step();
			stepCounter++;

			if (hasPendingText && cpu.getProgramCounter() == nextAddress) {
				if (pendingIsPrintf) {
					insidePrintf = true;
					printfReturnAddress = pendingReturnAddress;
					printfStartStep = stepCounter;
				}
				tty.addText(pendingText);
			}
		}

		frameCounter++;

		if (sideloaded && tty.synthesisSeen) { stopReason = "suite-complete"; break; } // libcester printed its final summary
		if (sideloaded && quietExitFrames != 0) {
			if (tty.characterCount != lastTTYCharacterCount) { lastTTYCharacterCount = tty.characterCount; lastTTYChangeFrame = frameCounter; }
			if (tty.characterCount > 0 && frameCounter - lastTTYChangeFrame >= quietExitFrames) { stopReason = "quiet"; break; }
		}
	}

	tty.finish();

	if (!vramPngPath.empty() && !writeVramPng(vramPngPath, bus.getGPU().getVRAMRawPointer())) {
		std::cout << "HEADLESS: could not write " << vramPngPath << std::endl;
	}

	std::cout << "[HEADLESS] sideloaded=" << (sideloaded ? 1 : 0) << " frames=" << frameCounter << " stop=" << stopReason
	          << " pass=" << tty.totalPassing() << " fail=" << tty.totalFailing() << std::endl;
	return 0;
}
