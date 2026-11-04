#include "library/GameLibrary.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include "library/GameScanner.h"
#include <filesystem>

namespace {

bool lessIgnoringCase(const std::string& a, const std::string& b) {
	return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
		[](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
}

} // namespace

void GameLibrary::loadDatabase(const std::string& databasePath, const std::string& boxArtFolder) {
	m_database.load(databasePath);
	m_database.setBoxArtFolder(boxArtFolder);
}

void GameLibrary::scan(const std::string& gamesFolder) {
	m_folder = gamesFolder;
	m_games.clear();
	m_allGames.clear();

	for (const ScannedGame& scanned : scanForGames(gamesFolder)) {
		GameInfo info = m_database.describe(scanned);
		const std::string id = info.id;

		// Ignore a second copy of a game that has the same id
		if (m_games.emplace(id, std::move(info)).second) {
			m_allGames.push_back(id);
		}
	}

	// Sort by the name the user sees, which can differ from the folder name
	std::sort(m_allGames.begin(), m_allGames.end(), [this](const std::string& a, const std::string& b) {
		return lessIgnoringCase(m_games.at(a).name, m_games.at(b).name);
	});

	size_t discCount = 0;
	for (const std::string& id : m_allGames) discCount += m_games.at(id).discs.size();
	std::cout << "Echo: Found " << m_allGames.size() << " Games with " << discCount << " discs." << std::endl;
}

std::vector<std::string> GameLibrary::recentlyPlayed(size_t limit) const {
	// Start from the alphabetical list. stable_sort keeps that order among games that tie, which includes all the never-played ones.
	std::vector<std::string> ordered = m_allGames;

	std::stable_sort(ordered.begin(), ordered.end(), [this](const std::string& a, const std::string& b) {
		const std::optional<int64_t>& playedA = m_games.at(a).lastPlayed;
		const std::optional<int64_t>& playedB = m_games.at(b).lastPlayed;

		if (playedA && playedB) return *playedA > *playedB; // newest first
		return playedA.has_value() && !playedB.has_value(); // played games before never-played ones
	});

	ordered.resize(std::min(limit, ordered.size()));
	return ordered;
}


std::string GameLibrary::folderFromDroppedPath(const char* dropped) {
	if (!dropped) return {};

	std::filesystem::path folder(dropped);
	std::error_code errorCode;

	// If a file was dropped, use the folder it sits in
	if (!std::filesystem::is_directory(folder, errorCode)) folder = folder.parent_path();
	if (!std::filesystem::is_directory(folder, errorCode)) return {};

	return folder.string();
}