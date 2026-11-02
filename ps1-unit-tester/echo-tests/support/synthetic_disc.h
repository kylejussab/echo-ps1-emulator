// Made-up disc images so Disc tests need no real game (which cannot be committed). A Workspace is a temporary directory that holds
// .bin files full of recognisable sectors and .cue sheets written as plain text, so a test controls the cue syntax exactly.
//
// Every sector follows a fixed pattern (sectorByte), so a test can tell exactly which sector it got back and where a wrong byte is:
//   bytes  0-11  the 12-byte sync pattern of a CD-ROM data sector (00, ten times FF, 00)
//   bytes 12-14  the sector's own MM SS FF header, in BCD, of (lba + 150): the header counts from the start of the disc including the
//                2-second lead-in, while the logical block address starts at 00:02:00
//   byte  15     mode 2
//   bytes 16-23  a Form 1 sub-header
//   bytes 24-27  the sector's LBA, little-endian (so a failure message can say which sector was actually returned)
//   rest         a pattern that depends on the LBA and the byte position
//   sector 16    also holds a volume name at bytes 64-95, where Disc looks for the game title
// The LBA used by the pattern is the sector's position in the whole disc (all files laid end to end), not in its own file.
#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace synthetic_disc {

constexpr uint32_t SECTOR_SIZE = 2352;
constexpr uint32_t VOLUME_NAME_OFFSET = 64;   // 24 bytes of header and sub-header, then the volume identifier at offset 40 of the data
constexpr uint32_t VOLUME_NAME_LENGTH = 32;
constexpr uint32_t VOLUME_SECTOR = 16;
constexpr const char* VOLUME_NAME = "ECHO TEST DISC";

inline uint8_t bcd(uint32_t value) {
	return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

// Byte `offset` (0 to 2351) of the sector at `lba`
inline uint8_t sectorByte(uint32_t lba, uint32_t offset) {
	uint32_t absoluteFrames = lba + 150;
	if (offset == 0 || offset == 11) return 0x00;
	if (offset < 11) return 0xFF;
	if (offset == 12) return bcd(absoluteFrames / (60 * 75));
	if (offset == 13) return bcd((absoluteFrames / 75) % 60);
	if (offset == 14) return bcd(absoluteFrames % 75);
	if (offset == 15) return 0x02;
	if (offset >= 16 && offset < 24) {
		const uint8_t subHeader[8] = {0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00};
		return subHeader[offset - 16];
	}
	if (offset >= 24 && offset < 28) return static_cast<uint8_t>(lba >> (8 * (offset - 24)));
	if (lba == VOLUME_SECTOR && offset >= VOLUME_NAME_OFFSET && offset < VOLUME_NAME_OFFSET + VOLUME_NAME_LENGTH) {
		std::string name = VOLUME_NAME;
		uint32_t position = offset - VOLUME_NAME_OFFSET;
		return position < name.size() ? static_cast<uint8_t>(name[position]) : static_cast<uint8_t>(' ');
	}
	return static_cast<uint8_t>(lba * 37u + offset * 11u + (offset >> 8) * 101u + 0x5Du);
}

// The whole sector, for comparing with what Disc returned
inline std::vector<uint8_t> expectedSector(uint32_t lba) {
	std::vector<uint8_t> sector(SECTOR_SIZE);
	for (uint32_t offset = 0; offset < SECTOR_SIZE; offset++) {
		sector[offset] = sectorByte(lba, offset);
	}
	return sector;
}

// The LBA a returned sector claims to be (bytes 24-27), for failure messages
inline uint32_t markerOf(const uint8_t* sector) {
	return static_cast<uint32_t>(sector[24]) | (static_cast<uint32_t>(sector[25]) << 8) | (static_cast<uint32_t>(sector[26]) << 16) | (static_cast<uint32_t>(sector[27]) << 24);
}

// A temporary directory that is removed, with everything in it, when the Workspace goes away
class Workspace {
public:
	Workspace() {
		static int counter = 0;
		std::error_code error;
		std::filesystem::path base = std::filesystem::temp_directory_path(error);
		if (error) return;
		directory = base / ("echo-test-disc-" + std::to_string(getpid()) + "-" + std::to_string(counter++));
		std::filesystem::create_directories(directory, error);
		if (error) directory.clear();
	}

	Workspace(const Workspace&) = delete;
	Workspace& operator=(const Workspace&) = delete;

	~Workspace() {
		if (directory.empty()) return;
		std::error_code error;
		std::filesystem::remove_all(directory, error);
	}

	bool ok() const { return !directory.empty(); }

	// Full path of a file inside the workspace
	std::string path(const std::string& name) const { return (directory / name).string(); }

	// Writes `wholeSectors` sectors into <name>. The first one is placed at `firstLba` on the disc (the sum of the sizes of the files before it).
	// `extraBytes` adds a partial sector at the end, as a damaged or truncated image would have.
	bool writeBin(const std::string& name, uint32_t firstLba, uint32_t wholeSectors, uint32_t extraBytes = 0) const {
		std::ofstream file(path(name), std::ios::binary);
		for (uint32_t sector = 0; sector < wholeSectors; sector++) {
			std::vector<uint8_t> bytes = expectedSector(firstLba + sector);
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}
		if (extraBytes > 0) {
			std::vector<uint8_t> bytes = expectedSector(firstLba + wholeSectors);
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(extraBytes));
		}
		file.close();
		return !file.fail();
	}

	// Writes the cue sheet text exactly as given (the test supplies its own line endings) and returns its path, or "" if that failed
	std::string writeCue(const std::string& name, const std::string& text) const {
		std::ofstream file(path(name), std::ios::binary);
		file << text;
		file.close();
		return file.fail() ? "" : path(name);
	}

	void removeFile(const std::string& name) const {
		std::error_code error;
		std::filesystem::remove(directory / name, error);
	}

private:
	std::filesystem::path directory;
};

} // namespace synthetic_disc
