// GameScanner.cpp
#include "library/GameScanner.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include "library/DiscReader.h"

namespace fs = std::filesystem;

namespace {

std::string lowercase(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

// Finds a "(Disc N)" tag, removes it from `name` and returns N. Returns 1 if there is no tag.
int extractDiscNumber(std::string& name) {
	const std::string lower = lowercase(name);

	size_t start = lower.find("(disc ");
	while (start != std::string::npos) {
		size_t digitsStart = start + 6;
		size_t digitsEnd = digitsStart;
		while (digitsEnd < lower.size() && std::isdigit(static_cast<unsigned char>(lower[digitsEnd]))) ++digitsEnd;

		if (digitsEnd > digitsStart && digitsEnd < lower.size() && lower[digitsEnd] == ')') {
			const int number = std::stoi(lower.substr(digitsStart, digitsEnd - digitsStart));

			// Also remove the space before the tag, so we don't leave a double space behind
			size_t eraseFrom = start;
			if (eraseFrom > 0 && name[eraseFrom - 1] == ' ') --eraseFrom;
			name.erase(eraseFrom, digitsEnd + 1 - eraseFrom);
			return number;
		}
		start = lower.find("(disc ", start + 1);
	}

	return 1;
}

} // namespace

std::vector<ScannedGame> scanForGames(const std::string& rootFolder) {
	// Keyed by lowercase name so discs of the same game land in the same entry
	std::map<std::string, ScannedGame> byName;

	std::error_code ec;
	if (!fs::is_directory(rootFolder, ec)) {
		std::cout << "Games folder not found: " << rootFolder << std::endl;
		return {};
	}

	fs::recursive_directory_iterator it(rootFolder, fs::directory_options::skip_permission_denied, ec);
	const fs::recursive_directory_iterator end;

	for (; !ec && it != end; it.increment(ec)) {
		if (!it->is_regular_file(ec)) continue;

		const fs::path& path = it->path();
		const std::string filename = path.filename().string();

		// Skip hidden files, like macOS's ._ copies and .DS_Store
		if (filename.empty() || filename[0] == '.') continue;
		if (lowercase(path.extension().string()) != ".cue") continue;

		std::string name = path.stem().string();
		const int discNumber = extractDiscNumber(name);

		ScannedGame& game = byName[lowercase(name)];
		if (game.name.empty()) game.name = name;
		game.discs.push_back({ path.string(), discNumber, readDiscSerial(path.string()) });
	}

	std::vector<ScannedGame> games;
	games.reserve(byName.size());
	for (auto& entry : byName) {
		ScannedGame& game = entry.second;
		std::sort(game.discs.begin(), game.discs.end(),
		          [](const DiscInfo& a, const DiscInfo& b) { return a.discNumber < b.discNumber; });
		games.push_back(std::move(game));
	}

	// std::map already iterates in sorted key order, so `games` is alphabetical
	return games;
}