// Shared check helpers for the component tests in echo-tests/.
//
// Every check prints one line in the format the ps1-tests use ("pass - <name>" / "fail - <name> (<details>)"), so run_tests.py reads a
// component test exactly like a test program running on the emulator. A failed check does not stop the test; end main() with
// `return echo_check::finish();`, which exits with 1 when any check failed.
#pragma once
#include <cstdio>
#include <string>
#include <type_traits>

namespace echo_check {

inline int passedCount = 0;
inline int failedCount = 0;

// Decimal, plus hex for unsigned values: "64 (0x40)"
template <typename Value>
std::string formatValue(Value value) {
	static_assert(std::is_integral_v<Value>, "formatValue prints integers");
	char buffer[64];
	if constexpr (std::is_signed_v<Value>) {
		std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
	} else {
		std::snprintf(buffer, sizeof(buffer), "%llu (0x%llx)", static_cast<unsigned long long>(value), static_cast<unsigned long long>(value));
	}
	return buffer;
}

// Equality without signed/unsigned surprises (a negative value never equals an unsigned one)
template <typename Left, typename Right>
bool valuesEqual(Left left, Right right) {
	if constexpr (std::is_signed_v<Left> == std::is_signed_v<Right>) {
		return left == right;
	} else if constexpr (std::is_signed_v<Left>) {
		return left >= 0 && static_cast<std::make_unsigned_t<Left>>(left) == right;
	} else {
		return right >= 0 && left == static_cast<std::make_unsigned_t<Right>>(right);
	}
}

inline void report(bool passed, const std::string& name, const std::string& details = "") {
	if (passed) {
		passedCount++;
	} else {
		failedCount++;
	}
	if (passed || details.empty()) {
		std::printf("%s - %s\n", passed ? "pass" : "fail", name.c_str());
	} else {
		std::printf("fail - %s (%s)\n", name.c_str(), details.c_str());
	}
	std::fflush(stdout); // keep the lines even if the emulator core later hits a FATAL and exits
}

// A condition. `details` is only printed when the check fails, so put the values that explain a failure there.
inline bool check(bool condition, const std::string& name, const std::string& details = "") {
	report(condition, name, details);
	return condition;
}

template <typename Actual, typename Expected>
bool checkEqual(Actual actual, Expected expected, const std::string& name) {
	bool equal = valuesEqual(actual, expected);
	report(equal, name, equal ? "" : "got " + formatValue(actual) + ", expected " + formatValue(expected));
	return equal;
}

// low <= actual <= high (values up to 63 bits)
template <typename Actual, typename Low, typename High>
bool checkRange(Actual actual, Low low, High high, const std::string& name) {
	long long value = static_cast<long long>(actual);
	bool inside = value >= static_cast<long long>(low) && value <= static_cast<long long>(high);
	report(inside, name, inside ? "" : "got " + std::to_string(value) + ", expected " + std::to_string(static_cast<long long>(low)) + " to " + std::to_string(static_cast<long long>(high)));
	return inside;
}

// Free-form context line (not a check); run_tests.py keeps it in tty.txt but does not list it
inline void note(const std::string& text) {
	std::printf("  %s\n", text.c_str());
	std::fflush(stdout);
}

inline int finish() {
	std::printf("%d checks passed, %d failed\n", passedCount, failedCount);
	std::fflush(stdout);
	return failedCount == 0 ? 0 : 1;
}

} // namespace echo_check
