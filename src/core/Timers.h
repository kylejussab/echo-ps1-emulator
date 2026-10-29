#pragma once
#include <cstdint>
#include <functional>
#include "Constants.h"

/// The three root counters (0x1F801100 - 0x1F80112F).
///
///   Timer 0: system clock or dot clock; can be gated by HBlank
///   Timer 1: system clock or HBlank;    can be gated by VBlank
///   Timer 2: system clock or system clock / 8; sync modes just stop or free-run it
///
/// Video timing here is a simple model driven from the same cycle counts as the rest of the emulator:
/// a frame is Hardware::CYCLES_PER_FRAME cycles, split into Hardware::SCANLINES_PER_FRAME_NTSC lines, VBlank is
/// the first VBLANK_LINES lines (so it starts on the same boundary the VBlank IRQ is raised on), HBlank is the
/// last quarter of every line.
class Timers {
public:
    using InterruptCallback = std::function<void(uint16_t)>;  // receives the I_STAT bit to set
    using DotDividerCallback = std::function<int()>;          // GPU dot clock divider: 4, 5, 7, 8 or 10
    using VideoModeCallback = std::function<bool()>;          // GPU video mode (GP1(08h) bit 3): true = PAL, false = NTSC

    void setInterruptCallback(InterruptCallback callback) { raiseInterrupt = std::move(callback); }
    void setDotDividerCallback(DotDividerCallback callback) { dotDivider = std::move(callback); }
    /// Optional. Without it the timers run on NTSC timing only.
    void setVideoModeCallback(VideoModeCallback callback) { isPal = std::move(callback); }

    /// @brief Reads a timer register. Reading the mode register clears its "reached" flags (bits 11 and 12).
    uint32_t read(uint32_t address);
    void write(uint32_t address, uint32_t value);

    /// @brief Time passes lazily: cycles are accumulated and only simulated when something can observe them
    /// (a register access, a video edge, or a timer event that could raise an interrupt).
    void tick(int cycles) {
        elapsedCycles += static_cast<uint32_t>(cycles);

        // The GPU's horizontal resolution and NTSC/PAL mode can only change through a GP1 write, so they're only looked at
        // right after one (the Bus calls notifyVideoStateMayHaveChanged() from its GP1 handler) instead of on every cycle.
        if (videoStateMayHaveChanged) checkVideoState();

        if (elapsedCycles >= deadlineCycles) catchUp();
    }

    /// @brief Tells the timers that a GP1 command was just written, so the dot divider / video mode must be re-checked on the next tick.
    void notifyVideoStateMayHaveChanged() { videoStateMayHaveChanged = true; }

    bool isInHBlank() const { return inHBlank; }
    bool isInVBlank() const { return inVBlank; }

    static constexpr int VBLANK_LINES = 23; // NTSC: 263 total lines - 240 visible (PAL uses 314 - 288 = 26, see applyVideoMode)

private:
    static constexpr uint16_t MODE_IRQ_LINE_HIGH = 0x0400; // bit 10: 1 = no IRQ requested
    static constexpr uint16_t MODE_REACHED_TARGET = 0x0800; // bit 11
    static constexpr uint16_t MODE_REACHED_FFFF = 0x1000;   // bit 12

    struct Timer {
        uint16_t counter = 0;
        uint16_t target = 0;
        uint16_t mode = MODE_IRQ_LINE_HIGH;
        bool irqAlreadyFired = false; // for "IRQ once" mode
        bool gateOpened = false;      // sync mode 3 on timers 0/1: paused until the gate signal occurs once
        int pulseCyclesLeft = 0;
        bool pulseRestarted = false; // an event (re)started the pulse during the current span
        uint32_t divideAccumulator = 0; // timer 2, system clock / 8
    };

    Timer timers[3];
    InterruptCallback raiseInterrupt;
    DotDividerCallback dotDivider;
    VideoModeCallback isPal;

    // Video position
    int frameCycle = 0;
    int line = 0;
    int cycleInLine = 0;
    bool inHBlank = false;
    bool inVBlank = true; // a frame starts at line 0, which is inside VBlank
    uint64_t dotAccumulator = 0;
    int cachedDotDivider = 8; // divider the pending cycles were produced under

    // Timing of the current video mode (NTSC until the GPU reports PAL)
    bool palActive = false;
    int cyclesPerScanline = Hardware::CYCLES_PER_SCANLINE;
    int scanlinesPerFrame = Hardware::SCANLINES_PER_FRAME_NTSC;
    int cyclesPerFrame = Hardware::CYCLES_PER_SCANLINE * Hardware::SCANLINES_PER_FRAME_NTSC;
    int hblankStartCycle = Hardware::CYCLES_PER_SCANLINE * 3 / 4;
    int vblankLines = VBLANK_LINES;
    uint64_t dotRatioNumerator = 715909;   // GPU clock / CPU clock, reduced by 75: 53693175 / 33868800
    uint64_t dotRatioDenominator = 451584;

    // Lazy simulation
    uint32_t elapsedCycles = 0;   // cycles not simulated yet
    uint32_t deadlineCycles = 1;  // simulate once this many have built up

    bool videoStateMayHaveChanged = false;
    void checkVideoState() {
        videoStateMayHaveChanged = false;

        // Timer 0 on the dot clock depends on the GPU's horizontal resolution. If the game just changed it, the cycles that are
        // still pending belong to the old divider, so simulate them first.
        if ((timers[0].mode & 0x0100) && dotDivider && dotDivider() != cachedDotDivider) catchUp();

        // Same for a switch between NTSC and PAL: scanline length and the dot clock ratio both change with it
        if (isPal && isPal() != palActive) catchUp();
    }

    void catchUp();
    void refreshGpuState();
    void applyVideoMode(bool pal);
    void advance(uint32_t cycles);
    void countSpan(uint32_t span);
    void moveVideo(uint32_t span);
    uint32_t cyclesToNextVideoEdge() const;
    uint32_t computeDeadline() const;

    bool gateAllowsCounting(int index) const;
    void onGateStart(int index);
    void addTicks(int index, uint32_t ticks);
    void incrementCounter(int index);
    void requestInterrupt(int index);
};