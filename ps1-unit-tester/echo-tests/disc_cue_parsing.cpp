// Disc: reading a cue sheet into a track table. Checks the track numbers and types, the INDEX 01 timestamps (minute:second:frame to
// sector number), how a single BIN and a BIN per track are laid out end to end, how the cue syntax is tolerated, and that a broken
// image fails cleanly instead of crashing. Needs no disc and no BIOS: the images are generated (support/synthetic_disc.h).
//
// A note on the 150-sector pregap. Disc numbers sectors from 0 = 00:02:00 and does not add or remove the 2-second lead-in anywhere:
// the INDEX 01 value in a cue is a position inside its file, and that is what Disc stores. The +150 / -150 between a sector number
// and the minute:second:frame the program sees is done by the CD-ROM controller (Setloc, GetlocP, GetTD), so that is where it has to be tested.
//
// The sector numbers below are worked out by hand from the cue text (75 frames per second, 60 seconds per minute), not with Disc's own helper.
#include "cdrom/Disc.h"
#include "support/captured_output.h"
#include "support/echo_check.h"
#include "support/isolated_probe.h"
#include "support/synthetic_disc.h"
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <string>
#include <vector>

using echo_check::check;
using echo_check::checkEqual;
using synthetic_disc::Workspace;

namespace {

std::string joinLines(const std::vector<std::string>& lines, const std::string& lineEnding = "\n") {
	std::string text;
	for (const std::string& line : lines) {
		text += line + lineEnding;
	}
	return text;
}

// Loads a cue sheet without letting Disc's console messages through; `printed` receives them
// An exception out of loadCue would end the whole test (nothing in the emulator catches it either), so it is turned into a failed check here
bool load(Disc& disc, const std::string& cuePath, std::string* printed = nullptr) {
	CapturedOutput output;
	bool loaded = false;
	try {
		loaded = disc.loadCue(cuePath);
	} catch (const std::exception& exception) {
		check(false, "loadCue did not throw", exception.what());
	}
	if (printed != nullptr) *printed = output.text();
	return loaded;
}

struct ExpectedTrack {
	int number;
	Disc::TrackType type;
	uint32_t indexInFile;   // INDEX 01 as written in the cue
	uint32_t fileStart;     // first sector of the track's file on the disc
	uint32_t start;         // fileStart + indexInFile
	std::string filename;
};

void checkTrack(const Disc& disc, const ExpectedTrack& expected, const std::string& context) {
	std::string prefix = context + ": track " + std::to_string(expected.number) + " ";
	const Disc::Track* track = disc.getTrack(expected.number);
	if (!check(track != nullptr, prefix + "exists")) return;
	check(track->type == expected.type, prefix + "has the right type", "type " + std::to_string(static_cast<int>(track->type)));
	checkEqual(track->indexLogicalBlockAddress, expected.indexInFile, prefix + "INDEX 01 as written in the cue");
	checkEqual(track->fileStartLogicalBlockAddress, expected.fileStart, prefix + "file starts at the right sector");
	checkEqual(track->startLogicalBlockAddress, expected.start, prefix + "starts at the right sector");
	check(track->filename == expected.filename, prefix + "points at its BIN file", "got '" + track->filename + "', expected '" + expected.filename + "'");
}

// One BIN with three tracks: the track table, the sector numbers, and the title read from sector 16
void checkSingleBinLayout() {
	Workspace workspace;
	if (!check(workspace.ok(), "single BIN: could create a temporary directory")) return;
	workspace.writeBin("game.bin", 0, 100);
	std::string cuePath = workspace.writeCue("game.cue", joinLines({
		"FILE \"game.bin\" BINARY",
		"  TRACK 01 MODE2/2352",
		"    INDEX 01 00:00:00",
		"  TRACK 02 AUDIO",
		"    INDEX 00 00:00:28",
		"    INDEX 01 00:00:30",
		"  TRACK 03 AUDIO",
		"    INDEX 01 00:01:05",
	}));

	Disc disc;
	std::string printed;
	check(!disc.isLoaded(), "single BIN: a new Disc is empty");
	check(load(disc, cuePath, &printed), "single BIN: the CUE loads");
	check(disc.isLoaded(), "single BIN: the disc reports itself loaded");
	checkEqual(disc.getTrackCount(), 3, "single BIN: three tracks");
	checkEqual(disc.getTotalSectors(), 100, "single BIN: the total is the size of the BIN, counted once for three tracks");

	std::string bin = workspace.path("game.bin");
	checkTrack(disc, {1, Disc::TrackType::Mode2_2352, 0, 0, 0, bin}, "single BIN");
	checkTrack(disc, {2, Disc::TrackType::Audio, 30, 0, 30, bin}, "single BIN");
	checkTrack(disc, {3, Disc::TrackType::Audio, 80, 0, 80, bin}, "single BIN");

	check(disc.getTrack(0) == nullptr, "single BIN: there is no track 0");
	check(disc.getTrack(4) == nullptr, "single BIN: there is no track 4");
	check(disc.getTrack(-1) == nullptr, "single BIN: there is no track -1");

	check(printed.find(synthetic_disc::VOLUME_NAME) != std::string::npos && printed.find("3 tracks") != std::string::npos,
		"single BIN: the title is read from the volume identifier of sector 16", "printed: " + printed);
}

// INDEX 01 is minute:second:frame at 75 frames per second. The CUE is read for its INDEX 01 only, so the value has to come out right at the extremes.
void checkTimestampConversion() {
	Workspace workspace;
	if (!check(workspace.ok(), "timestamps: could create a temporary directory")) return;
	workspace.writeBin("t.bin", 0, 20);
	std::string bin = workspace.path("t.bin");

	struct Case {
		const char* timestamp;
		uint32_t sector;
		const char* why;
	};
	const Case cases[] = {
		{"00:00:00", 0, "the start"},
		{"00:00:01", 1, "one frame"},
		{"00:00:74", 74, "the last frame of a second"},
		{"00:01:00", 75, "one second is 75 frames"},
		{"00:02:00", 150, "two seconds, the usual lead-in length"},
		{"00:59:74", 4499, "the last frame of a minute"},
		{"01:00:00", 4500, "one minute is 4500 frames"},
		{"02:03:04", 9229, "minutes, seconds and frames together"},
		{"74:59:74", 337499, "the end of a 74-minute disc"},
		{"79:59:74", 359999, "the end of an 80-minute disc"},
	};
	for (const Case& testCase : cases) {
		std::string cuePath = workspace.writeCue("t.cue", joinLines({
			"FILE \"t.bin\" BINARY",
			"  TRACK 01 AUDIO",
			std::string("    INDEX 01 ") + testCase.timestamp,
		}));
		Disc disc;
		std::string name = std::string("INDEX 01 ") + testCase.timestamp + " (" + testCase.why + ")";
		if (!check(load(disc, cuePath), name + ": the CUE loads")) continue;
		const Disc::Track* track = disc.getTrack(1);
		if (!check(track != nullptr, name + ": track 1 exists")) continue;
		checkEqual(track->indexLogicalBlockAddress, testCase.sector, name + " is sector " + std::to_string(testCase.sector));
		checkEqual(track->startLogicalBlockAddress, testCase.sector, name + ": a single BIN starts the track at the same sector");
	}
}

// Only INDEX 01 marks where a track really starts: INDEX 00 (the pregap) and INDEX 02 and up (sub-indexes) must not move it
void checkOtherIndexes() {
	Workspace workspace;
	if (!check(workspace.ok(), "other indexes: could create a temporary directory")) return;
	workspace.writeBin("game.bin", 0, 100);
	std::string cuePath = workspace.writeCue("game.cue", joinLines({
		"FILE \"game.bin\" BINARY",
		"  TRACK 01 AUDIO",
		"    INDEX 00 00:00:00",
		"    INDEX 01 00:00:20",
		"    INDEX 02 00:00:40",
		"  TRACK 02 AUDIO",
		"    INDEX 01 00:01:00",
	}));
	Disc disc;
	if (!check(load(disc, cuePath), "other indexes: the CUE loads")) return;
	const Disc::Track* first = disc.getTrack(1);
	if (check(first != nullptr, "other indexes: track 1 exists")) {
		checkEqual(first->startLogicalBlockAddress, 20, "other indexes: INDEX 00 and INDEX 02 do not move the start of the track (INDEX 01)");
	}
	const Disc::Track* second = disc.getTrack(2);
	if (check(second != nullptr, "other indexes: track 2 exists")) {
		checkEqual(second->startLogicalBlockAddress, 75, "other indexes: the next track still starts at its own INDEX 01");
	}
}

// Cue sheets come from many tools: Windows line endings, indentation, metadata lines, unquoted names
void checkCueSyntax() {
	Workspace workspace;
	if (!check(workspace.ok(), "cue syntax: could create a temporary directory")) return;
	workspace.writeBin("game.bin", 0, 60);
	workspace.writeBin("my game (track 1).bin", 0, 60);
	const std::vector<std::string> body = {
		"FILE \"game.bin\" BINARY",
		"  TRACK 01 MODE2/2352",
		"    INDEX 01 00:00:00",
		"  TRACK 02 AUDIO",
		"    INDEX 01 00:00:30",
	};

	struct Variant {
		const char* name;
		std::string text;
		std::string binName;
	};
	std::vector<Variant> variants;
	variants.push_back({"Windows line endings (CRLF)", joinLines(body, "\r\n"), "game.bin"});
	variants.push_back({"no newline after the last line", joinLines(body).substr(0, joinLines(body).size() - 1), "game.bin"});
	variants.push_back({"blank lines and tabs", "\n\nFILE \"game.bin\" BINARY\n\n\tTRACK 01 MODE2/2352\n\t\tINDEX 01 00:00:00\n\n\tTRACK 02 AUDIO\n\t\tINDEX 01 00:00:30\n\n", "game.bin"});
	variants.push_back({"metadata lines (REM, CATALOG, PERFORMER, TITLE, FLAGS)", joinLines({
		"REM GENRE Test",
		"CATALOG 0000000000000",
		"PERFORMER \"Someone\"",
		"TITLE \"A title\"",
		"FILE \"game.bin\" BINARY",
		"  TRACK 01 MODE2/2352",
		"    TITLE \"Track one\"",
		"    FLAGS DCP",
		"    INDEX 01 00:00:00",
		"  TRACK 02 AUDIO",
		"    PERFORMER \"Someone else\"",
		"    INDEX 01 00:00:30",
	}), "game.bin"});
	variants.push_back({"an unquoted file name", joinLines({
		"FILE game.bin BINARY",
		"  TRACK 01 MODE2/2352",
		"    INDEX 01 00:00:00",
		"  TRACK 02 AUDIO",
		"    INDEX 01 00:00:30",
	}), "game.bin"});
	variants.push_back({"a quoted file name with spaces and brackets", joinLines({
		"FILE \"my game (track 1).bin\" BINARY",
		"  TRACK 01 MODE2/2352",
		"    INDEX 01 00:00:00",
		"  TRACK 02 AUDIO",
		"    INDEX 01 00:00:30",
	}), "my game (track 1).bin"});

	for (const Variant& variant : variants) {
		std::string name = std::string("cue syntax, ") + variant.name;
		std::string cuePath = workspace.writeCue("variant.cue", variant.text);
		Disc disc;
		if (!check(load(disc, cuePath), name + ": the CUE loads")) continue;
		checkEqual(disc.getTrackCount(), 2, name + ": two tracks");
		std::string bin = workspace.path(variant.binName);
		checkTrack(disc, {1, Disc::TrackType::Mode2_2352, 0, 0, 0, bin}, name);
		checkTrack(disc, {2, Disc::TrackType::Audio, 30, 0, 30, bin}, name);
	}
}

// MODE2/2352 and AUDIO are the two types Disc knows. A sector size it cannot read (2336) must not be taken for one of them.
void checkTrackTypes() {
	Workspace workspace;
	if (!check(workspace.ok(), "track types: could create a temporary directory")) return;
	workspace.writeBin("game.bin", 0, 20);
	std::string cuePath = workspace.writeCue("game.cue", joinLines({
		"FILE \"game.bin\" BINARY",
		"  TRACK 01 MODE2/2352",
		"    INDEX 01 00:00:00",
		"  TRACK 02 AUDIO",
		"    INDEX 01 00:00:05",
		"  TRACK 03 MODE2/2336",
		"    INDEX 01 00:00:10",
	}));
	Disc disc;
	if (!check(load(disc, cuePath), "track types: the CUE loads")) return;
	const Disc::Track* data = disc.getTrack(1);
	const Disc::Track* audio = disc.getTrack(2);
	const Disc::Track* unsupported = disc.getTrack(3);
	check(data != nullptr && data->type == Disc::TrackType::Mode2_2352, "track types: MODE2/2352 is a data track");
	check(audio != nullptr && audio->type == Disc::TrackType::Audio, "track types: AUDIO is an audio track");
	check(unsupported != nullptr && unsupported->type == Disc::TrackType::Unknown, "track types: MODE2/2336 is not taken for a type that is read as 2352-byte sectors");
}

// A BIN per track, the usual layout of a rip with audio: the files are laid end to end, and a track's sector number is its file's start plus its INDEX 01.
// Two tracks share the second file, and the audio file includes its 2-second pregap (INDEX 00 at 00:00:00, INDEX 01 at 00:02:00).
void checkMultiFileLayout() {
	Workspace workspace;
	if (!check(workspace.ok(), "multi-file: could create a temporary directory")) return;
	workspace.writeBin("data.bin", 0, 60);
	workspace.writeBin("audio.bin", 60, 300);
	workspace.writeBin("last.bin", 360, 100);
	std::string cuePath = workspace.writeCue("game.cue", joinLines({
		"FILE \"data.bin\" BINARY",
		"  TRACK 01 MODE2/2352",
		"    INDEX 01 00:00:00",
		"FILE \"audio.bin\" BINARY",
		"  TRACK 02 AUDIO",
		"    INDEX 00 00:00:00",
		"    INDEX 01 00:02:00",
		"  TRACK 03 AUDIO",
		"    INDEX 01 00:03:00",
		"FILE \"last.bin\" BINARY",
		"  TRACK 04 AUDIO",
		"    INDEX 01 00:01:00",
	}));
	Disc disc;
	if (!check(load(disc, cuePath), "multi-file: the CUE loads")) return;
	checkEqual(disc.getTrackCount(), 4, "multi-file: four tracks");
	checkEqual(disc.getTotalSectors(), 460, "multi-file: the total is the sum of the three files (60 + 300 + 100)");
	checkTrack(disc, {1, Disc::TrackType::Mode2_2352, 0, 0, 0, workspace.path("data.bin")}, "multi-file");
	checkTrack(disc, {2, Disc::TrackType::Audio, 150, 60, 210, workspace.path("audio.bin")}, "multi-file");
	checkTrack(disc, {3, Disc::TrackType::Audio, 225, 60, 285, workspace.path("audio.bin")}, "multi-file");
	checkTrack(disc, {4, Disc::TrackType::Audio, 75, 360, 435, workspace.path("last.bin")}, "multi-file");
}

// Loading a second cue replaces the first one completely
void checkReload() {
	Workspace workspace;
	if (!check(workspace.ok(), "reload: could create a temporary directory")) return;
	workspace.writeBin("big.bin", 0, 100);
	workspace.writeBin("small.bin", 0, 30);
	std::string threeTracks = workspace.writeCue("three.cue", joinLines({
		"FILE \"big.bin\" BINARY", "  TRACK 01 MODE2/2352", "    INDEX 01 00:00:00", "  TRACK 02 AUDIO", "    INDEX 01 00:00:20", "  TRACK 03 AUDIO", "    INDEX 01 00:00:40",
	}));
	std::string oneTrack = workspace.writeCue("one.cue", joinLines({
		"FILE \"small.bin\" BINARY", "  TRACK 01 MODE2/2352", "    INDEX 01 00:00:00",
	}));
	Disc disc;
	if (!check(load(disc, threeTracks) && load(disc, oneTrack), "reload: both CUEs load")) return;
	checkEqual(disc.getTrackCount(), 1, "reload: only the tracks of the second CUE remain");
	checkEqual(disc.getTotalSectors(), 30, "reload: the total sector count is the second disc's alone");
	check(disc.getTrack(2) == nullptr, "reload: nothing is left of the first disc's track 2");
}

// A disc shorter than sector 16 has no volume identifier to read; it must still load
void checkTinyDisc() {
	Workspace workspace;
	if (!check(workspace.ok(), "tiny disc: could create a temporary directory")) return;
	workspace.writeBin("tiny.bin", 0, 5);
	std::string cuePath = workspace.writeCue("tiny.cue", joinLines({"FILE \"tiny.bin\" BINARY", "  TRACK 01 MODE2/2352", "    INDEX 01 00:00:00"}));
	Disc disc;
	check(load(disc, cuePath), "tiny disc (5 sectors, no sector 16): the CUE loads");
	checkEqual(disc.getTotalSectors(), 5, "tiny disc: five sectors");
}

// What a failed load looks like. Each probe runs in its own process because a crash must be one failed check, not the end of the test.
void checkFailures() {
	{
		Disc disc;
		std::string printed;
		check(!load(disc, "", &printed), "failure: an empty path is not a disc");
		check(!disc.isLoaded(), "failure: an empty path leaves the drive empty");
	}
	{
		Workspace workspace;
		if (!check(workspace.ok(), "failure: could create a temporary directory")) return;
		Disc disc;
		check(!load(disc, workspace.path("does-not-exist.cue")), "failure: a CUE file that does not exist is not a disc");
		check(!disc.isLoaded(), "failure: a missing CUE file leaves the drive empty");

		// A CUE with no tracks at all
		std::string emptyCue = workspace.writeCue("empty.cue", "REM nothing here\n");
		Disc emptyDisc;
		load(emptyDisc, emptyCue);
		check(!emptyDisc.isLoaded(), "failure: a CUE without tracks does not count as a loaded disc");
		checkEqual(emptyDisc.getTrackCount(), 0, "failure: a CUE without tracks has no tracks");
		checkEqual(emptyDisc.getTotalSectors(), 0, "failure: a CUE without tracks has no sectors");

		// The CUE is fine but the BIN it names is not there (a moved or half-copied rip: the most common way an image breaks)
		std::string orphanCue = workspace.writeCue("orphan.cue", joinLines({"FILE \"gone.bin\" BINARY", "  TRACK 01 MODE2/2352", "    INDEX 01 00:00:00"}));
		isolated_probe::Outcome orphan = isolated_probe::run([orphanCue] {
			CapturedOutput output;
			Disc disc;
			uint32_t result = disc.loadCue(orphanCue) ? 1 : 0;
			result |= disc.isLoaded() ? 2 : 0;
			return result;
		});
		std::string why = orphan.finished ? "" : isolated_probe::describeFailure(orphan);
		check(orphan.finished, "failure: a CUE whose BIN file is missing does not crash the loader", why);
		check(orphan.finished && (orphan.value & 1) == 0, "failure: a CUE whose BIN file is missing is reported as a failed load", orphan.finished ? "loadCue returned true" : why);
		check(orphan.finished && (orphan.value & 2) == 0, "failure: a CUE whose BIN file is missing leaves no disc loaded", orphan.finished ? "isLoaded() is true" : why);
	}
}

} // namespace

int main() {
	checkSingleBinLayout();
	checkTimestampConversion();
	checkOtherIndexes();
	checkCueSyntax();
	checkTrackTypes();
	checkMultiFileLayout();
	checkReload();
	checkTinyDisc();
	checkFailures();
	return echo_check::finish();
}
