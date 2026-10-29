#include "Timers.h"
#include "Constants.h"
#include <algorithm>

namespace {
    constexpr uint16_t TIMER_IRQ_BITS[3] = { Hardware::IRQ_TIMER0, Hardware::IRQ_TIMER1, Hardware::IRQ_TIMER2 };
}

// GPU clock / CPU clock, reduced by 75. NTSC: 53693175 / 33868800, PAL: 53203425 / 33868800
constexpr uint64_t DOT_RATIO_NUMERATOR_NTSC = 715909;
constexpr uint64_t DOT_RATIO_NUMERATOR_PAL = 709379;
constexpr uint64_t DOT_RATIO_DENOMINATOR = 451584;

uint32_t Timers::read(uint32_t address) {
    uint32_t index = (address - 0x1F801100) >> 4;
    if (index > 2) return 0;

    catchUp();

    Timer& timer = timers[index];
    switch (address & 0xC) {
        case 0x0: return timer.counter;
        case 0x4: {
            uint16_t value = timer.mode;
            timer.mode &= ~(MODE_REACHED_TARGET | MODE_REACHED_FFFF); // cleared on read
            return value;
        }
        case 0x8: return timer.target;
        default:  return 0;
    }
}


void Timers::write(uint32_t address, uint32_t value) {
    uint32_t index = (address - 0x1F801100) >> 4;
    if (index > 2) return;

    catchUp();

    Timer& timer = timers[index];
    switch (address & 0xC) {
        case 0x0:
            timer.counter = value & 0xFFFF;
            break;
        case 0x4:
            // Bits 0-9 are writable. Bit 10 is set again, bits 11-12 are cleared, and the counter restarts at 0.
            timer.mode = (value & 0x03FF) | MODE_IRQ_LINE_HIGH;
            timer.counter = 0;
            timer.irqAlreadyFired = false;
            timer.gateOpened = false;
            timer.pulseCyclesLeft = 0;
            timer.divideAccumulator = 0;
            break;
        case 0x8:
            timer.target = value & 0xFFFF;
            break;
        default:
            break;
    }

    refreshGpuState();
    deadlineCycles = computeDeadline();
}


// Picks up the GPU's current dot divider and video mode. Always called after the pending cycles were simulated, so
// everything that was pending was produced under the old settings.
void Timers::refreshGpuState() {
    if (dotDivider) cachedDotDivider = dotDivider();
    if (isPal && isPal() != palActive) applyVideoMode(isPal());
}


void Timers::applyVideoMode(bool pal) {
    palActive = pal;
    cyclesPerScanline = pal ? Hardware::CYCLES_PER_SCANLINE_PAL : Hardware::CYCLES_PER_SCANLINE;
    scanlinesPerFrame = pal ? Hardware::SCANLINES_PER_FRAME_PAL : Hardware::SCANLINES_PER_FRAME_NTSC;
    cyclesPerFrame = cyclesPerScanline * scanlinesPerFrame;
    hblankStartCycle = cyclesPerScanline * 3 / 4;
    vblankLines = pal ? scanlinesPerFrame - 288 : VBLANK_LINES; // PAL has 288 visible lines
    dotRatioNumerator = pal ? DOT_RATIO_NUMERATOR_PAL : DOT_RATIO_NUMERATOR_NTSC;
    dotRatioDenominator = DOT_RATIO_DENOMINATOR;
    dotAccumulator = 0; // the remainder is in units of the old ratio

    // Keep the position inside the new frame layout
    if (line >= scanlinesPerFrame) line = scanlinesPerFrame - 1;
    if (cycleInLine >= cyclesPerScanline) cycleInLine = cyclesPerScanline - 1;
    frameCycle = line * cyclesPerScanline + cycleInLine;
    inVBlank = line < vblankLines;
    inHBlank = cycleInLine >= hblankStartCycle;
}


void Timers::catchUp() {
    uint32_t remaining = elapsedCycles;
    elapsedCycles = 0;

    // No IRQ-raising event can happen before the deadline, so processing in deadline-sized chunks means events only ever
    // land on a chunk boundary and get their exact cycle (this matters for the 4-cycle pulse on bit 10).
    while (remaining > 0) {
        uint32_t chunk = std::min(remaining, computeDeadline());
        advance(chunk);
        remaining -= chunk;
    }
    refreshGpuState();
    deadlineCycles = computeDeadline();
}


