#!/usr/bin/env python3
"""Runs the PS1 test suite against your emulator core and tells you which tests failed.

	python3 ps1-unit-tester/run_tests.py            # build the runner, run everything
	python3 ps1-unit-tester/run_tests.py gte        # only tests with "gte" in their path
	python3 ps1-unit-tester/run_tests.py 3          # only tier 3
	python3 ps1-unit-tester/run_tests.py -v         # also list passing, known-failing and informational tests
	python3 ps1-unit-tester/run_tests.py exotic     # only the tier named exotic (tier names and path fragments both work)

Once baseline.txt exists, only changes against it are reported (see "Baseline" below); --strict ignores it.

How it works: the CMake target `echo-headless` links the emulator core (Bus, CPU, GPU...) with no window and runs ONE test
program. This script starts it once per test (in parallel, each in its own process so a FATAL or a hang only affects that
test), then judges the result against what the test prints and the real-hardware reference files next to it:
	sum    pass/fail counts printed by the test itself
	log    the test's printed output must match the real-hardware psx.log
	vram   VRAM at the end must match the reference vram.png
Tests marked `timing` or `info` in tiers.txt are run but not graded, because their numbers are hardware measurements.
Everything for every test lands in ps1-unit-tester/results/<test>/ (tty.txt, vram.png, stdout.txt, log.diff, vram-diff.png).

Baseline: baseline.txt records the verdict every test had the last time you accepted the results, and for failing tests how
wrong it was (pixels, differing lines, failed checks). A run then reports only what changed: REGRESSION (a pass that now fails, or
a failure that got worse), IMPROVED, and for informational tests whether their printed output changed (saved in baseline/).
The exit code is 1 only for regressions. `--update-baseline` accepts the current results (it will not record a regression unless
you add --force); `--strict` ignores the baseline and treats every failure as a failure, like before the baseline existed.
"""
import argparse
import concurrent.futures
import dataclasses
import difflib
import itertools
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import threading
import time
import zlib

SUITE_DIRECTORY = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIRECTORY = os.path.dirname(SUITE_DIRECTORY)

FAILING_VERDICTS = {"FAIL", "LOG-DIFF", "VRAM-DIFF", "CRASH", "TIMEOUT", "NO-RESULT", "NO-SIDELOAD", "HOST-FAIL"}
UNGRADED_VERDICTS = {"TIMING", "INFO"}
PASSING_VERDICTS = {"PASS"} | UNGRADED_VERDICTS   # nothing is wrong with these

# How badly a failing verdict is broken, for the baseline comparison: 1 = ran and produced wrong output,
# 2 = ran but produced no usable result, 3 = died. A move to a higher number is a regression even if the verdict names differ.
VERDICT_SEVERITY = {"FAIL": 1, "LOG-DIFF": 1, "VRAM-DIFF": 1, "NO-RESULT": 2, "NO-SIDELOAD": 2, "CRASH": 3, "TIMEOUT": 3, "HOST-FAIL": 3}

# The verdicts that come with a number saying how wrong the result was, and what that number counts
MEASURE_UNITS = {"FAIL": "failed", "LOG-DIFF": "lines", "VRAM-DIFF": "px"}

MAXIMUM_SAVED_OUTPUT_LINES = 2000


@dataclasses.dataclass
class TestEntry:
	tier: str
	checks: set
	path: str
	frames: int
	runner_arguments: list = dataclasses.field(default_factory=list)  # extra echo-headless arguments, e.g. --press start@150
	vram_region: tuple = None  # (left, top, width, height): only this part of VRAM is compared
	vram_ignored_regions: list = dataclasses.field(default_factory=list)  # [(left, top, width, height)] boxes skipped in the comparison
	ignored_log_lines: list = dataclasses.field(default_factory=list)  # compiled regexes: matching lines are dropped from both logs before comparing


@dataclasses.dataclass
class TestResult:
	name: str
	tier: str
	verdict: str = "PASS"
	summary: str = ""
	details: list = dataclasses.field(default_factory=list)
	seconds: float = 0.0
	measure: int = 0                                                   # how wrong a failing result is (see MEASURE_UNITS); 0 when not applicable
	output_lines: list = dataclasses.field(default_factory=list)       # normalized printed output, kept for informational tests only

	@property
	def failed(self):
		return self.verdict in FAILING_VERDICTS


# ---------------------------------------------------------------------------------------------
# Small text helpers
# ---------------------------------------------------------------------------------------------

ANSI_PATTERN = re.compile(r"\x1b\[[0-9;]*m")


def strip_ansi(text):
	return ANSI_PATTERN.sub("", text)


def read_text(path):
	try:
		with open(path, "r", encoding="utf-8", errors="replace") as file:
			return file.read()
	except OSError:
		return ""


# Lines the Sony libraries print on their own at start-up. The real-hardware psx.log files don't contain them.
IGNORED_LOG_LINE = re.compile(r"^ResetGraph:")


def normalize_log(text):
	"""No CRs, no ANSI colours, no leading "% ", no trailing spaces, no blank lines, no library start-up chatter."""
	lines = []
	for line in text.replace("\r", "").split("\n"):
		line = strip_ansi(line)
		if line.startswith("% "):
			line = line[2:]
		line = line.rstrip()
		if line and not IGNORED_LOG_LINE.match(line):
			lines.append(line)
	return lines


# ---------------------------------------------------------------------------------------------
# PNG reading / writing (standard library only)
# ---------------------------------------------------------------------------------------------

