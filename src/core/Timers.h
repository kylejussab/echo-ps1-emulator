#pragma once

#include <cstdint>
#include <functional>
#include "Constants.h"

/// @brief Emulates the three PS1 hardware timers (0x1F801100 - 0x1F80112F).
/// @details
///   Timer 0: system clock or dot clock; can be gated by HBlank.
///   Timer 1: system clock or HBlank;    can be gated by VBlank.
///   Timer 2: system clock or system clock / 8; sync modes just stop or free-run it.
///
/// Video timing here is a simple model driven from the same cycle counts as the rest of the emulator a frame is Hardware::CYCLES_PER_FRAME cycles, split into Hardware::SCANLINES_PER_FRAME_NTSC lines.
/// VBlank is the first VBLANK_LINES lines (so it starts on the same boundary the VBlank IRQ is raised on), and HBlank is the last quarter of every line.
class Timers {
public:
    /// @brief Callback to assert a specific interrupt in the I_STAT register.
    using InterruptCallback = std::function<void(uint16_t)>;
    
    /// @brief Callback to retrieve the current GPU dot clock divider (4, 5, 7, 8, or 10).
    using DotDividerCallback = std::function<int()>;
    
    /// @brief Callback to retrieve the current GPU video mode (GP1(08h) bit 3). True = PAL, false = NTSC.
    using VideoModeCallback = std::function<bool()>;


    using VBlankCallback = std::function<void()>;


    /// @brief Sets the callback used to trigger hardware interrupts.
    /// @param callback Function accepting the I_STAT interrupt bit to set.
    void setInterruptCallback(InterruptCallback callback) { raiseInterrupt = std::move(callback); }
    

    /// @brief Sets the callback used to query the GPU's dot divider.
    /// @param callback Function returning the active dot divider.
    void setDotDividerCallback(DotDividerCallback callback) { dotDivider = std::move(callback); }
    

    /// @brief Sets the callback used to query the GPU's regional video mode.
    /// @param callback Function returning true for PAL, false for NTSC. Without this, timers default to NTSC.
    void setVideoModeCallback(VideoModeCallback callback) { isPal = std::move(callback); }


    void setVBlankCallback(VBlankCallback cb) { onVBlank = std::move(cb); }

    
    /// @brief Reads a timer register. Reading the mode register clears its "reached" flags (bits 11 and 12).
    /// @param address The physical memory address of the register.
    /// @return The 32-bit value of the requested register.
    uint32_t read(uint32_t address);
    

    /// @brief Writes to a timer register.
    /// @param address The physical memory address of the register.
    /// @param value The 32-bit value to write.
    void write(uint32_t address, uint32_t value);


    /// @brief Time passes lazily: cycles are accumulated and only simulated when something can observe them.
    /// @param cycles The number of CPU cycles that have elapsed since the last synchronization point.
    void advanceClock(int cycles);


    /// @brief Tells the timers that a GP1 command was just written, so the dot divider / video mode must be re-checked on the next tick.
    void notifyVideoStateMayHaveChanged() { videoStateMayHaveChanged = true; }


    /// @brief Checks if the emulated video beam is currently in the Horizontal Blanking period.
    /// @return True if in HBlank.
    bool isInHBlank() const { return inHBlank; }


    /// @brief Checks if the emulated video beam is currently in the Vertical Blanking period.
    /// @return True if in VBlank.
    bool isInVBlank() const { return inVBlank; }

    static constexpr int VBLANK_LINES = 23; // NTSC: 263 total lines - 240 visible (PAL uses 314 - 288 = 26, see applyVideoMode)

private:
    struct Timer {
        uint16_t counter = 0;
        uint16_t target = 0;
        uint16_t mode = MODE_IRQ_LINE_HIGH;
        bool irqAlreadyFired = false; // for "IRQ once" mode
        bool gateOpened = false; // sync mode 3 on timers 0/1: paused until the gate signal occurs once
        int pulseCyclesLeft = 0;
        bool pulseRestarted = false; // an event (re)started the pulse during the current span
        uint32_t divideAccumulator = 0; // timer 2, system clock / 8
    };

    static constexpr uint16_t MODE_IRQ_LINE_HIGH = 0x0400; // bit 10: 1 = no IRQ requested
    static constexpr uint16_t MODE_REACHED_TARGET = 0x0800; // bit 11
    static constexpr uint16_t MODE_REACHED_FFFF = 0x1000;   // bit 12

    static constexpr uint16_t TIMER_IRQ_BITS[3] = { Hardware::IRQ_TIMER0, Hardware::IRQ_TIMER1, Hardware::IRQ_TIMER2 };

    // GPU clock / CPU clock, reduced by 75. NTSC: 53693175 / 33868800, PAL: 53203425 / 33868800
    static constexpr uint64_t DOT_RATIO_NUMERATOR_NTSC = 715909;
    static constexpr uint64_t DOT_RATIO_NUMERATOR_PAL = 709379;
    static constexpr uint64_t DOT_RATIO_DENOMINATOR = 451584;

    Timer timers[3];
    InterruptCallback raiseInterrupt;
    DotDividerCallback dotDivider;
    VideoModeCallback isPal;
    VBlankCallback onVBlank;

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


    /// @brief Validates if pending cycles need to be flushed due to a GPU video state change.
    void checkVideoState();


    /// @brief Processes all pending lazily-accumulated cycles up to the current simulation time.
    void catchUp();
    

    /// @brief Updates local dot clock and video mode caches from the GPU callbacks.
    void refreshGPUState();
    

    /// @brief Reconfigures internal scanline and frame boundaries based on the video region.
    /// @param pal True for PAL timing, false for NTSC timing.
    void applyVideoMode(bool pal);
    

    /// @brief Core loop that steps the timers and video beam forward by a requested span.
    /// @param cycles The number of CPU cycles to simulate.
    void advance(uint32_t cycles);
    

    /// @brief Advances timers across a contiguous block of cycles without crossing video boundaries.
    /// @param span The continuous chunk of cycles to process.
    void countSpan(uint32_t span);
    

    /// @brief Steps the emulated video beam (HBlank/VBlank state) forward.
    /// @param span The number of CPU cycles the beam traverses.
    void moveVideo(uint32_t span);
    

    /// @brief Calculates the remaining cycles until the next horizontal or vertical blanking edge.
    /// @return CPU cycles until the next video boundary.
    uint32_t cyclesToNextVideoEdge() const;
    

    /// @brief Calculates the maximum number of cycles the timers can sleep before an event requires processing.
    /// @return CPU cycles until the next strict deadline.
    uint32_t computeDeadline() const;


    /// @brief Evaluates if the current sync mode gate permits the timer to increment.
    /// @param index The timer index (0-2).
    /// @return True if the timer is allowed to count.
    bool gateAllowsCounting(int index) const;
    

    /// @brief Handles the rising edge of a gate signal (e.g., entering HBlank or VBlank).
    /// @param index The timer index (0-2).
    void onGateStart(int index);
    

    /// @brief Directly adds a calculated block of ticks to a timer's counter.
    /// @param index The timer index (0-2).
    /// @param ticks The number of ticks to add.
    void addTicks(int index, uint32_t ticks);
    

    /// @brief Steps a timer's counter by 1 and checks for target/overflow triggers.
    /// @param index The timer index (0-2).
    void incrementCounter(int index);
    

    /// @brief Triggers the hardware interrupt pulse for a specific timer.
    /// @param index The timer index (0-2).
    void requestInterrupt(int index);
};