// Disc::readSector: does sector N come back as sector N, for a single BIN and for a BIN per track, and what happens at and past the end.
// Needs no disc and no BIOS: the images are generated (support/synthetic_disc.h), and every sector says which sector it is.
//
// readSector returns the whole raw 2352-byte sector (sync, header and sub-header included) for every track type: Disc does not tell Mode 1 from
// Mode 2 or from audio when reading, it only copies bytes from file offset (sector - first sector of the file) * 2352. Where a sector's header
// and data start are interpreted is up to the CD-ROM controller, which is where Mode 1 / Mode 2 layout belongs.
#include "cdrom/Disc.h"
#include "support/captured_output.h"
#include "support/echo_check.h"
#include "support/synthetic_disc.h"
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

using echo_check::check;
using echo_check::checkEqual;
using echo_check::hex;
using synthetic_disc::SECTOR_SIZE;
using synthetic_disc::Workspace;

namespace {

constexpr uint32_t GUARD_BYTES = 64;   // untouched bytes after the 2352 the caller's buffer holds, to catch a write that runs over
constexpr uint8_t GUARD_VALUE = 0xCD;

std::string joinLines(const std::vector<std::string>& lines) {
	std::string text;
	for (const std::string& line : lines) {
		text += line + "\n";
	}
	return text;
}

bool load(Disc& disc, const std::string& cuePath) {
	CapturedOutput output;
	try {
		return disc.loadCue(cuePath);
	} catch (const std::exception& exception) {
		check(false, "loadCue did not throw", exception.what());
		return false;
	}
}

struct ReadResult {
	bool read = false;
	std::string mismatch;   // empty when the bytes are exactly the expected sector
	bool overran = false;   // wrote past the 2352 bytes
};

// Reads sector `lba` and compares all 2352 bytes with what the generator wrote for it
ReadResult readAndCompare(Disc& disc, uint32_t lba) {
	ReadResult result;
	std::vector<uint8_t> buffer(SECTOR_SIZE + GUARD_BYTES, GUARD_VALUE);
	{
		CapturedOutput output;
		result.read = disc.readSector(lba, buffer.data());
	}
	if (!result.read) return result;
	std::vector<uint8_t> expected = synthetic_disc::expectedSector(lba);
	for (uint32_t offset = 0; offset < SECTOR_SIZE; offset++) {
		if (buffer[offset] != expected[offset]) {
			result.mismatch = "byte " + std::to_string(offset) + " is " + hex(buffer[offset], 2) + ", expected " + hex(expected[offset], 2)
								+ " (the sector that came back says it is sector " + std::to_string(synthetic_disc::markerOf(buffer.data())) + ")";
			break;
		}
	}
	for (uint32_t offset = SECTOR_SIZE; offset < SECTOR_SIZE + GUARD_BYTES; offset++) {
		if (buffer[offset] != GUARD_VALUE) {
			result.overran = true;
			break;
		}
	}
	return result;
}

// One check for one sector
void checkSector(Disc& disc, uint32_t lba, const std::string& name) {
	ReadResult result = readAndCompare(disc, lba);
	if (!result.read) {
		check(false, name, "readSector returned false");
	} else {
		check(result.mismatch.empty(), name, result.mismatch);
	}
}

// One check for a run of sectors: reports the first one that is wrong
void checkSectorRange(Disc& disc, uint32_t first, uint32_t count, const std::string& name) {
	for (uint32_t lba = first; lba < first + count; lba++) {
		ReadResult result = readAndCompare(disc, lba);
		if (!result.read || !result.mismatch.empty() || result.overran) {
			std::string reason = !result.read ? "readSector returned false" : !result.mismatch.empty() ? result.mismatch : "wrote past the end of the buffer";
			check(false, name, "sector " + std::to_string(lba) + ": " + reason);
			return;
		}
	}
	check(true, name);
}

bool readFails(Disc& disc, uint32_t lba) {
	std::vector<uint8_t> buffer(SECTOR_SIZE);
	CapturedOutput output;
	return !disc.readSector(lba, buffer.data());
}

// One BIN with three tracks (so the track lookup has several candidates, all in the same file)
void checkSingleBin() {
	Workspace workspace;
	if (!check(workspace.ok(), "single BIN: could create a temporary directory")) return;
	workspace.writeBin("game.bin", 0, 100);
	std::string cuePath = workspace.writeCue("game.cue", joinLines({
		"FILE \"game.bin\" BINARY",
		"  TRACK 01 MODE2/2352", "    INDEX 01 00:00:00",
		"  TRACK 02 AUDIO", "    INDEX 01 00:00:30",
		"  TRACK 03 AUDIO", "    INDEX 01 00:01:05",
	}));
	Disc disc;
	if (!check(load(disc, cuePath), "single BIN: the CUE loads")) return;

	checkSector(disc, 0, "single BIN: sector 0 (the first)");
	checkSector(disc, 16, "single BIN: sector 16 (where the volume identifier is)");
	checkSector(disc, 29, "single BIN: sector 29 (the last before track 2)");
	checkSector(disc, 30, "single BIN: sector 30 (the first of track 2)");
	checkSector(disc, 99, "single BIN: sector 99 (the last)");
	checkSectorRange(disc, 0, 100, "single BIN: every one of the 100 sectors reads back as itself, 2352 bytes each");

	// Not in order, so a read cannot depend on the one before it
	bool scatteredOk = true;
	std::string details;
	for (uint32_t lba : {99u, 0u, 50u, 49u, 51u, 1u, 98u, 30u}) {
		ReadResult result = readAndCompare(disc, lba);
		if (!result.read || !result.mismatch.empty()) {
			scatteredOk = false;
			details = "sector " + std::to_string(lba) + ": " + (result.read ? result.mismatch : "readSector returned false");
			break;
		}
	}
	check(scatteredOk, "single BIN: sectors read out of order come back correct", details);

	// The CD-ROM reads into a 2352-byte buffer: it must get exactly 2352 bytes and nothing past them
	ReadResult overrun = readAndCompare(disc, 7);
	check(overrun.read && !overrun.overran, "single BIN: a read writes exactly 2352 bytes");

	// Past the end
	check(readFails(disc, 100), "single BIN: the sector just past the end cannot be read");
	check(readFails(disc, 101), "single BIN: two sectors past the end cannot be read");
	check(readFails(disc, 1000000), "single BIN: a sector far past the end cannot be read");
	check(readFails(disc, 0xFFFFFFFFu), "single BIN: the largest sector number cannot be read");
	checkSector(disc, 99, "single BIN: a failed read leaves the disc usable (sector 99 still reads)");
}

// A last sector that is incomplete (a truncated image) is not a sector
void checkTruncatedImage() {
	Workspace workspace;
	if (!check(workspace.ok(), "truncated: could create a temporary directory")) return;
	workspace.writeBin("game.bin", 0, 100, 1000);
	std::string cuePath = workspace.writeCue("game.cue", joinLines({"FILE \"game.bin\" BINARY", "  TRACK 01 MODE2/2352", "    INDEX 01 00:00:00"}));
	Disc disc;
	if (!check(load(disc, cuePath), "truncated: the CUE loads")) return;
	checkEqual(disc.getTotalSectors(), 100, "truncated: only whole sectors are counted (100 sectors and 1000 bytes is 100 sectors)");
	checkSector(disc, 99, "truncated: the last whole sector reads");
	check(readFails(disc, 100), "truncated: the 1000-byte stub at the end is not returned as a sector");
}

// A BIN per track: sector numbers count across all the files, each file starting where the previous one ended
void checkMultiFile() {
	Workspace workspace;
	if (!check(workspace.ok(), "multi-file: could create a temporary directory")) return;
	workspace.writeBin("data.bin", 0, 60);
	workspace.writeBin("audio.bin", 60, 300);
	workspace.writeBin("last.bin", 360, 100);
	std::string cuePath = workspace.writeCue("game.cue", joinLines({
		"FILE \"data.bin\" BINARY",
		"  TRACK 01 MODE2/2352", "    INDEX 01 00:00:00",
		"FILE \"audio.bin\" BINARY",
		"  TRACK 02 AUDIO", "    INDEX 00 00:00:00", "    INDEX 01 00:02:00",
		"  TRACK 03 AUDIO", "    INDEX 01 00:03:00",
		"FILE \"last.bin\" BINARY",
		"  TRACK 04 AUDIO", "    INDEX 01 00:01:00",
	}));
	Disc disc;
	if (!check(load(disc, cuePath), "multi-file: the CUE loads")) return;

	checkSector(disc, 0, "multi-file: sector 0 (first sector of the first file)");
	checkSector(disc, 59, "multi-file: sector 59 (last sector of the first file)");
	checkSector(disc, 60, "multi-file: sector 60 (first sector of the second file)");
	checkSector(disc, 359, "multi-file: sector 359 (last sector of the second file)");
	checkSector(disc, 360, "multi-file: sector 360 (first sector of the third file)");
	checkSector(disc, 459, "multi-file: sector 459 (the last on the disc)");
	checkSectorRange(disc, 0, 460, "multi-file: every one of the 460 sectors reads back as itself");

	bool scatteredOk = true;
	std::string details;
	for (uint32_t lba : {459u, 0u, 360u, 59u, 60u, 359u, 200u, 10u, 400u}) {
		ReadResult result = readAndCompare(disc, lba);
		if (!result.read || !result.mismatch.empty()) {
			scatteredOk = false;
			details = "sector " + std::to_string(lba) + ": " + (result.read ? result.mismatch : "readSector returned false");
			break;
		}
	}
	check(scatteredOk, "multi-file: jumping between files in any order reads the right sectors", details);

	check(readFails(disc, 460), "multi-file: the sector just past the end of the last file cannot be read");
	check(readFails(disc, 0xFFFFFFFFu), "multi-file: the largest sector number cannot be read");
}

// The type in the cue says how the sectors are meant to be used, not how they are read: a MODE1/2352 image reads like any other
void checkOtherSectorTypes() {
	Workspace workspace;
	if (!check(workspace.ok(), "sector types: could create a temporary directory")) return;
	workspace.writeBin("game.bin", 0, 20);
	std::string cuePath = workspace.writeCue("game.cue", joinLines({"FILE \"game.bin\" BINARY", "  TRACK 01 MODE1/2352", "    INDEX 01 00:00:00"}));
	Disc disc;
	if (!check(load(disc, cuePath), "sector types: a MODE1/2352 CUE loads")) return;
	checkSectorRange(disc, 0, 20, "sector types: a MODE1/2352 image reads back sector for sector");
}

// Reads on a drive with no disc, and on a disc whose file went away, are failed reads
void checkNothingToRead() {
	{
		Disc disc;
		check(readFails(disc, 0), "no disc: reading sector 0 fails");
		check(readFails(disc, 16), "no disc: reading sector 16 fails");
	}
	{
		Workspace workspace;
		if (!check(workspace.ok(), "file removed: could create a temporary directory")) return;
		workspace.writeBin("game.bin", 0, 20);
		std::string cuePath = workspace.writeCue("game.cue", joinLines({"FILE \"game.bin\" BINARY", "  TRACK 01 MODE2/2352", "    INDEX 01 00:00:00"}));
		Disc disc;
		if (!check(load(disc, cuePath), "file removed: the CUE loads")) return;
		checkSector(disc, 3, "file removed: sector 3 reads while the BIN is there");
		workspace.removeFile("game.bin");
		check(readFails(disc, 3), "file removed: sector 3 fails once the BIN is gone");
	}
}

} // namespace

int main() {
	checkSingleBin();
	checkTruncatedImage();
	checkMultiFile();
	checkOtherSectorTypes();
	checkNothingToRead();
	return echo_check::finish();
}
