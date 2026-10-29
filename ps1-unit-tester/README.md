# PS1 unit tester

A test runner for the emulator core: public hardware tests (Jaczekanski, Amidog, pcsx-redux), our own component tests (`echo-tests/`) and a
baseline of expected results, so a run tells you what got worse instead of re-listing what is already known to be broken.

Run everything, from the project root:

```
python3 ps1-unit-tester/run_tests.py
```

It rebuilds `echo-headless` (the emulator core without a window), runs every test in `tiers.txt` in parallel, and prints only what needs attention.
The exit code is 0 when nothing got worse and 1 otherwise.

```
python3 ps1-unit-tester/run_tests.py gte          # only tests with "gte" in their path
python3 ps1-unit-tester/run_tests.py 3            # only tier 3
python3 ps1-unit-tester/run_tests.py exotic       # only the exotic tier (tiers can be picked by number or by name)
python3 ps1-unit-tester/run_tests.py -v           # also list passing, known-failing and informational tests
python3 ps1-unit-tester/run_tests.py --no-build   # don't rebuild first
python3 ps1-unit-tester/run_tests.py --no-exotic  # leave out the exotic tier
python3 ps1-unit-tester/run_tests.py -j 4         # 4 tests at a time
```

Everything for each test lands in `results/<test>/`: `tty.txt` (what the test printed), `vram.png`, `stdout.txt` (the emulator's own
output, including any FATAL line), `log.diff` and `vram-diff.png` (red = differs from the real-hardware reference).

## The baseline

`baseline.txt` records what every test did the last time you accepted the results: its verdict and, for failing tests, how wrong it was
(pixels that differ, differing log lines, failed checks). `baseline/` holds the printed output of the informational tests. Commit both.

Once `baseline.txt` exists, a run reports only changes against it:

| category | meaning | fails the run |
|---|---|---|
| `REGRESSION` | passed before and fails now, or fails in the same way as before but worse (more pixels, more lines, more failed checks), or fails harder (a wrong result became a crash) | yes |
| `NEW-FAILING` | a test that is not in the baseline yet and fails | yes |
| `IMPROVED` | a failure that got better or now passes | no |
| `CHANGED` | still failing, but with a different verdict of the same severity | no |
| `OUTPUT-CHANGED` | an informational (`info`/`timing`) test printed something different from the saved output (`results/<test>/baseline.diff` has the full diff) | no |
| `NEW` | not in the baseline yet, and fine | no |

Known failures are hidden unless you use `-v`.

```
python3 ps1-unit-tester/run_tests.py --update-baseline   # accept the current results
python3 ps1-unit-tester/run_tests.py --strict            # ignore the baseline: every failure counts, like a run without one
```

`--update-baseline` records improvements, new tests and changed output, but **refuses to record a regression** (it lists them and exits 1);
add `--force` to accept one on purpose. Run on a subset (`... 3 --update-baseline`) it only touches the tests that ran; a complete run also
drops entries of tests that are no longer in the suite. With no `baseline.txt` at all, the runner behaves as `--strict` and says so.

The saved output of informational tests is a snapshot of what *this emulator* printed when you accepted it, not what the hardware does.
It is only useful if the emulator is deterministic: if a test's output changes between two identical runs, something depends on host timing.

## Looking at the failures

A normal run hides known failures. To work on them:

```
python3 ps1-unit-tester/run_tests.py --strict            # every failure with its details, ignoring the baseline
python3 ps1-unit-tester/run_tests.py --strict 3          # the same, only tier 3
python3 ps1-unit-tester/run_tests.py --strict triangle   # the same, only tests with "triangle" in their path
python3 ps1-unit-tester/run_tests.py -v                  # one line per known failure (verdict and size), no details
```

`results/summary.txt` is written on every run, normal or strict, and lists all tests with their verdict, summary and details, so you can read
it after a plain run without running anything again. The per-test files (`tty.txt`, `stdout.txt`, `log.diff`, `vram-diff.png`) are in `results/<test>/`.

## Getting the tests

The test programs are not stored in the repo (about 28 MB, and not all of them are ours to redistribute), and you need your own BIOS. Neither is committed.

