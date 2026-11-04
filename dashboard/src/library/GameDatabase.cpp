#include "library/GameDatabase.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <filesystem>

namespace {

std::string trim(const std::string& text) {
	const size_t first = text.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return "";
	const size_t last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

std::string uppercase(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(),
	               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return text;
}

std::vector<std::string> splitFields(const std::string& line) {
	std::vector<std::string> fields;
	size_t start = 0;
	while (true) {
		const size_t bar = line.find('|', start);
		if (bar == std::string::npos) {
			fields.push_back(trim(line.substr(start)));
			break;
		}
		fields.push_back(trim(line.substr(start, bar - start)));
		start = bar + 1;
	}
	return fields;
}

} // namespace

bool GameDatabase::load(const std::string& path) {
	std::ifstream file(path);
	if (!file) {
		std::cout << "Game database not found: " << path << std::endl;
		return false;
	}

	entries.clear();

	std::string line;
	while (std::getline(file, line)) {
		line = trim(line);
		if (line.empty() || line[0] == '#') continue; // blank lines and comments

		const std::vector<std::string> fields = splitFields(line);
		if (fields.size() < 3 || fields[0].empty() || fields[1].empty()) {
			std::cout << "Skipping malformed game database line: " << line << std::endl;
			continue;
		}

		// Extra fields (genre and so on) can be added after the developer later
		entries[uppercase(fields[0])] = Entry{ fields[1], fields[2] };
	}

	return true;
}

GameInfo GameDatabase::describe(const ScannedGame& scanned) const {
	GameInfo info;
	info.discs = scanned.discs;

	const DiscInfo& firstDisc = scanned.discs.front();
	info.id = firstDisc.serial.empty() ? firstDisc.cuePath : firstDisc.serial;

    // Box art is optional. It's a file named after the game's id, like boxart/SLUS-00594.png
	if (!firstDisc.serial.empty() && !boxArtFolder.empty()) {
		for (const char* extension : { ".png", ".jpg" }) {
			const std::filesystem::path candidate = std::filesystem::path(boxArtFolder) / (info.id + extension);

			std::error_code errorCode;
			if (std::filesystem::is_regular_file(candidate, errorCode)) {
				info.boxArtPath = candidate.string();
				break;
			}
		}
	}

	// Fallbacks for games that aren't in the dictionary
	info.name = scanned.name;
	info.developer = "Unknown";

	// Any disc of the game can match, so a multi-disc game only needs one line to be listed
	for (const DiscInfo& disc : scanned.discs) {
		if (disc.serial.empty()) continue;

		const auto it = entries.find(uppercase(disc.serial));
		if (it != entries.end()) {
			info.name = it->second.name;
			info.developer = it->second.developer;
			break;
		}
	}

	return info;
}