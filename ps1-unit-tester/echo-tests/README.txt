Component tests written for this project: each one exercises a piece of the emulator core (src/) directly, with no BIOS and no disc.

How it works
  - Every echo-tests/*.cpp is its own program. CMake (see the echo-tests target in CMakeLists.txt) builds each one as
    build/echo-tests/echo-test-<name>, linked against echo_core, and run_tests.py runs them next to the public tests (tier "host").
    Adding a test is adding a .cpp file here; CMake picks it up on the next build. The support/ folder holds shared headers, not tests.
  - A test reports with the helpers in support/echo_check.h, which print "pass - <name>" / "fail - <name> (<details>)" lines, the same format
    the ps1-tests use. A failed check does not stop the test. A test ends with `return echo_check::finish();`.
  - run_tests.py judges a test by those lines: any "fail - " is FAIL (the failing lines are listed), no pass/fail lines at all is NO-RESULT,
    and HOST-FAIL means it did not compile, crashed, or was not built. Skip them all with --no-echo-tests.

Running one by hand, from the repo root:

  cmake --build build --target echo-test-timers_unit && build/echo-tests/echo-test-timers_unit
  cmake --build build --target echo-tests              # build all of them

Helpers
  support/echo_check.h       check(condition, name, details), checkEqual(actual, expected, name), checkRange(actual, low, high, name),
                             note(text), hex(value, digits), finish(). Put the values that explain a failure in `details`; it is only printed on failure.
  support/timer_registers.h  register offsets and bit names for the Timers tests (readRegister / writeRegister / syncMode).
  support/synthetic_bios.h   writes a made-up 512 KB BIOS to the temp directory so a test can build a Bus without the real BIOS.
  support/synthetic_disc.h   a Workspace (temporary directory, removed afterwards) that writes .bin files full of recognisable sectors and .cue
                             sheets from text a test supplies. Every sector carries its own number, a sync pattern and a header, so a
                             test can tell exactly which sector it got back. expectedSector(lba) gives the bytes a sector must have.
  support/captured_output.h  CapturedOutput holds back what the core writes to std::cout / std::cerr while it is alive (Disc prints a line per
                             disc loaded and per sector it cannot read), and can hand the text back for a check.
  support/isolated_probe.h   run(probe) executes a function in a forked child and reports whether it finished, what it returned, or how it died.
                             Use it where the core prints FATAL and exits: one unsupported access becomes one failed check instead of the
                             end of the test. The probe builds its own Bus. POSIX only (macOS, Linux).

Tests
  timers_unit : 29 checks of the documented counter, mode, target, IRQ, clock-source and sync-mode behaviour.
  timers_diff : thousands of random register writes and ticks against a copy of Timers forced to simulate one cycle at a time (reading a
                counter makes it catch up by one cycle), comparing every counter, mode value and IRQ count. Run it again after any change
                to Timers.cpp. It stops at the first mismatch, because the two copies have diverged by then, and prints the last operations.
  bus_memory_map : the Bus address decode against the documented memory map: RAM and its four 2 MB mirrors, KUSEG/KSEG0/KSEG1 aliasing,
                   the scratchpad, little-endian byte/halfword/word agreement, the BIOS ROM (visible, write-protected, writes do not reach
                   RAM) and unattached expansion region 1 (reads all ones). An unmapped access makes the Bus print FATAL and exit, which the
                   driver reports as HOST-FAIL with that line. What depends on the segment the CPU used (scratchpad through KSEG1, KSEG2)
                   is not visible at the Bus and is not covered.
  bus_register_widths : register accesses through the Bus, each probe in its own child process. SIO1 at 8, 16 and 32 bits, the RAM size
                   register reading back what was written, I_STAT (a raised interrupt is visible, writing 0 acknowledges only that bit,
                   writing 1s raises nothing), I_MASK (the eleven bits read back) and the I_STAT/I_MASK gating that decides whether the CPU
                   sees an interrupt. Left out on purpose because the hardware behaviour is not confirmed: the unused upper bits of I_MASK and
                   8-bit access to the interrupt registers.
  disc_cue_parsing : Disc reading a cue sheet: the track table (numbers, types, file names), INDEX 01 timestamps to sector numbers (ten values
                   from 00:00:00 to 79:59:74, worked out by hand), only INDEX 01 marking a track start, a single BIN and a BIN per track
                   laid end to end (file start sectors, total sectors), cue syntax variants (CRLF, no final newline, tabs and blank
                   lines, metadata lines, unquoted names, names with spaces), reloading, an empty drive, a missing or empty cue, and a cue whose
                   BIN file is missing, which has to fail cleanly instead of crashing (that probe runs in its own child process).
                   The 150-sector pregap is not in Disc (sector 0 is 00:02:00); it is applied by the CD-ROM controller.
  disc_sector_reads : Disc::readSector: all 2352 bytes of every sector of a single BIN and of a three-file image come back as that sector, in
                   order and out of order, never more than 2352 bytes written, the last sector reads, the one after it and the largest
                   sector number do not, an incomplete sector at the end of a truncated image is not returned, a MODE1/2352 image reads like
                   any other, and reads fail (without crashing) with no disc or after the BIN file is removed.
