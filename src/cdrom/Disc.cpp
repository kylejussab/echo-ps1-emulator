#include "Disc.h"
#include <sstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

Disc::~Disc() {

}

uint32_t Disc::msfToLba(int minutes, int seconds, int frames) {
    return (minutes * 60 + seconds) * 75 + frames;
}

std::string Disc::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

bool Disc::loadCue(const std::string& cuePath) {
    std::ifstream cueFile(cuePath);
    if (!cueFile.is_open()) {
        std::cerr << "CDROM: Could not open CUE sheet: " << cuePath << std::endl;
        return false;
    }

    tracks.clear();
    std::string line;
    Track currentTrack;
    bool inTrack = false;

    fs::path basePath = fs::path(cuePath).parent_path();
    std::string currentBinFilename; // Consolidated to a single tracking variable

    while (std::getline(cueFile, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        std::stringstream ss(trimmed);
        std::string command;
        ss >> command;

        if (command == "FILE") {
            // Extract the path inside quotes
            size_t firstQuote = trimmed.find('"');
            size_t lastQuote = trimmed.rfind('"');
            if (firstQuote != std::string::npos && lastQuote != std::string::npos && firstQuote != lastQuote) {
                currentBinFilename = trimmed.substr(firstQuote + 1, lastQuote - firstQuote - 1);
            } else {
                ss >> currentBinFilename;
            }
        }
        else if (command == "TRACK") {
            if (inTrack) {
                tracks.push_back(currentTrack);
            }
            inTrack = true;
            currentTrack = Track();
            
            // Store the absolute path so readSector() can open it directly
            currentTrack.filename = (basePath / currentBinFilename).string();

            int trackNum = 0;
            std::string typeStr;
            ss >> trackNum >> typeStr;

            currentTrack.number = trackNum;
            if (typeStr == "MODE2/2352") {
                currentTrack.type = TrackType::Mode2_2352;
            } else if (typeStr == "AUDIO") {
                currentTrack.type = TrackType::Audio;
            } else {
                currentTrack.type = TrackType::Unknown;
            }
        }
        else if (command == "INDEX") {
            int indexNum = 0;
            std::string msfStr;
            ss >> indexNum >> msfStr;

            // We care primarily about INDEX 01 (track data start)
            if (indexNum == 1 && inTrack) {
                int mm = 0, ss_val = 0, ff = 0;
                char colon1 = 0, colon2 = 0;
                std::stringstream msfStream(msfStr);
                msfStream >> mm >> colon1 >> ss_val >> colon2 >> ff;

                currentTrack.startLBA = msfToLba(mm, ss_val, ff);
                
                // For Multi-BIN rips, the local file offset is almost always 0.
                // We will handle the LBA math dynamically in readSector.
                currentTrack.fileByteOffset = 0; 
            }
        }
    }

    if (inTrack) {
        tracks.push_back(currentTrack);
    }


    uint32_t globalLBA = 0;
    std::string lastFilename;
    for (auto& track : tracks) {
        if (track.filename != lastFilename) {
            track.startLBA = globalLBA;
            lastFilename = track.filename;

            uintmax_t fileSize = fs::file_size(track.filename);
            globalLBA += static_cast<uint32_t>(fileSize / 2352);
        } else {
            track.startLBA = globalLBA;
        }
    }

    totalSectors = globalLBA;

    cueFile.close();

    std::cout << "CDROM: Loaded CUE sheet with " << tracks.size() << " tracks." << std::endl;

    return true;
}



const Track* Disc::getTrack(int trackNumber) const {
    for (const auto& track : tracks) {
        if (track.number == trackNumber) return &track;
    }
    return nullptr;
}






bool Disc::readSector(uint32_t lba, uint8_t* destination2352) {
    const Track* targetTrack = nullptr;

    // 1. Find which track contains this LBA
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (lba >= tracks[i].startLBA) {
            targetTrack = &tracks[i];
        } else {
            break;
        }
    }

    if (!targetTrack) {
        std::cerr << "CDROM: LBA " << lba << " is out of bounds." << std::endl;
        return false;
    }

    // 2. Find the LBA where this specific file physically begins
    uint32_t fileStartLBA = 0;
    for (const auto& track : tracks) {
        if (track.filename == targetTrack->filename) {
            fileStartLBA = track.startLBA;
            break; // Found the very first track that lives in this file
        }
    }

    // 3. Open the file
    std::ifstream trackFile(targetTrack->filename, std::ios::binary);
    if (!trackFile.is_open()) {
        std::cerr << "CDROM: Could not open track file: " << targetTrack->filename << std::endl;
        return false;
    }

    // 4. Calculate offset relative to the start of THIS file
    uint32_t lbaOffsetWithinFile = lba - fileStartLBA;
    uint64_t byteOffset = static_cast<uint64_t>(lbaOffsetWithinFile) * 2352;

    trackFile.seekg(byteOffset, std::ios::beg);
    trackFile.read(reinterpret_cast<char*>(destination2352), 2352);
    
    bool success = trackFile.gcount() == 2352;
    trackFile.close();
    
    return success;
}
