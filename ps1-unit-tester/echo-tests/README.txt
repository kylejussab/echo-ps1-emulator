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
  dma_interrupt_control : the DMA interrupt register (DICR) and how a finished transfer reaches I_STAT bit 3 (IRQ3): the bit layout (which bits hold
                   what is written, which read 0, which cannot be set), a flag latching only when its channel's enable bit is set and clearing
                   when a 1 is written to it, the flag following the channel number (channels 6 and 2), the force bit, the master enable, IRQ3 being
                   raised on the 0 -> 1 edge of bit 31 and not while it stays 1, I_MASK gating, and DICR / DPCR read through byte and halfword
                   accesses (each in its own child process, because the Bus prints FATAL and exits on an access it does not handle). Finished
                   transfers are made with the ordering-table clear channel, which only writes RAM. Left out because the hardware behaviour is
                   not confirmed: byte/halfword writes to DICR, a flag latching while the master enable is off, and whether bit 31 follows
                   "enable AND flag" or just "flag".
  sio_pad_protocol : the controller on SIO0, driven the way a game drives it (bytes to JOY_DATA, replies back, /ACK watched in JOY_STAT). Byte-exact
                   transactions: the digital poll (FF 41 5A FF FF) and analog poll (FF 73 5A ...), /ACK after every byte but the last, configuration
                   mode (enter, exit, ID F3h), the status and constant queries, setting the analog mode and its lock, the rumble mapping, and who
                   answers (dropping DTR restarts a packet, nothing on port 2, nothing at the memory card address, nothing with DTR low).
                   There is no host gamepad in a test run, so every poll reads "nothing pressed, sticks centred": button bit positions and stick byte
                   order cannot be checked until the pad can be fed values. Left out because the hardware behaviour is not confirmed: unknown commands,
                   a poll sent in configuration mode, the first rumble-mapping reply, the exact cycle of /ACK.
  sio_registers : the SIO0 registers: power-on JOY_STAT, readback of JOY_MODE / JOY_CTRL / JOY_BAUD (bits 4 and 6 of JOY_CTRL write-only), 16- and
                   32-bit layout, the status bits while a byte shifts, the byte time (8 * reload * multiplier cycles for multipliers 1, 16, 64), the
                   /ACK pulse and the interrupt flag it raises (only with the DSR interrupt enabled, cleared by JOY_CTRL bit 4), the reset bit, byte and
                   halfword reads of the registers, and IRQ7 reaching I_STAT through the Bus. Left out because the hardware behaviour is not confirmed:
                   narrow writes, the baud timer in JOY_STAT, writing JOY_DATA with transmit disabled, a baud reload of 0.
  sio1_registers : the SIO1 (serial port) registers, against the stable psx-spx page: power-on STAT, readback of MODE / CTRL / BAUD, the bits that
                   are always zero (MODE 8-15, CTRL 14-15) and the write-only CTRL bits 4 and 6, 32-bit layout, the baud timer in STAT (loaded with
                   reload * factor / 2 on a write to BAUD, down one per cycle), what a read of DR returns at 8, 16 and 32 bits (the FIFO preview and
                   how many entries each width removes), and byte reads of the 16-bit registers. Left out because the page says "probably" or gives no
                   value: SIO1_MISC, STAT bits 0 and 2 at power-on, the RX input level, an empty-FIFO read, narrow writes.
  sio1_transfer : SIO1 sending and receiving: frame time at several baud and frame settings (start, 5-8 data, parity, 1 / 1.5 / 2 stop bits, floor of
                   the divisor), the TX status bits, two bytes back to back, DR written again while busy (overwrite), TXEN, CTS, baud factor 0 (STOP),
                   the 8-byte RX FIFO with its overrun, RXEN clearing the FIFO, the RX interrupt level (1, 2, 4, 8 bytes) and its acknowledge, the DSR
                   and CTS inputs, reset, and IRQ8 reaching I_STAT through the Bus. Left out: the TX interrupt, parity and framing errors, the
                   latched-TXEN quirk, an empty-FIFO read, what a reset does to BAUD.
