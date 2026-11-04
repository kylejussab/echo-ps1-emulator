#pragma once

#include <string>
#include <vector>
//#include "library/DiscReader.h"

struct DiscInfo {
	std::string cuePath; // full path to this disc's .cue file
	int discNumber = 1;
	std::string serial;  // e.g. "SCUS-94900", empty if it couldn't be read
};

struct ScannedGame {
	std::string name;           // temporary: the folder/cue name with the "(Disc N)" tag removed
	std::vector<DiscInfo> discs; // sorted by disc number, always at least one
};

// Finds every game under rootFolder. Multi-disc games are merged into a single entry.
// Results are sorted alphabetically by name.
std::vector<ScannedGame> scanForGames(const std::string& rootFolder);