1. Put a BIOS at `assets/SCPH1001.BIN` (or pass `--bios <file>` to `run_tests.py`).
2. Download the Jaczekanski and Amidog tests into `ps1-unit-tester/public-tests/`:

```
./ps1-unit-tester/fetch_tests.sh
```

3. The pcsx-redux tests have no prebuilt binaries, so build them from the nugget repo (exact commits and toolchain are in `PROVENANCE.md`: `make PREFIX=mipsel-linux-gnu FORMAT=elf32-tradlittlemips`) and put each `.ps-exe` at the path `tiers.txt` lists, for example `public-tests/pcsx-redux/basic/basic.ps-exe`.

Tests whose files are missing are reported as `SKIPPED` (and ignored by the baseline), so you can run with only some of the sets.

## How a test is judged (the `checks` column in tiers.txt)

| check | meaning |
|---|---|
| `sum` | the test's own pass/fail counts (`pass - ` / `fail - ` lines, or libcester's summary) must show no failures |
| `log` | the printed output must match the real-hardware `psx.log` next to the test (the Sony library's `ResetGraph:` start-up lines are ignored) |
| `vram` | VRAM at the end must match the reference `vram.png` next to the test |
| `timing`, `info` | run, but not graded: the numbers are hardware measurements. Their printed output is compared with the baseline instead |

Verdicts: `FAIL`, `LOG-DIFF`, `VRAM-DIFF`, `CRASH` (your FATAL / `exit(1)` text is shown), `TIMEOUT`, `NO-SIDELOAD` (the BIOS never reached the shell),
`NO-RESULT`, `HOST-FAIL` (a component test in `echo-tests/` that does not compile, was not built, or crashed).

## Tiers

Tiers 1 to 6 are ordered by dependency: a later tier builds on the earlier ones, so when several fail, start with the lowest.

| Tier | Covers |
|---|---|
| 1 | CPU, exceptions, COP0 |
| 2 | Timers, interrupts, DMA |
| 3 | GPU commands, VRAM |
| 4 | GTE |
| 5 | MDEC, SPU, CD-ROM |
| 6 | Pixel-exact rasterizer (strict, pixel for pixel) |
| host | Our own component tests in `echo-tests/` (Timers so far), built by CMake and run without a BIOS |
| exotic | Hardware-characterisation tests: quirks of the real console that games do not depend on (bus access widths, COP0 breakpoints, scratchpad as data cache, unusual coprocessor branches, bus timing). They run with everything else; the ones that fail today are known failures in the baseline. |

## Layout

- `runner/echo_headless.cpp`: the windowless runner. Builds a Bus and CPU, loads the BIOS, starts the test program when the BIOS reaches the shell, captures its printf output and dumps VRAM. The emulator itself contains none of this.
- `run_tests.py`: the driver described above.
- `baseline.txt`, `baseline/`: expected results and the saved output of informational tests.
- `echo-tests/`: our own component tests, one program per `.cpp` (no BIOS needed). CMake builds them (target `echo-tests`) and the driver runs them as tier `host`; to add one, add a `.cpp` there. `echo-tests/README.txt` explains the check helpers.
- `public-tests/`: the test programs and their real-hardware references. Not in the repo: see "Getting the tests" above. `PROVENANCE.md` says where each came from.
- `fetch_tests.sh`: downloads the Jaczekanski and Amidog tests into `public-tests/`.
- `tiers.txt`: the manifest: `tier | checks | path | max frames | extra runner arguments (optional)`. The last column is passed straight to `echo-headless`; for example `--press start@150+10 --quiet-exit 2000` is how a menu-driven test like amidog's psxtest_gte gets started.

`--press <button>@<frame>[+<heldFrames>]` plugs a virtual controller into the emulator and presses a button at that frame
(`start select up down left right cross circle square triangle l1 r1 l2 r2 l3 r3`). It needs SDL 2.0.14 or newer.

Tests that need a human (interactive, audio-only) or are specific to other emulators are not in the manifest; run one by hand with
`build/echo-headless --bios assets/SCPH1001.BIN --exe ps1-unit-tester/public-tests/<path> --tty-log out.txt --vram-png out.png`.
