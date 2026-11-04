#pragma once

#include "library/GameInfo.h"
#include "library/GameScanner.h"
#include <string>
#include <unordered_map>

class GameDatabase {
public:
	// Loads lines like:  SCUS-94900|Crash Bandicoot|Naughty Dog
	bool load(const std::string& path);

	// Turns a scanned game into display-ready info, using the dictionary where it can
	GameInfo describe(const ScannedGame& scanned) const;

	// Folder holding box art images named after each game's id, like SLUS-00594.png
	void setBoxArtFolder(const std::string& folder) { boxArtFolder = folder; }

private:
	struct Entry {
		std::string name;
		std::string developer;
	};

	std::unordered_map<std::string, Entry> entries; // keyed by uppercase serial

	std::string boxArtFolder;
};