def paeth_predictor(left, above, upper_left):
	estimate = left + above - upper_left
	distance_left = abs(estimate - left)
	distance_above = abs(estimate - above)
	distance_upper_left = abs(estimate - upper_left)
	if distance_left <= distance_above and distance_left <= distance_upper_left:
		return left
	return above if distance_above <= distance_upper_left else upper_left


def unfilter_row(filter_type, line, previous, bytes_per_pixel):
	if filter_type == 0:
		return line
	output = bytearray(len(line))
	if filter_type == 1:  # Sub: running sum per channel
		for offset in range(bytes_per_pixel):
			output[offset::bytes_per_pixel] = bytes(itertools.accumulate(line[offset::bytes_per_pixel], lambda total, value: (total + value) & 255))
	elif filter_type == 2:  # Up
		output = bytearray((value + above) & 255 for value, above in zip(line, previous))
	elif filter_type == 3:  # Average
		for index in range(len(line)):
			left = output[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
			output[index] = (line[index] + ((left + previous[index]) >> 1)) & 255
	elif filter_type == 4:  # Paeth
		for index in range(len(line)):
			left = output[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
			upper_left = previous[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
			output[index] = (line[index] + paeth_predictor(left, previous[index], upper_left)) & 255
	else:
		raise ValueError("unknown PNG filter type %d" % filter_type)
	return output


def read_png(path):
	"""Returns (width, height, rows) with every row converted to RGB bytes.
	Supports grayscale, RGB, palette and RGBA/gray+alpha images (8-bit, plus 1/2/4-bit grayscale and palette); not interlaced."""
	with open(path, "rb") as file:
		data = file.read()
	if data[:8] != b"\x89PNG\r\n\x1a\n":
		raise ValueError("not a PNG file")
	position, compressed, palette = 8, b"", b""
	width = height = bit_depth = color_type = 0
	while position < len(data):
		length, kind = struct.unpack(">I4s", data[position:position + 8])
		body = data[position + 8:position + 8 + length]
		if kind == b"IHDR":
			width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", body[:13])
			if interlace != 0:
				raise ValueError("interlaced PNGs are not supported")
			if color_type not in (0, 2, 3, 4, 6) or bit_depth not in (1, 2, 4, 8) or (color_type in (2, 4, 6) and bit_depth != 8):
				raise ValueError("unsupported PNG format (color type %d, %d-bit)" % (color_type, bit_depth))
		elif kind == b"PLTE":
			palette = body
		elif kind == b"IDAT":
			compressed += body
		position += 12 + length

	channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color_type]
	bits_per_pixel = channels * bit_depth
	bytes_per_pixel = max(1, bits_per_pixel // 8)
	stride = (width * bits_per_pixel + 7) // 8
	raw = zlib.decompress(compressed)

	if color_type == 3:
		if not palette:
			raise ValueError("palette PNG without a palette")
		palette_tables = [bytes(palette[entry * 3 + channel] if entry * 3 + channel < len(palette) else 0 for entry in range(256)) for channel in range(3)]
	pixel_masks = (1 << bit_depth) - 1

	rows, previous = [], bytearray(stride)
	for row_index in range(height):
		base = row_index * (stride + 1)
		line = unfilter_row(raw[base], bytearray(raw[base + 1:base + 1 + stride]), previous, bytes_per_pixel)
		previous = line
		rgb = bytearray(width * 3)
		if color_type in (0, 3):
			if bit_depth == 8:
				indexes = bytes(line)
			else:  # unpack 1/2/4-bit samples
				per_byte = 8 // bit_depth
				unpacked = bytearray(stride * per_byte)
				for slot in range(per_byte):
					shift = 8 - bit_depth * (slot + 1)
					unpacked[slot::per_byte] = bytes(line).translate(bytes((value >> shift) & pixel_masks for value in range(256)))
				indexes = bytes(unpacked[:width])
			if color_type == 3:
				rgb[0::3], rgb[1::3], rgb[2::3] = (indexes.translate(table) for table in palette_tables)
			else:
				scale = bytes(min(255, value * 255 // pixel_masks) for value in range(256)) if bit_depth < 8 else bytes(range(256))
				gray = indexes.translate(scale)
				rgb[0::3], rgb[1::3], rgb[2::3] = gray, gray, gray
		elif color_type == 2:
			rgb = bytearray(line)
		elif color_type == 6:
			rgb[0::3], rgb[1::3], rgb[2::3] = line[0::4], line[1::4], line[2::4]
		else:  # gray + alpha
			gray = line[0::2]
			rgb[0::3], rgb[1::3], rgb[2::3] = gray, gray, gray
		rows.append(bytes(rgb))
	return width, height, rows


def write_png(path, width, height, rows):
	def chunk(kind, body):
		payload = kind + body
		return struct.pack(">I", len(body)) + payload + struct.pack(">I", zlib.crc32(payload) & 0xFFFFFFFF)

	raw = b"".join(b"\x00" + bytes(row) for row in rows)
	header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
	with open(path, "wb") as file:
		file.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


DIM_TABLE = bytes(value >> 2 for value in range(256))


def compare_vram(reference_path, actual_path, diff_path, region=None, ignored_regions=()):
	"""Returns None when identical, otherwise (number of differing pixels, one-line description). Writes a diff image (red = different)."""
	width, height, reference_rows = read_png(reference_path)
	actual_width, actual_height, actual_rows = read_png(actual_path)
	if (width, height) != (actual_width, actual_height):
		return width * height, "size mismatch: reference is %dx%d, ours is %dx%d" % (width, height, actual_width, actual_height)

	left, top, region_width, region_height = region if region else (0, 0, width, height)
	byte_start, byte_end = left * 3, (left + region_width) * 3

	count, min_x, min_y, max_x, max_y, max_delta = 0, width, height, -1, -1, 0
	diff_rows = []
	for row_index in range(height):
		reference_row, actual_row = reference_rows[row_index], actual_rows[row_index]
		for ignored_left, ignored_top, ignored_width, ignored_height in ignored_regions:
			if ignored_top <= row_index < ignored_top + ignored_height:
				actual_row = bytearray(actual_row)
				actual_row[ignored_left * 3:(ignored_left + ignored_width) * 3] = reference_row[ignored_left * 3:(ignored_left + ignored_width) * 3]
		if not (top <= row_index < top + region_height) or reference_row[byte_start:byte_end] == actual_row[byte_start:byte_end]:
			diff_rows.append(reference_row.translate(DIM_TABLE))
			continue
		diff_row = bytearray(reference_row.translate(DIM_TABLE))
		for column in range(left, left + region_width):
			offset = column * 3
			expected, actual = reference_row[offset:offset + 3], actual_row[offset:offset + 3]
			if expected != actual:
				count += 1
				min_x, max_x, min_y, max_y = min(min_x, column), max(max_x, column), min(min_y, row_index), max(max_y, row_index)
				max_delta = max(max_delta, max(abs(a - b) for a, b in zip(expected, actual)))
				diff_row[offset:offset + 3] = b"\xff\x00\x00"
		diff_rows.append(bytes(diff_row))

	if count == 0:
		return None
	write_png(diff_path, width, height, diff_rows)
	return count, "%d pixels differ, bounding box x %d-%d y %d-%d, largest channel difference %d" % (count, min_x, max_x, min_y, max_y, max_delta)


# ---------------------------------------------------------------------------------------------
# Judging one test
# ---------------------------------------------------------------------------------------------

def failing_test_lines(tty_text):
	"""Picks the lines that say which individual checks failed: "fail - ..." lines, and libcester's "X name" / "...Error file:line" lines."""
	lines = []
	for raw_line in tty_text.replace("\r", "").split("\n"):
		line = strip_ansi(raw_line)
		if line.startswith("% "):
			line = line[2:]
		if line.startswith("fail - ") or re.match(r"^\s*X\s+\S", line) or re.match(r"^\w*Error\b", line):
			lines.append(line.strip())
	return lines


def describe_log_difference(expected, actual, diff_path):
	"""Writes the full diff to diff_path and returns a short list describing the first differences."""
	if len(expected) + len(actual) > 6000:
		# Very long logs: a position-by-position comparison is fast and good enough to point at the first problems
		opcodes = []
		for index in range(max(len(expected), len(actual))):
			left = expected[index] if index < len(expected) else None
			right = actual[index] if index < len(actual) else None
			if left != right:
				opcodes.append(("replace" if left is not None and right is not None else ("delete" if right is None else "insert"), index, index + 1, index, index + 1))
	else:
		opcodes = [opcode for opcode in difflib.SequenceMatcher(None, expected, actual, autojunk=False).get_opcodes() if opcode[0] != "equal"]

	with open(diff_path, "w", encoding="utf-8") as file:
		file.write("".join(line + "\n" for line in difflib.unified_diff(expected, actual, "psx.log (real hardware)", "ours", lineterm="", n=2)) if len(expected) + len(actual) <= 6000 else "(log too long for a full diff; see tty.txt and psx.log)\n")

	details = []
	for tag, expected_start, expected_end, actual_start, actual_end in opcodes:
		if tag == "replace":
			for offset in range(max(expected_end - expected_start, actual_end - actual_start)):
				if expected_start + offset < expected_end:
					details.append("expected: " + expected[expected_start + offset])
				if actual_start + offset < actual_end:
					details.append("     got: " + actual[actual_start + offset])
		elif tag == "delete":  # present in the real log, missing from ours
			details.extend("missing:  " + line for line in expected[expected_start:expected_end])
		elif tag == "insert":  # printed by us, not in the real log
			details.extend("extra:    " + line for line in actual[actual_start:actual_end])
	return details


def parse_headless_line(stdout_text):
	matches = re.findall(r"^\[HEADLESS\] (.*)$", stdout_text, re.MULTILINE)
	if not matches:
		return None
	return dict(pair.split("=", 1) for pair in matches[-1].split() if "=" in pair)


def run_test(entry, options):
	name = entry.path
	result = TestResult(name=name, tier=entry.tier)
	started = time.time()

	program_path = os.path.join(SUITE_DIRECTORY, "public-tests", entry.path)
	if not os.path.isfile(program_path):
		result.verdict, result.summary = "SKIPPED", "file not found: public-tests/" + entry.path
		return result

	output_directory = os.path.join(options.results, entry.path.replace("/", "_"))
	os.makedirs(output_directory, exist_ok=True)
	tty_path = os.path.join(output_directory, "tty.txt")
	vram_path = os.path.join(output_directory, "vram.png")
	stdout_path = os.path.join(output_directory, "stdout.txt")
	reference_directory = os.path.dirname(program_path)

	frames = options.frames if options.frames else entry.frames
	command = [options.runner, "--bios", options.bios, "--exe", program_path, "--frames", str(frames),
	           "--quiet-exit", str(options.quiet_frames), "--tty-log", tty_path, "--vram-png", vram_path] + entry.runner_arguments
	environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")

	timed_out, return_code = False, 0
	with open(stdout_path, "w") as stdout_file:
		try:
			return_code = subprocess.run(command, cwd=PROJECT_DIRECTORY, env=environment, stdout=stdout_file, stderr=subprocess.STDOUT, timeout=options.timeout).returncode
		except subprocess.TimeoutExpired:
			timed_out = True
	result.seconds = time.time() - started

	stdout_text = read_text(stdout_path)
	tty_text = read_text(tty_path)
	headless = parse_headless_line(stdout_text)

	fatal_lines = [line for line in stdout_text.split("\n") if re.search(r"fatal|unimplemented|unhandled|not implemented|unknown hardware opcode|garbage memory", line, re.IGNORECASE)]
	fatal_text = fatal_lines[-1].strip() if fatal_lines else ""
	last_lines = [line.strip() for line in stdout_text.split("\n") if line.strip()]
	last_line = last_lines[-1] if last_lines else ""

	if timed_out:
		result.verdict, result.summary = "TIMEOUT", "no result after %d seconds" % options.timeout
		return result
	if return_code != 0:
		if fatal_text:
			reason = fatal_text
		elif return_code < 0:
			reason = "killed by signal %d (a real crash, with no FATAL message). Last output: %s" % (-return_code, last_line)
		else:
			reason = "exited with code %d but printed no FATAL message (an exit(1) that needs a message). Last output: %s" % (return_code, last_line)
		result.verdict, result.summary = "CRASH", reason
		if tty_text.strip():
			result.details.append("(its output so far is in %s)" % os.path.relpath(tty_path, PROJECT_DIRECTORY))
		return result
	if headless is None:
		result.verdict, result.summary = "NO-RESULT", "runner printed no result line " + (fatal_text or last_line)
		return result
	if headless.get("sideloaded") != "1":
		result.verdict, result.summary = "NO-SIDELOAD", "BIOS never reached the shell within %d frames" % frames
		return result

	passed, failed = int(headless.get("pass", 0)), int(headless.get("fail", 0))
	problems = []   # (verdict, summary, details, measure)
	notes = []

	if "sum" in entry.checks:
		if failed > 0:
			problems.append(("FAIL", "%d passed, %d failed" % (passed, failed), failing_test_lines(tty_text), failed))
		elif passed == 0:
			problems.append(("NO-RESULT", "test printed no pass/fail lines (raise its frame limit?)", [], 0))
		else:
			notes.append("%d passed" % passed)

	reference_log = os.path.join(reference_directory, "psx.log")
	if "log" in entry.checks and os.path.isfile(reference_log):
		expected, actual = normalize_log(read_text(reference_log)), normalize_log(tty_text)
		if entry.ignored_log_lines:
			expected = [line for line in expected if not any(pattern.search(line) for pattern in entry.ignored_log_lines)]
			actual = [line for line in actual if not any(pattern.search(line) for pattern in entry.ignored_log_lines)]
		# The reference log can predate tests added to the program later. A "pass - " line that the log has never heard of is not a difference.
		knownLines = set(expected)
		actual = [line for line in actual if not (line.startswith("pass - ") and line not in knownLines)]
		if expected != actual:
			differences = describe_log_difference(expected, actual, os.path.join(output_directory, "log.diff"))
			problems.append(("LOG-DIFF", "output differs from the real-hardware psx.log (%d differing lines)" % len(differences), differences, len(differences)))

	if "vram" in entry.checks:
		reference_png = os.path.join(reference_directory, "vram.png")
		if not os.path.isfile(reference_png):
			candidates = sorted(name for name in os.listdir(reference_directory) if name.startswith("vram") and name.endswith(".png"))
			reference_png = os.path.join(reference_directory, candidates[0]) if candidates else ""
		if reference_png and os.path.isfile(vram_path):
			diff_image_path = os.path.join(output_directory, "vram-diff.png")
			try:
				difference = compare_vram(reference_png, vram_path, diff_image_path, entry.vram_region, entry.vram_ignored_regions)
			except (ValueError, zlib.error, struct.error) as error:
				difference = (0, "could not compare images: %s" % error)
			if difference:
				different_pixel_count, difference_text = difference
				extra = ["diff image: " + os.path.relpath(diff_image_path, PROJECT_DIRECTORY)] if os.path.isfile(diff_image_path) else []
				problems.append(("VRAM-DIFF", difference_text, extra, different_pixel_count))
		elif reference_png:
			problems.append(("NO-RESULT", "runner produced no vram.png", [], 0))

	if problems:
		result.verdict = problems[0][0]
		result.summary = "; ".join(problem[1] for problem in problems)
		result.measure = sum(problem[3] for problem in problems)
		for problem in problems:
			result.details.extend(problem[2])
	elif "timing" in entry.checks:
		result.verdict, result.summary = "TIMING", "not graded: timing numbers are hardware measurements"
	elif "info" in entry.checks:
		result.verdict, result.summary = "INFO", "not graded, look at %s (tty.txt, vram.png)" % os.path.relpath(output_directory, PROJECT_DIRECTORY)
	else:
		result.summary = ", ".join(notes)
	if result.verdict in UNGRADED_VERDICTS:
		result.output_lines = normalize_log(tty_text)[:MAXIMUM_SAVED_OUTPUT_LINES]
	return result


def discover_host_tests():
	"""The component tests: every echo-tests/*.cpp (the support/ folder holds shared headers, not tests)."""
	directory = os.path.join(SUITE_DIRECTORY, "echo-tests")
	if not os.path.isdir(directory):
		return []
	return sorted(os.path.splitext(name)[0] for name in os.listdir(directory) if name.endswith(".cpp"))


def host_test_binary(options, source_name):
	"""Where CMake puts the executable for echo-tests/<source_name>.cpp (see the echo-tests target in CMakeLists.txt)."""
	return os.path.join(options.build_directory, "echo-tests", "echo-test-" + source_name)


def run_host_test(source_name, options):
	"""Runs a component test from echo-tests/ (built by CMake, no BIOS needed) and judges it like a test program: by its pass/fail lines."""
	name = "echo-tests/" + source_name
	result = TestResult(name=name, tier="host")
	started = time.time()

	build_errors = options.host_build_errors.get(source_name)
	if build_errors is not None:
		result.verdict, result.summary = "HOST-FAIL", "does not compile"
		result.details = build_errors
		return result
	binary = host_test_binary(options, source_name)
	if not os.path.isfile(binary):
		result.verdict, result.summary = "HOST-FAIL", "not built: %s (run without --no-build, or build the echo-tests target)" % os.path.relpath(binary, PROJECT_DIRECTORY)
		return result

	output_directory = os.path.join(options.results, name.replace("/", "_"))
	os.makedirs(output_directory, exist_ok=True)
	environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
	try:
		completed = subprocess.run([binary], cwd=PROJECT_DIRECTORY, env=environment, capture_output=True, text=True, errors="replace", timeout=options.timeout)
	except subprocess.TimeoutExpired:
		result.verdict, result.summary = "HOST-FAIL", "timed out after %d seconds" % options.timeout
		return result
	result.seconds = time.time() - started

	text = completed.stdout + completed.stderr
	with open(os.path.join(output_directory, "tty.txt"), "w", encoding="utf-8") as file:
		file.write(text)
	lines = [strip_ansi(line) for line in text.replace("\r", "").split("\n")]
	passed = sum(1 for line in lines if line.startswith("pass - "))
	failed = sum(1 for line in lines if line.startswith("fail - "))
	last_lines = [line for line in lines if line.strip()][-10:]

	if failed > 0:
		result.verdict, result.summary, result.measure = "FAIL", "%d passed, %d failed" % (passed, failed), failed
		result.details = failing_test_lines(text)
	elif completed.returncode != 0:
		reason = "killed by signal %d" % -completed.returncode if completed.returncode < 0 else "exit code %d" % completed.returncode
		result.verdict, result.summary = "HOST-FAIL", "%s, but no failed check (a crash, or an exit before the checks ran)" % reason
		result.details = last_lines
	elif passed == 0:
		result.verdict, result.summary = "NO-RESULT", "test printed no pass/fail lines"
		result.details = last_lines
	else:
		result.summary = "%d checks passed" % passed
	return result


# ---------------------------------------------------------------------------------------------
# Baseline: what every test did the last time the results were accepted
# ---------------------------------------------------------------------------------------------

def baseline_output_directory(options):
	"""Saved output of informational tests lives next to baseline.txt, in a folder with the same name (baseline/)."""
	return os.path.splitext(options.baseline)[0]


def saved_output_path(options, test_name):
	return os.path.join(baseline_output_directory(options), test_name.replace("/", "_") + ".txt")


def read_baseline(path):
	"""Returns {test name: (verdict, measure)} from baseline.txt."""
	entries = {}
	for line in read_text(path).split("\n"):
		line = line.strip()
		if not line or line.startswith("#"):
			continue
		fields = [field.strip() for field in line.split("|")]
		if len(fields) < 2:
			continue
		measure = int(fields[2]) if len(fields) > 2 and fields[2].isdigit() else 0
		entries[fields[0]] = (fields[1], measure)
	return entries


def write_baseline(path, entries):
	with open(path, "w", encoding="utf-8") as file:
		file.write("# Expected result of every test: path | verdict | how wrong (pixels / differing lines / failed checks; 0 when not applicable)\n")
		file.write("# Written by `run_tests.py --update-baseline`. A run reports only what is worse or better than this.\n")
		file.write("# INFO and TIMING tests have their printed output saved in the baseline/ folder next to this file.\n")
		for name in sorted(entries):
			verdict, measure = entries[name]
			file.write("%s | %s | %d\n" % (name, verdict, measure))


def describe_verdict(verdict, measure):
	if verdict in MEASURE_UNITS:
		return "%s (%d %s)" % (verdict, measure, MEASURE_UNITS[verdict])
	return verdict


def read_saved_output(path):
	text = read_text(path)
	lines = text.split("\n")
	if lines and lines[-1] == "":
		lines.pop()
	return lines


def compare_output_with_baseline(result, options):
	"""For an informational test: returns a short list of differing lines (empty when the same or when nothing was saved)."""
	saved_path = saved_output_path(options, result.name)
	if not os.path.isfile(saved_path):
		return []
	saved_lines = read_saved_output(saved_path)
	if saved_lines == result.output_lines:
		return []
	diff_lines = list(difflib.unified_diff(saved_lines, result.output_lines, "baseline", "now", lineterm="", n=1))
	diff_directory = os.path.join(options.results, result.name.replace("/", "_"))
	os.makedirs(diff_directory, exist_ok=True)
	with open(os.path.join(diff_directory, "baseline.diff"), "w", encoding="utf-8") as file:
		file.write("".join(line + "\n" for line in diff_lines))
	return diff_lines[2:]


def compare_with_baseline(result, baseline, options):
	"""Returns (category, text, details).
	categories: REGRESSION, NEW-FAILING (both fail the run), IMPROVED, CHANGED, OUTPUT-CHANGED, KNOWN, OK, NEW, SKIPPED"""
	if result.verdict == "SKIPPED":
		return "SKIPPED", result.summary, []
	now = describe_verdict(result.verdict, result.measure)
	entry = baseline.get(result.name)
	if entry is None:
		return ("NEW-FAILING" if result.failed else "NEW"), "not in the baseline yet: " + now, []
	baseline_verdict, baseline_measure = entry
	was = describe_verdict(baseline_verdict, baseline_measure)
	was_fine = baseline_verdict in PASSING_VERDICTS
	is_fine = not result.failed

	if was_fine and is_fine:
		if result.verdict in UNGRADED_VERDICTS:
			differences = compare_output_with_baseline(result, options)
			if differences:
				return "OUTPUT-CHANGED", "printed output differs from the baseline (full diff: baseline.diff)", differences
		return "OK", now, []
	if was_fine:
		return "REGRESSION", "was %s, now %s" % (was, now), []
	if is_fine:
		return "IMPROVED", "was %s, now %s" % (was, now), []

	baseline_severity = VERDICT_SEVERITY.get(baseline_verdict, 1)
	severity = VERDICT_SEVERITY.get(result.verdict, 1)
	text = "was %s, now %s" % (was, now)
	if severity != baseline_severity:
		return ("REGRESSION" if severity > baseline_severity else "IMPROVED"), text, []
	if result.verdict != baseline_verdict:
		return "CHANGED", text, []
	if result.measure > baseline_measure:
		return "REGRESSION", text, []
	if result.measure < baseline_measure:
		return "IMPROVED", text, []
	return "KNOWN", now, []


def update_baseline(results, options, ran_everything):
	"""Records the current results. A regression is not recorded (it stays as it was) unless --force is given."""
	original = read_baseline(options.baseline)
	entries = dict(original)
	counts = {"NEW": 0, "NEW-FAILING": 0, "IMPROVED": 0, "OUTPUT-CHANGED": 0, "CHANGED": 0, "REGRESSION": 0}
	blocked = []
	for result in results:
		category, text, _ = compare_with_baseline(result, original, options)
		if category == "SKIPPED":
			continue
		if category == "REGRESSION" and not options.force:
			blocked.append((result, text))
			continue
		counts[category] = counts.get(category, 0) + 1
		entries[result.name] = (result.verdict, result.measure)
		saved_path = saved_output_path(options, result.name)
		if result.verdict in UNGRADED_VERDICTS:
			os.makedirs(os.path.dirname(saved_path), exist_ok=True)
			with open(saved_path, "w", encoding="utf-8") as file:
				file.write("".join(line + "\n" for line in result.output_lines))
		elif os.path.isfile(saved_path):
			os.remove(saved_path)
	if ran_everything:   # a complete run also drops entries (and saved output) of tests that are no longer in the suite
		current_names = {result.name for result in results}
		for stale_name in [name for name in entries if name not in current_names]:
			del entries[stale_name]
			stale_path = saved_output_path(options, stale_name)
			if os.path.isfile(stale_path):
				os.remove(stale_path)
	write_baseline(options.baseline, entries)

	print("Baseline written: %d tests in %s" % (len(entries), os.path.relpath(options.baseline, PROJECT_DIRECTORY)))
	changes = ["%d %s" % (count, name.lower().replace("-", " ")) for name, count in counts.items() if count and name not in ("REGRESSION", "OK", "KNOWN")]
	if options.force and counts["REGRESSION"]:
		changes.append("%d regressions recorded (--force)" % counts["REGRESSION"])
	if changes:
		print("  recorded: " + ", ".join(changes))
	if blocked:
		print("\n%d regressions were NOT recorded (fix them, or add --force to accept them):" % len(blocked))
		for result, text in blocked:
			print("  %s: %s" % (result.name, text))
	return 1 if blocked else 0


def report_against_baseline(results, options, elapsed):
	"""Prints only what changed against baseline.txt. Returns the exit code (1 when something regressed)."""
	baseline = read_baseline(options.baseline)
	compared = [(result,) + compare_with_baseline(result, baseline, options) for result in results]
	by_category = {}
	for result, category, text, details in compared:
		by_category.setdefault(category, []).append((result, text, details))

	maximum_detail_lines = 12
	for category in ("REGRESSION", "NEW-FAILING"):
		for result, text, details in by_category.get(category, []):
			print("%-11s tier %-6s %s" % (category, result.tier, result.name))
			print("\t" + text)
			if result.summary and result.summary not in text:
				print("\t" + result.summary)
			for line in result.details[:maximum_detail_lines]:
				print("\t\t" + line)
			if len(result.details) > maximum_detail_lines:
				print("\t\t... %d more (see %s)" % (len(result.details) - maximum_detail_lines, os.path.relpath(os.path.join(options.results, result.name.replace("/", "_")), PROJECT_DIRECTORY)))
			print()
	for category in ("CHANGED", "OUTPUT-CHANGED", "IMPROVED", "NEW"):
		for result, text, details in by_category.get(category, []):
			print("%-14s tier %-6s %s\n\t%s" % (category, result.tier, result.name, text))
			for line in details[:6]:
				print("\t\t" + line)
			if len(details) > 6:
				print("\t\t... %d more diff lines (see %s)" % (len(details) - 6, os.path.relpath(os.path.join(options.results, result.name.replace("/", "_"), "baseline.diff"), PROJECT_DIRECTORY)))
	if options.verbose:
		for category in ("KNOWN", "OK", "SKIPPED"):
			for result, text, _ in by_category.get(category, []):
				print("%-14s tier %-6s %-62s %s" % (category, result.tier, result.name, text))
		print()

	counts = {category: len(items) for category, items in by_category.items()}
	regressions = counts.get("REGRESSION", 0) + counts.get("NEW-FAILING", 0)
	passed = sum(1 for result in results if result.verdict == "PASS")
	ungraded = sum(1 for result in results if result.verdict in UNGRADED_VERDICTS)
	parts = ["%d passed" % passed, "%d known failures" % counts.get("KNOWN", 0)]
	if ungraded:
		parts.append("%d informational" % ungraded)
	for category, label in (("IMPROVED", "improved"), ("CHANGED", "changed"), ("OUTPUT-CHANGED", "output changed"), ("NEW", "new")):
		if counts.get(category):
			parts.append("%d %s" % (counts[category], label))
	if counts.get("SKIPPED"):
		parts.append("%d skipped" % counts["SKIPPED"])
	if regressions:
		print("%d REGRESSIONS in %.0fs: %s." % (regressions, elapsed, ", ".join(parts)))
	else:
		print("NO REGRESSIONS in %.0fs: %s." % (elapsed, ", ".join(parts)))
	if counts.get("IMPROVED") or counts.get("NEW") or counts.get("CHANGED") or counts.get("OUTPUT-CHANGED"):
		print("Run with --update-baseline to accept the changes above.")
	print("Details: %s" % os.path.relpath(options.results, PROJECT_DIRECTORY))
	return 1 if regressions else 0


# ---------------------------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------------------------
def split_ignore_line_arguments(arguments):
	"""Separates the harness-only `--ignore-line <regex>` options from the arguments meant for echo-headless."""
	runner_arguments, ignored_log_lines, index = [], [], 0
	while index < len(arguments):
		if arguments[index] == "--ignore-line" and index + 1 < len(arguments):
			ignored_log_lines.append(re.compile(arguments[index + 1]))
			index += 2
		else:
			runner_arguments.append(arguments[index])
			index += 1
	return runner_arguments, ignored_log_lines


def read_manifest(path):
	entries = []
	for line in read_text(path).split("\n"):
		line = line.strip()
		if not line or line.startswith("#"):
			continue
		fields = line.split("|")
		tier, checks, test_path, frames = fields[:4]
		runner_arguments, ignored_log_lines = split_ignore_line_arguments(shlex.split(fields[4]) if len(fields) > 4 else [])
		check_names = set()
		vram_region = None
		vram_ignored_regions = []
		for check in checks.strip().split(","):
			if check.startswith("vram@"):
				vram_region = tuple(int(number) for number in check[len("vram@"):].split(":"))
				check = "vram"
			elif check.startswith("vram-ignore@"):
				vram_ignored_regions.append(tuple(int(number) for number in check[len("vram-ignore@"):].split(":")))
				check = "vram"
			check_names.add(check)
		entries.append(TestEntry(tier.strip(), check_names, test_path.strip(), int(frames), runner_arguments, vram_region, vram_ignored_regions, ignored_log_lines))
	return entries


def matches_filters(entry_tier, entry_name, filters):
	"""A filter matches a test whose tier is exactly that (1, 2 ... or a name like exotic or host), or, unless it is a number, whose path contains it."""
	if not filters:
		return True
	for item in filters:
		if item.lower() == entry_tier.lower() or (not item.isdigit() and item in entry_name):
			return True
	return False


def build_runner(options, host_test_names):
	"""Rebuilds echo-headless and the component tests so the tests always run your latest code.
	Returns False (after printing why) when echo-headless does not build. A component test that does not compile is not fatal:
	it is remembered in options.host_build_errors and reported as that test's HOST-FAIL, so the other tests still run."""
	if shutil.which("cmake") is None:
		print("cmake not found; skipping the rebuild (use --no-build to silence this)")
		return os.path.isfile(options.runner)
	commands = []
	if not os.path.isfile(os.path.join(options.build_directory, "CMakeCache.txt")):
		commands.append(["cmake", "-S", PROJECT_DIRECTORY, "-B", options.build_directory])
	commands.append(["cmake", "--build", options.build_directory, "--target", "echo-headless", "--parallel"])
	for command in commands:
		completed = subprocess.run(command, capture_output=True, text=True)
		if completed.returncode != 0:
			print("Build failed: " + " ".join(command))
			print("\n".join((completed.stdout + completed.stderr).strip().split("\n")[-40:]))
			return False

	if host_test_names:
		everything = subprocess.run(["cmake", "--build", options.build_directory, "--target", "echo-tests", "--parallel"], capture_output=True, text=True)
		if everything.returncode != 0:   # find out which test(s) are to blame, one target at a time
			for source_name in host_test_names:
				single = subprocess.run(["cmake", "--build", options.build_directory, "--target", "echo-test-" + source_name], capture_output=True, text=True)
				if single.returncode != 0:
					options.host_build_errors[source_name] = (single.stdout + single.stderr).strip().split("\n")[-12:]
	return True


def main():
	parser = argparse.ArgumentParser(description="Run the PS1 test suite against the emulator core.")
	parser.add_argument("filters", nargs="*", help="tier number or test-path substring (default: everything)")
	parser.add_argument("--bios", default=os.path.join(PROJECT_DIRECTORY, "assets", "SCPH1001.BIN"))
	parser.add_argument("--build-directory", default=os.path.join(PROJECT_DIRECTORY, "build"))
	parser.add_argument("--runner", help="path to echo-headless (default: <build-directory>/echo-headless)")
	parser.add_argument("--no-build", action="store_true", help="don't rebuild echo-headless first")
	parser.add_argument("--manifest", default=os.path.join(SUITE_DIRECTORY, "tiers.txt"))
	parser.add_argument("--results", default=os.path.join(SUITE_DIRECTORY, "results"))
	parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
	parser.add_argument("--timeout", type=int, default=300, help="seconds before a single test is killed")
	parser.add_argument("--frames", type=int, default=0, help="override every test's frame limit")
	parser.add_argument("--quiet-frames", type=int, default=240, help="stop a test this many frames after its last output")
	parser.add_argument("--no-echo-tests", action="store_true", help="skip the standalone component tests in echo-tests/")
	parser.add_argument("--no-exotic", action="store_true", help="skip the exotic tier (hardware-characterisation tests)")
	parser.add_argument("--baseline", default=os.path.join(SUITE_DIRECTORY, "baseline.txt"), help="expected results (default: ps1-unit-tester/baseline.txt)")
	parser.add_argument("--update-baseline", action="store_true", help="record the current results as the new baseline (regressions are not recorded unless --force)")
	parser.add_argument("--force", action="store_true", help="with --update-baseline: record regressions too")
	parser.add_argument("--strict", action="store_true", help="ignore the baseline: every failure counts and sets the exit code")
	parser.add_argument("-v", "--verbose", action="store_true", help="list every test, not just the failures")
	options = parser.parse_args()

	if options.update_baseline and options.frames:
		print("--update-baseline cannot be combined with --frames: results with a changed frame limit are not comparable.")
		return 2
	if not options.runner:
		options.runner = os.path.join(options.build_directory, "echo-headless")
	options.host_build_errors = {}
	host_sources = [] if options.no_echo_tests else [name for name in discover_host_tests() if matches_filters("host", "echo-tests/" + name, options.filters)]
	if not options.no_build and not build_runner(options, host_sources):
		return 2
	if not os.path.isfile(options.runner):
		print("Runner not found: %s\nBuild it with: cmake -S . -B build && cmake --build build --target echo-headless" % options.runner)
		return 2
	if not os.path.isfile(options.bios):
		print("BIOS not found: %s (use --bios)" % options.bios)
		return 2

	entries = [entry for entry in read_manifest(options.manifest) if matches_filters(entry.tier, entry.path, options.filters) and not (options.no_exotic and entry.tier == "exotic")]
	if not entries and not host_sources:
		print("No tests match %s" % options.filters)
		return 2
	if entries and not os.path.isdir(os.path.join(SUITE_DIRECTORY, "public-tests")):
		print("ps1-unit-tester/public-tests/ not found. Run ps1-unit-tester/fetch_tests.sh first (see 'Getting the tests' in ps1-unit-tester/README.md).")
		return 2

	shutil.rmtree(options.results, ignore_errors=True)
	os.makedirs(options.results)

	jobs = [(lambda entry=entry: run_test(entry, options)) for entry in entries]
	jobs += [(lambda name=name: run_host_test(name, options)) for name in host_sources]
	print("Running %d tests (%d at a time)..." % (len(jobs), options.jobs), flush=True)

	results = [None] * len(jobs)
	finished = 0
	progress_lock = threading.Lock()
	started = time.time()
	with concurrent.futures.ThreadPoolExecutor(max_workers=options.jobs) as pool:
		futures = {pool.submit(job): index for index, job in enumerate(jobs)}
		for future in concurrent.futures.as_completed(futures):
			results[futures[future]] = future.result()
			with progress_lock:
				finished += 1
				if sys.stderr.isatty():
					sys.stderr.write("\r  %d/%d done" % (finished, len(jobs)))
					sys.stderr.flush()
	if sys.stderr.isatty():
		sys.stderr.write("\r" + " " * 30 + "\r")

	# Full listing on disk, failures on screen
	with open(os.path.join(options.results, "summary.txt"), "w") as summary_file:
		for result in results:
			summary_file.write("%-11s tier %-6s %-62s %s\n" % (result.verdict, result.tier, result.name, result.summary))
			for line in result.details:
				summary_file.write("\t%s\n" % line)

	if options.update_baseline:
		ran_everything = not options.filters and not options.no_echo_tests and not options.no_exotic
		return update_baseline(results, options, ran_everything)
	use_baseline = not options.strict and not options.frames and os.path.isfile(options.baseline)
	if use_baseline:
		return report_against_baseline(results, options, time.time() - started)

	failures = [result for result in results if result.failed]
	ungraded = [result for result in results if result.verdict in UNGRADED_VERDICTS]
	skipped = [result for result in results if result.verdict == "SKIPPED"]
	graded = [result for result in results if result.verdict not in UNGRADED_VERDICTS and result.verdict != "SKIPPED"]
	passed = [result for result in graded if not result.failed]

	if options.verbose:
		for result in passed + ungraded + skipped:
			print("%-11s tier %-6s %-62s %s" % (result.verdict, result.tier, result.name, result.summary))
		print()

	maximum_detail_lines = 12
	for result in failures:
		print("%-11s tier %-6s %s" % (result.verdict, result.tier, result.name))
		print("\t" + result.summary)
		for line in result.details[:maximum_detail_lines]:
			print("\t\t" + line)
		if len(result.details) > maximum_detail_lines:
			print("\t\t... %d more (see %s)" % (len(result.details) - maximum_detail_lines, os.path.relpath(os.path.join(options.results, result.name.replace("/", "_")), PROJECT_DIRECTORY)))
		print()

	elapsed = time.time() - started
	extras = []
	if ungraded:
		extras.append("%d informational, not graded" % len(ungraded))
	if skipped:
		extras.append("%d skipped (%s)" % (len(skipped), ", ".join(result.name for result in skipped)))
	extra_text = ("  (" + "; ".join(extras) + ")") if extras else ""
	if failures:
		print("%d of %d tests FAILED, %d passed in %.0fs.%s" % (len(failures), len(graded), len(passed), elapsed, extra_text))
	else:
		print("ALL %d TESTS PASSED in %.0fs.%s" % (len(graded), elapsed, extra_text))
	print("Details: %s" % os.path.relpath(options.results, PROJECT_DIRECTORY))
	if not options.strict and not options.frames:
		print("No baseline yet: run with --update-baseline to record these results, after which only changes are reported.")
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
