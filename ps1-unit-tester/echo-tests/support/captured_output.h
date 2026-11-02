// Captures what the emulator core writes to std::cout and std::cerr while it is alive. Disc prints a line for every disc it loads and for
// every sector it cannot read; capturing keeps tty.txt readable and lets a test check what was printed. Checks themselves go through
// printf (support/echo_check.h) and are not captured.
#pragma once
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>

class CapturedOutput {
public:
	CapturedOutput() : previousOut(std::cout.rdbuf(sink.rdbuf())), previousErr(std::cerr.rdbuf(sink.rdbuf())) {}

	CapturedOutput(const CapturedOutput&) = delete;
	CapturedOutput& operator=(const CapturedOutput&) = delete;

	~CapturedOutput() {
		std::cout.rdbuf(previousOut);
		std::cerr.rdbuf(previousErr);
	}

	// Everything captured so far
	std::string text() const { return sink.str(); }

	// The captured text on one line, for the details of a failed check
	std::string oneLine() const {
		std::string result = sink.str();
		for (char& character : result) {
			if (character == '\n' || character == '\r') character = '|';
		}
		return result;
	}

private:
	std::ostringstream sink;
	std::streambuf* previousOut;
	std::streambuf* previousErr;
};
