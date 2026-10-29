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
                             note(text), finish(). Put the values that explain a failure in `details`; it is only printed on failure.
  support/timer_registers.h  register offsets and bit names for the Timers tests (readRegister / writeRegister / syncMode).

Tests
  timers_unit : 29 checks of the documented counter, mode, target, IRQ, clock-source and sync-mode behaviour.
  timers_diff : thousands of random register writes and ticks against a copy of Timers forced to simulate one cycle at a time (reading a
                counter makes it catch up by one cycle), comparing every counter, mode value and IRQ count. Run it again after any change
                to Timers.cpp. It stops at the first mismatch, because the two copies have diverged by then, and prints the last operations.
