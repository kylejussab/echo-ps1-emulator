#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include "Disc.h"


bool Disc::loadCue(const std::string& cuePath) {
    if (cuePath.empty()) {
        std::cout << "CDROM: Drive is empty. Booting to BIOS shell." << std::endl;
        return false;
    }
    
    std::ifstream cueFile(cuePath);
    if (!cueFile.is_open()) {
        std::cout << "CDROM: Could not open CUE sheet: " << cuePath << std::endl;
        return false;
    }

    tracks.clear();
    std::string line;
    Track currentTrack;
    bool inTrack = false;

    std::filesystem::path basePath = std::filesystem::path(cuePath).parent_path();
    std::string currentBinFilename;

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
            } 
            else {
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

            int trackNumber = 0;
            std::string typeString;
            ss >> trackNumber >> typeString;

            currentTrack.number = trackNumber;
            if (typeString == "MODE2/2352") {
                currentTrack.type = TrackType::Mode2_2352;
            } 
            else if (typeString == "AUDIO") {
                currentTrack.type = TrackType::Audio;
            } 
            else {
                currentTrack.type = TrackType::Unknown;
            }
        }
        else if (command == "INDEX") {
            int indexNumber = 0;
            std::string minuteSecondFrameString;
            ss >> indexNumber >> minuteSecondFrameString;

            // INDEX 01 designates the start of the actual track data.
            if (indexNumber == 1 && inTrack) {
                int minutes = 0, seconds = 0, frames = 0;
                char colon1 = 0, colon2 = 0;
                std::stringstream minuteSecondFrameStream(minuteSecondFrameString);
                minuteSecondFrameStream >> minutes >> colon1 >> seconds >> colon2 >> frames;

                currentTrack.indexLogicalBlockAddress = minuteSecondFrameToLogicalBlockAddress(minutes, seconds, frames);
                
                // For Multi-BIN rips, the local file offset is strictly initialized to 0.
                // Logical Block Address offsets are calculated dynamically during sector reads.
                currentTrack.fileByteOffset = 0; 
            }
        }
    }

    if (inTrack) {
        tracks.push_back(currentTrack);
    }

    uint32_t globalLogicalBlockAddress = 0;
    uint32_t currentFileStartLogicalBlockAddress = 0;
    std::string lastFilename;
    for (auto& track : tracks) {
        if (track.filename != lastFilename) {
            currentFileStartLogicalBlockAddress = globalLogicalBlockAddress;
            lastFilename = track.filename;

            uintmax_t fileSize = std::filesystem::file_size(track.filename);
            globalLogicalBlockAddress += static_cast<uint32_t>(fileSize / 2352);
        }
        track.fileStartLogicalBlockAddress = currentFileStartLogicalBlockAddress;

        // INDEX 01 is relative to the start of the track's file. In a single-BIN image that is the real track start,
        // in a multi-BIN image it is the pregap length when the file includes the pregap (usually 00:02:00 for audio).
        track.startLogicalBlockAddress = currentFileStartLogicalBlockAddress + track.indexLogicalBlockAddress;
    }

    totalSectors = globalLogicalBlockAddress;

    cueFile.close();

     // Read Sector 16 (ISO 9660 Primary Volume Descriptor) to get the game name
    uint8_t sector[2352];

    if (readSector(16, sector)) {
        // Mode 2 Form 1 sector: Data starts at byte 24. 
        // Volume Identifier is a 32-byte string at offset 40 of the data payload (24 + 40 = 64).
        char volumeID[33] = {0};
        std::memcpy(volumeID, &sector[64], 32);

        std::string gameName(volumeID);
        
        // Trim trailing spaces and null characters
        size_t lastChar = gameName.find_last_not_of(" \t\r\n\0");
        if (lastChar != std::string::npos) {
            gameName.erase(lastChar + 1);
        } 
        else {
            gameName.clear();
        }

        if (!gameName.empty()) {
            std::cout << "CDROM: Inserted Disc - " << gameName << " (" << tracks.size() << " tracks)" << std::endl;
        } 
        else {
            std::cout << "CDROM: Inserted Disc - Unknown Title (" << tracks.size() << " tracks)" << std::endl;
        }
    } 
    else {
        std::cout << "CDROM: Loaded CUE sheet with " << tracks.size() << " tracks." << std::endl;
    }

    return true;
}


bool Disc::readSector(uint32_t logicalBlockAddress, uint8_t* destination2352) {
    const Track* targetTrack = nullptr;

    // Find which track contains this Logical Block Address
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (logicalBlockAddress >= tracks[i].fileStartLogicalBlockAddress) {
            targetTrack = &tracks[i];
        } 
        else {
            break;
        }
    }

    if (!targetTrack) {
        std::cerr << "CDROM: Logical Block Address " << logicalBlockAddress << " is out of bounds." << std::endl;
        return false;
    }

    // The Logical Block Address where this specific file physically begins
    uint32_t fileStartLogicalBlockAddress = targetTrack->fileStartLogicalBlockAddress;

    std::ifstream trackFile(targetTrack->filename, std::ios::binary);
    if (!trackFile.is_open()) {
        std::cerr << "CDROM: Could not open track file: " << targetTrack->filename << std::endl;
        return false;
    }

    // Calculate offset relative to the start of THIS file
    uint32_t logicalBlockAddressOffsetWithinFile = logicalBlockAddress - fileStartLogicalBlockAddress;
    uint64_t byteOffset = static_cast<uint64_t>(logicalBlockAddressOffsetWithinFile) * 2352;

    trackFile.seekg(byteOffset, std::ios::beg);
    trackFile.read(reinterpret_cast<char*>(destination2352), 2352);
    
    bool success = trackFile.gcount() == 2352;
    trackFile.close();
    
    return success;
}


const Disc::Track* Disc::getTrack(int trackNumber) const {
    for (const auto& track : tracks) {
        if (track.number == trackNumber) return &track;
    }
    return nullptr;
}



//Privates
std::string Disc::trim(const std::string& stringToTrim) {
    size_t first = stringToTrim.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = stringToTrim.find_last_not_of(" \t\r\n");
    return stringToTrim.substr(first, (last - first + 1));
}
