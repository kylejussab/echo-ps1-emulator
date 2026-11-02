#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>


/// @brief Represents the optical media inserted into the CD-ROM drive, handling CUE sheet parsing, track layout management, and raw sector data retrieval.
class Disc {
public:
    /// @brief Defines the type of data contained within a specific track on the disc.
    enum class TrackType {
        Mode2_2352,
        Audio,
        Unknown
    };


    /// @brief Represents a single track on the disc, including its timing and file information.
    struct Track {
        int number = 0;
        TrackType type = TrackType::Unknown;
        uint32_t startLogicalBlockAddress = 0;      // Logical Block Address of INDEX 01 (where the track's audio/data really starts)
        uint32_t fileStartLogicalBlockAddress = 0;  // Logical Block Address of the first sector of the file this track lives in
        uint32_t indexLogicalBlockAddress = 0;      // INDEX 01 as written in the CUE: relative to the start of the file
        uint32_t fileByteOffset = 0;
        std::string filename;
    };

    
    /// @brief Loads and parses a CUE sheet to construct the disc's track layout.
    /// @param cuePath The file system path to the .cue file.
    /// @return True if the CUE sheet was loaded and parsed successfully, false otherwise.
    bool loadCue(const std::string& cuePath);


    /// @brief Reads a raw 2352-byte sector from the disc into the provided destination buffer.
    /// @param logicalBlockAddress The Logical Block Address of the sector to read.
    /// @param destination2352 Pointer to the buffer where the 2352 bytes will be written.
    /// @return True if the sector was read successfully, false otherwise.
    bool readSector(uint32_t logicalBlockAddress, uint8_t* destination2352);


    /// @brief Retrieves a pointer to a specific track by its track number.
    /// @param trackNumber The number of the track to retrieve.
    /// @return A constant pointer to the Track structure, or nullptr if the track does not exist.
    const Track* getTrack(int trackNumber) const;
    

    /// @brief Checks if a disc (CUE sheet) has been successfully loaded.
    /// @return True if at least one track is loaded, false otherwise.
    bool isLoaded() const { return !tracks.empty(); }


    /// @brief Retrieves the total number of tracks currently loaded on the disc.
    /// @return The total track count.
    uint32_t getTrackCount() const { return static_cast<uint32_t>(tracks.size()); }
    

    /// @brief Retrieves the total number of sectors on the disc.
    /// @return The total sector count.
    uint32_t getTotalSectors() const { return totalSectors; }

private:
    std::vector<Track> tracks;
    uint32_t totalSectors = 0;

    /// @brief Converts a Minute/Second/Frame timestamp into a Logical Block Address.
    /// @param minutes The minute component of the timestamp.
    /// @param seconds The second component of the timestamp.
    /// @param frames The frame component of the timestamp.
    /// @return The calculated Logical Block Address.
    static uint32_t minuteSecondFrameToLogicalBlockAddress(int minutes, int seconds, int frames) { return (minutes * 60 + seconds) * 75 + frames; };


    /// @brief Removes leading and trailing whitespace from a given string.
    /// @param stringToTrim The string to be trimmed.
    /// @return A new string with whitespace removed from both ends.
    static std::string trim(const std::string& stringToTrim);
};