void Timers::advance(uint32_t cycles) {
    while (cycles > 0) {
        uint32_t span = std::min(cycles, cyclesToNextVideoEdge());

        // Let a short pulse-mode IRQ line return high on exactly the right cycle
        for (const Timer& timer : timers) {
            if (timer.pulseCyclesLeft > 0) span = std::min<uint32_t>(span, timer.pulseCyclesLeft);
        }

        // Only pulses that were already running when this span began count down during it
        // (one raised by an event inside the span starts fresh at the end of it)
        bool pulseWasRunning[3];
        for (int index = 0; index < 3; index++) pulseWasRunning[index] = timers[index].pulseCyclesLeft > 0;

        countSpan(span);
        moveVideo(span);

        for (int index = 0; index < 3; index++) {
            Timer& timer = timers[index];
            if (timer.pulseRestarted) {
                timer.pulseRestarted = false; // started by an event in this span: counts down from the next one
            }
            else if (pulseWasRunning[index]) {
                timer.pulseCyclesLeft -= static_cast<int>(span);
                if (timer.pulseCyclesLeft <= 0) { timer.pulseCyclesLeft = 0; timer.mode |= MODE_IRQ_LINE_HIGH; }
            }
        }
        cycles -= span;
    }
}


// Counts every clock source that ticks continuously. The video gate state is constant for the whole span.
void Timers::countSpan(uint32_t span) {
    for (int index = 0; index < 3; index++) {
        Timer& timer = timers[index];
        int clockSource = (timer.mode >> 8) & 0x3;
        uint32_t ticks = 0;

        if (index == 0) {
            if (clockSource & 1) { // dot clock = GPU clock / divider (53.693175 MHz NTSC, 53.203425 MHz PAL), system clock = 33.8688 MHz
                uint32_t divider = static_cast<uint32_t>(cachedDotDivider);
                dotAccumulator += dotRatioNumerator * span;
                ticks = static_cast<uint32_t>(dotAccumulator / (dotRatioDenominator * divider));
                dotAccumulator -= static_cast<uint64_t>(ticks) * dotRatioDenominator * divider;
            }
            else ticks = span;
        }
        else if (index == 1) {
            if (clockSource & 1) continue; // HBlank source: counted at the HBlank edge in moveVideo()
            ticks = span;
        }
        else {
            if (clockSource & 2) { // system clock / 8
                timer.divideAccumulator += span;
                ticks = timer.divideAccumulator / 8;
                timer.divideAccumulator %= 8;
            }
            else ticks = span;
        }

        if (ticks != 0 && gateAllowsCounting(index)) addTicks(index, ticks);
    }
}


uint32_t Timers::cyclesToNextVideoEdge() const {
    uint32_t toFrameEnd = cyclesPerFrame - frameCycle;
    uint32_t toLineEvent;
    if (line == scanlinesPerFrame - 1) {
        toLineEvent = (cycleInLine < hblankStartCycle) ? hblankStartCycle - cycleInLine : toFrameEnd; // the last line runs to the end of the frame
    }
    else {
        toLineEvent = (cycleInLine < hblankStartCycle) ? hblankStartCycle - cycleInLine : cyclesPerScanline - cycleInLine;
    }
    return std::max<uint32_t>(1, std::min(toFrameEnd, toLineEvent));
}


void Timers::moveVideo(uint32_t span) {
    frameCycle += span;
    cycleInLine += span;
    if (frameCycle >= cyclesPerFrame) {
        frameCycle = 0;
        line = 0;
        cycleInLine = 0;
    }
    else if (cycleInLine >= cyclesPerScanline && line < scanlinesPerFrame - 1) {
        line++;
        cycleInLine = 0;
    }

    bool nowInVBlank = line < vblankLines;
    bool nowInHBlank = cycleInLine >= hblankStartCycle;
    bool hblankStarted = nowInHBlank && !inHBlank;
    bool vblankStarted = nowInVBlank && !inVBlank;
    inHBlank = nowInHBlank;
    inVBlank = nowInVBlank;

    if (hblankStarted) onGateStart(0);
    if (vblankStarted) onGateStart(1);

    // Timer 1 with the HBlank clock source ticks once per HBlank
    if (hblankStarted && ((timers[1].mode >> 8) & 1) && gateAllowsCounting(1)) addTicks(1, 1);
}


