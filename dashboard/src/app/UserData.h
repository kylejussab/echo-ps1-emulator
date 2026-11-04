#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include "app/Preferences.h"

// What Echo remembers about the player's games: when each was last played, how long it has been played. Kept apart from settings (Preferences).
// Nothing outside this class knows how or where it is stored.
class UserData {
public:
	bool load();
	bool save() const;

	std::optional<int64_t> lastPlayed(const std::string& gameId) const; // Unix time in seconds, empty if never played
	void setLastPlayed(const std::string& gameId, int64_t timestamp);

	int64_t timePlayedSeconds(const std::string& gameId) const;
	int64_t addTimePlayedSeconds(const std::string& gameId, int64_t seconds); // returns the new total

private:
	Preferences m_store{ "userdata.cfg" };
};