// A made-up 512 KB BIOS image so Bus tests do not need a real BIOS (which cannot be committed). Every byte follows a fixed pattern,
// so a test can tell exactly what the Bus returned and where it came from.
#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace synthetic_bios {

constexpr uint32_t SIZE = 512 * 1024;

inline uint8_t byteAt(uint32_t offset) {
	return static_cast<uint8_t>((offset * 167u) ^ (offset >> 9) ^ 0x5Au);
}

inline uint16_t halfwordAt(uint32_t offset) {
	return static_cast<uint16_t>(byteAt(offset) | (byteAt(offset + 1) << 8));
}

inline uint32_t wordAt(uint32_t offset) {
	return static_cast<uint32_t>(halfwordAt(offset)) | (static_cast<uint32_t>(halfwordAt(offset + 2)) << 16);
}

// Writes the image to the temp directory and returns its path, or an empty string when that did not work
inline std::string createFile() {
	std::error_code error;
	std::filesystem::path path = std::filesystem::temp_directory_path(error) / ("echo-test-bios-" + std::to_string(getpid()) + ".bin");
	if (error) {
		return "";
	}
	std::vector<char> image(SIZE);
	for (uint32_t offset = 0; offset < SIZE; offset++) {
		image[offset] = static_cast<char>(byteAt(offset));
	}
	std::ofstream file(path, std::ios::binary);
	file.write(image.data(), image.size());
	file.close();
	return file.fail() ? "" : path.string();
}

inline void removeFile(const std::string& path) {
	std::error_code error;
	std::filesystem::remove(path, error);
}

} // namespace synthetic_bios
