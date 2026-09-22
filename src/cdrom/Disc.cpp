#include "Disc.h"
#include <sstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

Disc::~Disc() {
    if (binFile.is_open()) {
        binFile.close();
    }
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
    std::string binFilename;
    Track currentTrack;
    bool inTrack = false;

    fs::path basePath = fs::path(cuePath).parent_path();

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
                binFilename = trimmed.substr(firstQuote + 1, lastQuote - firstQuote - 1);
            } else {
                ss >> binFilename;
            }
        }
        else if (command == "TRACK") {
            if (inTrack) {
                tracks.push_back(currentTrack);
            }
            inTrack = true;
            currentTrack = Track();

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
                currentTrack.fileByteOffset = currentTrack.startLBA * 2352;
            }
        }
    }

    if (inTrack) {
        tracks.push_back(currentTrack);
    }
    cueFile.close();

    // Resolve .bin path relative to the .cue location
    fs::path fullBinPath = basePath / binFilename;
    binFile.open(fullBinPath, std::ios::binary);

    if (!binFile.is_open()) {
        std::cerr << "CDROM: Could not open BIN image: " << fullBinPath << std::endl;
        return false;
    }

    binFile.seekg(0, std::ios::end);
    totalBinSizeBytes = binFile.tellg();
    binFile.seekg(0, std::ios::beg);

    std::cout << "CDROM: Loaded disc image " << fullBinPath.filename()
              << " (" << tracks.size() << " tracks, "
              << (totalBinSizeBytes / (1024 * 1024)) << " MB)" << std::endl;

    return true;
}

const Track* Disc::getTrack(int trackNumber) const {
    for (const auto& track : tracks) {
        if (track.number == trackNumber) return &track;
    }
    return nullptr;
}

bool Disc::readSector(uint32_t lba, uint8_t* destination2352) {
    if (!binFile.is_open()) return false;

    uint64_t byteOffset = static_cast<uint64_t>(lba) * 2352;
    if (byteOffset + 2352 > totalBinSizeBytes) {
        std::cerr << "CDROM: Attempted to read out-of-bounds LBA: " << lba << std::endl;
        return false;
    }

    binFile.seekg(byteOffset, std::ios::beg);
    binFile.read(reinterpret_cast<char*>(destination2352), 2352);
    return binFile.gcount() == 2352;
}
