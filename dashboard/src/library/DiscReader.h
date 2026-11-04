// DiscReader.h
#pragma once

#include <string>

// Reads the disc's serial from SYSTEM.CNF, in "SCUS-94900" form.
// Returns an empty string if the disc can't be read or has no serial.
std::string readDiscSerial(const std::string& cuePath);