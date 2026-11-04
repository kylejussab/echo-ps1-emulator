#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "library/GameDatabase.h"
#include "library/GameInfo.h"

// Every game found in the games folder, with the name, box art and discs for each.
class GameLibrary {
public:
	// The dictionary of serial -> name and developer, and where box art images live
	void loadDatabase(const std::string& databasePath, const std::string& boxArtFolder);

	// Scans the folder and replaces the current contents. Remembers the folder it scanned.
	void scan(const std::string& gamesFolder);

	// The games folder a dropped path points at: the path itself if it's a folder, otherwise the folder the dropped file sits in. Empty if neither exists.
	static std::string folderFromDroppedPath(const char* dropped);

	const std::string& folder() const { return m_folder; }
	size_t count() const { return m_allGames.size(); }
	bool empty() const { return m_allGames.empty(); }

	// Game ids, alphabetical by display name
	const std::vector<std::string>& allGames() const { return m_allGames; }

	// Up to `limit` game ids, most recent first
	std::vector<std::string> recentlyPlayed(size_t limit) const;

	const GameInfo& at(const std::string& id) const { return m_games.at(id); }

	void setLastPlayed(const std::string& id, int64_t timestamp) { m_games.at(id).lastPlayed = timestamp; }

	void setTimePlayedSeconds(const std::string& id, int64_t seconds) { m_games.at(id).timePlayedSeconds = seconds; }

private:
	GameDatabase m_database;
	std::string m_folder;
	std::unordered_map<std::string, GameInfo> m_games; // id -> info for every game
	std::vector<std::string> m_allGames;
};