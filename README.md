# Echo PS1 Emulator

A lightweight, work-in-progress PlayStation 1 (PSX) emulator written in C++ using SDL2 for rendering and input handling.

## Built-in unit testing

Echo comes with a unit test suite in [`ps1-unit-tester/`](ps1-unit-tester/). It runs real PS1 test programs on the emulator core (no window needed) and checks the results against what the programs print and, where the test authors provide them, captures from real hardware.

Run it from the project root:

```
python3 ps1-unit-tester/run_tests.py
```

It rebuilds the headless runner, runs every test in parallel, and prints only the tests that **failed** and why, or `ALL N TESTS PASSED`. You need your own BIOS at `assets/SCPH1001.BIN` and the test programs downloaded first; see [`ps1-unit-tester/README.md`](ps1-unit-tester/README.md) for setup and options.

**Run the tests before you change any code**, so you know what the baseline is, and again afterwards. Some tests still fail while the emulator is a work in progress, so what matters is that the list of failures doesn't get longer.

Passing tests are not proof that the emulator is accurate. A test suite can't cover every nuance of the hardware, so treat this one as a check that nothing has **regressed**, not as a measure of progress.

### Where the tests come from

- [JaCzekanski/ps1-tests](https://github.com/JaCzekanski/ps1-tests) (MIT licence), including the real-hardware reference captures
- [PCSX-Redux nugget tests](https://github.com/pcsx-redux/nugget) (MIT licence)
- Amidog's `psxtest_cpu` and `psxtest_gte`, from [psx.amidog.se](https://psx.amidog.se)
- Echo's own Timers tests in [`ps1-unit-tester/echo-tests/`](ps1-unit-tester/echo-tests/)

The third-party tests are downloaded on your machine and are not stored in this repository. See [`ps1-unit-tester/PROVENANCE.md`](ps1-unit-tester/PROVENANCE.md) for exact versions.
