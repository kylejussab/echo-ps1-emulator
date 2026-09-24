#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

enum class TrackType {
    Mode2_2352,
    Audio,
    Unknown
};

struct Track {
    int number = 0;
    TrackType type = TrackType::Unknown;
    uint32_t startLBA = 0;
    uint32_t fileByteOffset = 0;
    std::string filename;
};

class Disc {
public:
    Disc() = default;
    ~Disc();

    bool loadCue(const std::string& cuePath);
    bool readSector(uint32_t lba, uint8_t* destination2352);
    
    uint32_t getTrackCount() const { return static_cast<uint32_t>(tracks.size()); }
    const Track* getTrack(int trackNumber) const;
    bool isLoaded() const { return !tracks.empty(); }

    uint32_t getTotalSectors() const { return totalSectors; }

private:
    std::vector<Track> tracks;
    uint32_t totalSectors = 0;

    static uint32_t msfToLba(int minutes, int seconds, int frames);
    static std::string trim(const std::string& str);
};
