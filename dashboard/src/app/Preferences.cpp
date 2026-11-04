#include "app/Preferences.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

std::string Preferences::filePath() const {
	// Per-user folder, created on demand (on macOS: ~/Library/Application Support/Echo/Echo/)
	char* prefPath = SDL_GetPrefPath(nullptr, "Echo");
	if (!prefPath) return "";

	std::string path = std::string(prefPath) + m_fileName;
	SDL_free(prefPath);
	return path;
}

bool Preferences::load() {
	m_values.clear();

	const std::string path = filePath();
	if (path.empty()) return false;

	std::ifstream file(path);
	if (!file) return true; // first run, nothing saved yet

	std::string line;
	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty() || line[0] == '#') continue;

		const size_t equals = line.find('=');
		if (equals == std::string::npos || equals == 0) continue;

		m_values[line.substr(0, equals)] = line.substr(equals + 1);
	}
	return true;
}

bool Preferences::save() const {
	const std::string path = filePath();
	if (path.empty()) return false;

	// Write to a temp file and rename it over the real one, so a crash mid-write can't corrupt the preferences
	const std::string tempPath = path + ".tmp";
	{
		std::ofstream file(tempPath, std::ios::trunc);
		if (!file) return false;

		file << "# Echo " << m_fileName << "\n";
		for (const auto& entry : m_values) file << entry.first << '=' << entry.second << '\n';
		if (!file) return false;
	}

	std::error_code ec;
	std::filesystem::rename(tempPath, path, ec);
	if (ec) {
		std::cout << "Could not save preferences: " << ec.message() << std::endl;
		return false;
	}
	return true;
}

bool Preferences::has(const std::string& key) const { return m_values.count(key) > 0; }
void Preferences::remove(const std::string& key) { m_values.erase(key); }

std::string Preferences::getString(const std::string& key, const std::string& fallback) const {
	const auto it = m_values.find(key);
	return it == m_values.end() ? fallback : it->second;
}

void Preferences::setString(const std::string& key, const std::string& value) {
	m_values[key] = value;
}

int64_t Preferences::getInt(const std::string& key, int64_t fallback) const {
	const auto it = m_values.find(key);
	if (it == m_values.end()) return fallback;

	char* end = nullptr;
	const long long value = std::strtoll(it->second.c_str(), &end, 10);
	return (end == it->second.c_str()) ? fallback : static_cast<int64_t>(value);
}

void Preferences::setInt(const std::string& key, int64_t value) {
	m_values[key] = std::to_string(value);
}