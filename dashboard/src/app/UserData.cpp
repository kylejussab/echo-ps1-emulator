#include "app/UserData.h"

namespace {
const std::string lastPlayedKeyPrefix = "lastPlayed.";
const std::string timePlayedKeyPrefix = "playtime.";
}

bool UserData::load() { return m_store.load(); }
bool UserData::save() const { return m_store.save(); }

std::optional<int64_t> UserData::lastPlayed(const std::string& gameId) const {
	const std::string key = lastPlayedKeyPrefix + gameId;
	if (!m_store.has(key)) return std::nullopt;
	return m_store.getInt(key);
}

void UserData::setLastPlayed(const std::string& gameId, int64_t timestamp) {
	m_store.setInt(lastPlayedKeyPrefix + gameId, timestamp);
}

int64_t UserData::timePlayedSeconds(const std::string& gameId) const {
	return m_store.getInt(timePlayedKeyPrefix + gameId, 0);
}

int64_t UserData::addTimePlayedSeconds(const std::string& gameId, int64_t seconds) {
	const int64_t total = timePlayedSeconds(gameId) + seconds;
	m_store.setInt(timePlayedKeyPrefix + gameId, total);
	return total;
}