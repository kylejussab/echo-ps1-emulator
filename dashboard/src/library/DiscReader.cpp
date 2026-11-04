// DiscReader.cpp
#include "library/DiscReader.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int sectorDataSize = 2048;

struct DataTrack {
	fs::path binPath;
	int sectorSize = 2352;
	int dataOffset = 24; // bytes from the start of a sector to its 2048 bytes of user data
};

std::string uppercase(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(),
	               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return text;
}

std::string trim(const std::string& text) {
	const size_t first = text.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return "";
	const size_t last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

uint32_t readU32(const uint8_t* p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Finds the first FILE and the first TRACK after it
bool parseCue(const fs::path& cuePath, DataTrack& track) {
	std::ifstream cue(cuePath);
	if (!cue) return false;

	bool haveFile = false;
	std::string line;
	while (std::getline(cue, line)) {
		line = trim(line);
		const std::string upperLine = uppercase(line);

		if (!haveFile && upperLine.rfind("FILE", 0) == 0) {
			const size_t open = line.find('"');
			const size_t close = (open == std::string::npos) ? std::string::npos : line.find('"', open + 1);
			if (close == std::string::npos) return false;

			track.binPath = cuePath.parent_path() / line.substr(open + 1, close - open - 1);
			haveFile = true;
		}
		else if (haveFile && upperLine.rfind("TRACK", 0) == 0) {
			std::istringstream tokens(upperLine);
			std::string keyword, number, mode;
			tokens >> keyword >> number >> mode;

			if (mode == "MODE2/2352")      { track.sectorSize = 2352; track.dataOffset = 24; }
			else if (mode == "MODE1/2352") { track.sectorSize = 2352; track.dataOffset = 16; }
			else if (mode == "MODE1/2048") { track.sectorSize = 2048; track.dataOffset = 0; }
			else if (mode == "MODE2/2336") { track.sectorSize = 2336; track.dataOffset = 8; }
			else return false; // first track isn't data

			return true;
		}
	}
	return false;
}

bool readSector(std::ifstream& bin, const DataTrack& track, uint32_t lba, std::vector<uint8_t>& out) {
	out.resize(sectorDataSize);
	bin.clear();
	bin.seekg(static_cast<std::streamoff>(lba) * track.sectorSize + track.dataOffset);
	bin.read(reinterpret_cast<char*>(out.data()), sectorDataSize);
	return bin.gcount() == sectorDataSize;
}

// Looks through the root directory for SYSTEM.CNF. Returns its extent and size.
bool findSystemCnf(std::ifstream& bin, const DataTrack& track, uint32_t& extent, uint32_t& size) {
	std::vector<uint8_t> sector;

	// Primary Volume Descriptor lives at sector 16
	if (!readSector(bin, track, 16, sector)) return false;
	if (sector[0] != 1 || std::memcmp(&sector[1], "CD001", 5) != 0) return false;

	// The root directory record is embedded at offset 156 of the descriptor
	const uint32_t rootLba = readU32(&sector[156 + 2]);
	const uint32_t rootLength = readU32(&sector[156 + 10]);
	const uint32_t rootSectors = (rootLength + sectorDataSize - 1) / sectorDataSize;

	for (uint32_t s = 0; s < rootSectors; ++s) {
		if (!readSector(bin, track, rootLba + s, sector)) return false;

		size_t pos = 0;
		while (pos + 34 <= sectorDataSize) {
			const uint8_t recordLength = sector[pos];
			if (recordLength == 0) break;                          // padding, rest of the sector is empty
			if (recordLength < 34 || pos + recordLength > sectorDataSize) break;

			const uint8_t flags = sector[pos + 25];
			const uint8_t nameLength = sector[pos + 32];
			if (!(flags & 0x02) && 33u + nameLength <= recordLength) { // skip directories
				const std::string name = uppercase(std::string(
					reinterpret_cast<const char*>(&sector[pos + 33]), nameLength));

				if (name.rfind("SYSTEM.CNF", 0) == 0) {
					extent = readU32(&sector[pos + 2]);
					size = readU32(&sector[pos + 10]);
					return true;
				}
			}
			pos += recordLength;
		}
	}
	return false;
}

// "cdrom:\SCUS_949.00;1" -> "SCUS-94900"
std::string serialFromBootLine(const std::string& cnf) {
	std::istringstream lines(cnf);
	std::string line;
	while (std::getline(lines, line)) {
		line = trim(line);
		const std::string upperLine = uppercase(line);

		// "BOOT = ..." but not "BOOT2 = ..." (that's PS2)
		if (upperLine.rfind("BOOT", 0) != 0 || upperLine.rfind("BOOT2", 0) == 0) continue;

		const size_t equals = line.find('=');
		if (equals == std::string::npos) continue;

		std::string value = trim(line.substr(equals + 1));

		const size_t lastSeparator = value.find_last_of("\\/:");
		if (lastSeparator != std::string::npos) value = value.substr(lastSeparator + 1);

		const size_t semicolon = value.find(';');
		if (semicolon != std::string::npos) value = value.substr(0, semicolon);

		value = uppercase(trim(value));

		// Normal executables are named like SCUS_949.00, but a few discs (Rayman) use SLUS-000.05
		if (value.find('_') == std::string::npos && value.find('-') == std::string::npos) return "";

		std::string serial;
		for (char c : value) {
			if (c == '.') continue;
			serial += (c == '_') ? '-' : c;
		}
		return serial;
	}
	return "";
}

} // namespace

std::string readDiscSerial(const std::string& cuePath) {
	DataTrack track;
	if (!parseCue(cuePath, track)) {
		std::cout << "Could not read cue: " << cuePath << std::endl;
		return "";
	}

	std::ifstream bin(track.binPath, std::ios::binary);
	if (!bin) {
		std::cout << "Could not open disc image: " << track.binPath << std::endl;
		return "";
	}

	uint32_t extent = 0, size = 0;
	if (!findSystemCnf(bin, track, extent, size)) {
		std::cout << "No SYSTEM.CNF found on: " << cuePath << std::endl;
		return "";
	}

	std::vector<uint8_t> sector;
	if (!readSector(bin, track, extent, sector)) return "";

	// SYSTEM.CNF is tiny, so the first sector holds all of it
	const size_t length = std::min<size_t>(size, sectorDataSize);
	const std::string cnf(reinterpret_cast<const char*>(sector.data()), length);

	return serialFromBootLine(cnf);
}