// Lower bound on the number of cycles until something happens that has to be delivered on time (an IRQ).
// Gating and clock dividers can only make events later than this, never earlier.
uint32_t Timers::computeDeadline() const {
    uint32_t deadline = cyclesToNextVideoEdge();

    for (int index = 0; index < 3; index++) {
        const Timer& timer = timers[index];
        bool irqOnTarget = (timer.mode & 0x0010) != 0;
        bool irqOnFFFF = (timer.mode & 0x0020) != 0;
        if (!irqOnTarget && !irqOnFFFF) continue;
        if ((timer.mode & 0x0040) == 0 && timer.irqAlreadyFired) continue;

        int clockSource = (timer.mode >> 8) & 0x3;
        if (index == 1 && (clockSource & 1)) continue; // HBlank source only moves at video edges, which already cap the deadline

        uint32_t ticks = 65536;
        if (irqOnTarget) ticks = std::min<uint32_t>(ticks, std::max<uint32_t>(1, (timer.target - timer.counter) & 0xFFFF));
        if (irqOnFFFF)   ticks = std::min<uint32_t>(ticks, std::max<uint32_t>(1, (0xFFFF - timer.counter) & 0xFFFF));
        if ((timer.mode & 0x0008) && timer.counter == timer.target) ticks = 1;

        uint64_t cycles;
        if (index == 0 && (clockSource & 1)) cycles = static_cast<uint64_t>(ticks - 1) * 2 + 1; // dot clock is at most 11/28 ticks per cycle; the first tick can come at once
        else if (index == 2 && (clockSource & 2)) cycles = static_cast<uint64_t>(ticks - 1) * 8 + 1;
        else cycles = ticks;

        deadline = static_cast<uint32_t>(std::min<uint64_t>(deadline, std::max<uint64_t>(1, cycles)));
    }
    return deadline;
}


bool Timers::gateAllowsCounting(int index) const {
    const Timer& timer = timers[index];
    if ((timer.mode & 1) == 0) return true; // sync disabled: free run

    int syncMode = (timer.mode >> 1) & 0x3;
    if (index == 2) {
        return syncMode == 1 || syncMode == 2; // modes 0 and 3 stop the counter
    }

    bool gateActive = (index == 0) ? inHBlank : inVBlank;
    switch (syncMode) {
        case 0:  return !gateActive;       // pause during the gate
        case 1:  return true;              // free run (the counter is reset when the gate starts)
        case 2:  return gateActive;        // only count during the gate (and reset when it starts)
        default: return timer.gateOpened;  // paused until the gate occurs once, then free run
    }
}


void Timers::onGateStart(int index) {
    Timer& timer = timers[index];
    if ((timer.mode & 1) == 0) return;

    int syncMode = (timer.mode >> 1) & 0x3;
    if (syncMode == 1 || syncMode == 2) timer.counter = 0;
    else if (syncMode == 3) timer.gateOpened = true;
}


// Adds many ticks at once, fast-forwarding over the stretches where nothing observable happens
void Timers::addTicks(int index, uint32_t ticks) {
    Timer& timer = timers[index];

    while (ticks > 0) {
        uint32_t toTarget = (timer.target - timer.counter) & 0xFFFF;  // ticks until counter == target (0 = it is there already)
        uint32_t toFFFF = (0xFFFF - timer.counter) & 0xFFFF;

        if (toTarget == 0) toTarget = (timer.mode & 0x0008) ? 1 : 65536;
        if (toFFFF == 0) toFFFF = 65536;

        uint32_t nextEvent = std::min(toTarget, toFFFF);   // the tick number on which something happens
        uint32_t quiet = nextEvent - 1;                    // ticks before that are plain increments
        uint32_t step = std::min(ticks, quiet);

        timer.counter = static_cast<uint16_t>(timer.counter + step);
        ticks -= step;

        if (ticks > 0) {
            incrementCounter(index);
            ticks--;
        }
    }
}


void Timers::incrementCounter(int index) {
    Timer& timer = timers[index];

    // With "reset at target" the target value is visible for one tick and then the counter restarts at 0
    if ((timer.mode & 0x0008) && timer.counter == timer.target) timer.counter = 0;
    else timer.counter++; // 16-bit wrap

    if (timer.counter == timer.target) {
        timer.mode |= MODE_REACHED_TARGET;
        if (timer.mode & 0x0010) requestInterrupt(index);
    }
    if (timer.counter == 0xFFFF) {
        timer.mode |= MODE_REACHED_FFFF;
        if (timer.mode & 0x0020) requestInterrupt(index);
    }
}


void Timers::requestInterrupt(int index) {
    Timer& timer = timers[index];

    bool repeat = (timer.mode & 0x0040) != 0;
    bool toggle = (timer.mode & 0x0080) != 0;
    if (!repeat && timer.irqAlreadyFired) return;
    timer.irqAlreadyFired = true;

    if (toggle) {
        timer.mode ^= MODE_IRQ_LINE_HIGH;
        if ((timer.mode & MODE_IRQ_LINE_HIGH) == 0 && raiseInterrupt) raiseInterrupt(TIMER_IRQ_BITS[index]); // only the high->low edge raises it
    }
    else {
        timer.mode &= ~MODE_IRQ_LINE_HIGH;
        timer.pulseCyclesLeft = 4;
        timer.pulseRestarted = true;
        if (raiseInterrupt) raiseInterrupt(TIMER_IRQ_BITS[index]);
    }
}