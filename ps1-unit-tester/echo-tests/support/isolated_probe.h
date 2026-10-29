// Runs one probe (a function returning a value) in a forked child process, so a probe that crashes the emulator core, or makes it print
// FATAL and call exit(), becomes one failed check instead of ending the whole test. POSIX only (macOS and Linux).
//
// The probe builds whatever it needs itself (typically its own Bus), so each probe starts from a clean state and the parent never
// has to construct one.
#pragma once
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

namespace isolated_probe {

struct Outcome {
	bool finished = false;    // the probe ran to its end
	uint32_t value = 0;       // what it returned (only meaningful when finished)
	int exitCode = 0;         // the process exited by itself before finishing (e.g. the Bus printing FATAL and calling exit(1))
	int signalNumber = 0;     // the process was killed (a crash, or the 10 second timeout)
	std::string firstLine;    // first line the probe printed, e.g. the FATAL message
};

template <typename Probe>
Outcome run(Probe probe) {
	struct Shared {
		uint32_t value;
		int finished;
	};
	Outcome outcome;
	void* mapped = mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	int pipeEnds[2];
	if (mapped == MAP_FAILED || pipe(pipeEnds) != 0) {
		outcome.firstLine = "could not set up the probe process";
		return outcome;
	}
	Shared* shared = static_cast<Shared*>(mapped);
	shared->value = 0;
	shared->finished = 0;

	std::fflush(nullptr); // so the child does not repeat output that is still buffered in this process
	pid_t child = fork();
	if (child == 0) {
		close(pipeEnds[0]);
		dup2(pipeEnds[1], STDOUT_FILENO);
		dup2(pipeEnds[1], STDERR_FILENO);
		alarm(10);
		shared->value = static_cast<uint32_t>(probe());
		shared->finished = 1;
		std::fflush(nullptr);
		_exit(0);
	}
	close(pipeEnds[1]);
	std::string output;
	char buffer[512];
	ssize_t count;
	while ((count = read(pipeEnds[0], buffer, sizeof(buffer))) > 0) {
		if (output.size() < 4096) {
			output.append(buffer, static_cast<size_t>(count));
		}
	}
	close(pipeEnds[0]);
	int status = 0;
	waitpid(child, &status, 0);

	outcome.finished = shared->finished == 1;
	outcome.value = shared->value;
	if (WIFEXITED(status)) {
		outcome.exitCode = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		outcome.signalNumber = WTERMSIG(status);
	}
	outcome.firstLine = output.substr(0, output.find('\n'));
	munmap(mapped, sizeof(Shared));
	return outcome;
}

// Why a probe did not finish, for the details of a failed check
inline std::string describeFailure(const Outcome& outcome) {
	std::string text = outcome.signalNumber != 0 ? "killed by signal " + std::to_string(outcome.signalNumber)
												: "exited with code " + std::to_string(outcome.exitCode);
	if (!outcome.firstLine.empty()) {
		text += ": " + outcome.firstLine;
	}
	return text;
}

} // namespace isolated_probe
