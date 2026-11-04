#include "Constants.h"
#include "PlayStation.h"

PlayStation::PlayStation() : cpu(&bus) {
}


bool PlayStation::powerOn(const std::string& biosPath, const std::string& cuePath) {
    if (!bus.loadBIOS(biosPath)) {
        return false;
    }

    if (!cuePath.empty()) {
        bus.getCDROM().mount(cuePath);
    }

    return true;
}


void PlayStation::runFrame() {
    /// @todo Move from a fixed per-frame block to a precise cycle-stepped scheduler.
    for (int i = 0; i < Hardware::CYCLES_PER_FRAME; i++) {
        cpu.step();
    }
}


void PlayStation::handleEvent(const SDL_Event& event) {
    /// @todo Expand input routing to handle Controller 2 multiplexing.
    bus.getSIO0().getController().handleEvent(event);
}


void PlayStation::drainAudio(std::vector<int16_t>& output) {
    bus.getSPU().drainOutput(output);
}


DisplayFrame PlayStation::getDisplayFrame() {
    return videoOutput.capture(bus.getGPU());
}
