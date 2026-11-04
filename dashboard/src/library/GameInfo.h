#pragma once

#include "library/GameScanner.h" // for DiscInfo
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

// Everything the UI knows about one game. Add genre, release year, etc. here later.
struct GameInfo {
	std::string id;                // unique key (serial of disc 1, or the cue path if there's no serial)
	std::string name;              // display name, e.g. "Crash Team Racing"
	std::string developer;         // e.g. "Naughty Dog", or "Unknown"
	std::vector<DiscInfo> discs;   // one entry per disc, sorted by disc number. Always at least one.
    std::string boxArtPath;        // full path to the box art image, empty if the game has none
	std::optional<int64_t> lastPlayed; // Unix time (seconds) of the last launch, empty if never played
	int64_t timePlayedSeconds = 0; // total time spent actively in the game
};