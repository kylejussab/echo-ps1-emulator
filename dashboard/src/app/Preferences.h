#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>

// Small key=value store saved in the user's per-app preferences folder.
// One entry per line ("key=value"), lines starting with # are ignored.
// Later entries can be things like "playtime.SCUS-94900=7260" (seconds).
class Preferences {
public:
	explicit Preferences(std::string fileName = "preferences.cfg") : m_fileName(std::move(fileName)) {}

	bool load();        // A missing file is fine: you just start empty
	bool save() const;

	bool has(const std::string& key) const;
	void remove(const std::string& key);

	std::string getString(const std::string& key, const std::string& fallback = "") const;
	void setString(const std::string& key, const std::string& value);

	int64_t getInt(const std::string& key, int64_t fallback = 0) const;
	void setInt(const std::string& key, int64_t value);

private:
	std::string m_fileName;
	std::string filePath() const;
	std::map<std::string, std::string> m_values;
};