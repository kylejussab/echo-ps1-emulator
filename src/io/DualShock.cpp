#include <iostream>
#include "DualShock.h"


DualShock::DualShock() {
    // Force SDL to use physical button positions
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "0");
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_GameControllerAddMappingsFromFile("assets/gamecontrollerdb.txt");
}


DualShock::~DualShock() {
    closeController();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}


void DualShock::handleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_CONTROLLERDEVICEADDED:{
            if (!controller) openController(event.cdevice.which);
            break;
        }
        case SDL_CONTROLLERDEVICEREMOVED: {
            if (controller && event.cdevice.which == instanceIdentifier) {
                closeController();
                // Fall back to any other pad that is still plugged in
                for (int i = 0; i < SDL_NumJoysticks() && !controller; ++i) openController(i);
            }
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN: {
            // The Guide/PS button acts as the DualShock's Analog button
            if (controller && event.cbutton.which == instanceIdentifier && event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
                toggleAnalogMode();
            }
            break;

        }
        default: {
            break;
        }
    }
}


void DualShock::updateState() {
    controllerButtonState = 0xFFFF; // All unpressed
    rightX = rightY = leftX = leftY = 128;

    if (!controller) {
        selectStartHoldStartTime = 0;
        toggleLocked = false;
        return;
    }
    
    // Digital buttons
    for (const ButtonMap& m : kButtonMap) {
        if (SDL_GameControllerGetButton(controller, m.sdlButton)) {
            controllerButtonState &= static_cast<uint16_t>(~(1u << m.bit));
        }
    }

    // L2/R2: SDL triggers rest at 0 and go up to 32767; treat ~half-pull as a press
    if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > kTriggerThreshold) controllerButtonState &= ~(1u << 8);
    if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > kTriggerThreshold) controllerButtonState &= ~(1u << 9);

    // Sticks are always read; transfer() decides whether the game gets to see them
    leftX  = axisToByte(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX));
    leftY  = axisToByte(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
    rightX = axisToByte(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX));
    rightY = axisToByte(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY));

    // Fallback Analog toggle: hold Select + Start for 2 seconds
    const bool selectPressed = !(controllerButtonState & (1u << 0));
    const bool startPressed  = !(controllerButtonState & (1u << 3));

    if (selectPressed && startPressed) {
        if (selectStartHoldStartTime == 0) {
            selectStartHoldStartTime = SDL_GetTicks();
        } 
        else if (!toggleLocked && SDL_GetTicks() - selectStartHoldStartTime >= kSelectStartHoldMilliseconds) {
            toggleAnalogMode();
            toggleLocked = true; // Once per hold
        }
    } 
    else {
        selectStartHoldStartTime = 0;
        toggleLocked = false;
    }
}


bool DualShock::transfer(uint8_t dataIn, uint8_t& dataOut) {
    if (isTransferFinished) {
        dataOut = 0xFF;
        return false;
    }

    if (transferStep == 0) {
        command = dataIn;
        dataOut = getIdentifierByte();

        if (!isCommandSupported(command)) {
            isTransferFinished = true; // Unknown command: reply with ID, then go silent (no ACK)
            return false;
        }

        if (command == COMMAND_POLL || command == COMMAND_CONFIGURE) {
            // Digital: 2 bytes. Analog or config mode: 6 bytes.
            responseLength = (configurationMode || analogMode) ? 6 : 2;
        } 
        else {
            responseLength = 6; // All config-mode commands reply with 6 bytes
        }

        // Motor bytes in a poll are ignored while in config mode
        rumbleThisTransfer = (command == COMMAND_POLL && !configurationMode);

        transferStep = 1;
        return true;
    }

    if (transferStep == 1) {
        dataOut = 0x5A;
        transferStep = 2;
        return true;
    }

    const int index = transferStep - 2;
    if (index == 0) buildResponse(dataIn); // dataIn here is the first command parameter

    dataOut = response[index];
    consumeByte(index, dataIn);
    ++transferStep;

    if (index + 1 < responseLength) return true;

    finishTransfer();
    return false; // No ACK on the final byte
}


void DualShock::resetTransfer() {
    if (rumbleDirty) applyRumble();
    transferStep = 0;
    isTransferFinished = false;
    command = 0;
    responseLength = 0;
    rumbleThisTransfer = false;
}


void DualShock::setAnalogMode(bool enable, bool lock) {
    const bool changed = (analogMode != enable);
    analogMode = enable;
    analogLocked = lock;
    if (changed) {
        std::cout << "DUALSHOCK: Mode switched to " << (enable ? "Analog (0x73)" : "Digital (0x41)") << std::endl;
    }
}


void DualShock::toggleAnalogMode() {
    if (analogLocked) {
        std::cout << "DUALSHOCK: Analog button ignored (mode locked by game)" << std::endl;
        return;
    }
    setAnalogMode(!analogMode, false);
}



// Privates
uint8_t DualShock::axisToByte(int16_t raw) {
    int v = raw;
    if (v > -kStickDeadzone && v < kStickDeadzone) v = 0;
    return static_cast<uint8_t>((v + 32768) >> 8); // -32768..32767 -> 0..255, centre = 128
}


void DualShock::openController(int deviceIndex) {
    if (!SDL_IsGameController(deviceIndex)) return;

    controller = SDL_GameControllerOpen(deviceIndex);
    if (!controller) return;

    instanceIdentifier = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
    lastLow = lastHigh = 0;
    rumbleWarned = false;

    const char* name = SDL_GameControllerName(controller);
    std::cout << "DUALSHOCK: Controller Connected (" << (name ? name : "unknown") << ")" << std::endl;
}


void DualShock::closeController() {
    if (controller) {
        SDL_GameControllerRumble(controller, 0, 0, 0);
        SDL_GameControllerClose(controller);
        controller = nullptr;
        instanceIdentifier = -1;
        std::cout << "DUALSHOCK: Controller Disconnected" << std::endl;
    }
}


void DualShock::applyRumble() {
    rumbleDirty = false;
    if (!controller) return;

    // Large motor -> SDL low-frequency (heavy), small motor -> SDL high-frequency (light)
    const uint16_t low  = static_cast<uint16_t>(largeMotor * 257);
    const uint16_t high = static_cast<uint16_t>(smallMotor * 257);
    const bool active  = (low | high) != 0;
    const bool changed = (low != lastLow) || (high != lastHigh);
    const uint32_t now = SDL_GetTicks();

    if (!changed && !(active && now - lastRumbleTick >= kRumbleRefreshMilliseconds)) return;

    if (SDL_GameControllerRumble(controller, low, high, active ? kRumbleDurationMilliseconds : 0) != 0) {
        if (!rumbleWarned) {
            std::cout << "DUALSHOCK: Rumble failed: " << SDL_GetError() << std::endl;
            rumbleWarned = true;
        }
    }
    lastLow = low;
    lastHigh = high;
    lastRumbleTick = now;
}


uint8_t DualShock::getIdentifierByte() const {
    // High nibble = type (4 digital, 7 analog, F config), low nibble = halfwords of data
    if (configurationMode) return 0xF3;
    
    return analogMode ? 0x73 : 0x41;
}


bool DualShock::isCommandSupported(uint8_t command) const {
    switch (command) {
    case COMMAND_POLL:
    case COMMAND_CONFIGURE:
        return true;
    case COMMAND_SET_MODE:
    case COMMAND_GET_STATUS:
    case COMMAND_GET_CONSTANT_1:
    case COMMAND_GET_CONSTANT_2:
    case COMMAND_GET_CONSTANT_3:
    case COMMAND_MAP_RUMBLE:
        return configurationMode;
    default:
        return false;
    }
}


void DualShock::buildResponse(uint8_t firstByte) {
    response.fill(0);

    switch (command) {
    case COMMAND_POLL:
    case COMMAND_CONFIGURE:
        updateState(); // Live read of the host controller exactly when the game asks
        response[0] = controllerButtonState & 0xFF;
        response[1] = (controllerButtonState >> 8) & 0xFF;
        response[2] = rightX;
        response[3] = rightY;
        response[4] = leftX;
        response[5] = leftY;
        break;

    case COMMAND_SET_MODE:
        break; // Replies with zeros

    case COMMAND_GET_STATUS:
        // model (3 = DualShock), 2, LED (analog on/off), 2, 1, 0
        response = {0x03, 0x02, static_cast<uint8_t>(analogMode ? 1 : 0), 0x02, 0x01, 0x00};
        break;

    case COMMAND_GET_CONSTANT_1:
        if (firstByte == 0) response = {0x00, 0x00, 0x01, 0x02, 0x00, 0x0A};
        else                response = {0x00, 0x00, 0x01, 0x01, 0x01, 0x14};
        break;

    case COMMAND_GET_CONSTANT_2:
        response = {0x00, 0x00, 0x02, 0x00, 0x01, 0x00};
        break;

    case COMMAND_GET_CONSTANT_3:
        response = {0x00, 0x00, 0x00, static_cast<uint8_t>(firstByte == 0 ? 0x04 : 0x07), 0x00, 0x00};
        break;

    case COMMAND_MAP_RUMBLE:
        response = rumbleConfiguration; // Controller reports the PREVIOUS mapping
        break;

    default:
        break;
    }
}


void DualShock::consumeByte(int index, uint8_t value) {
    switch (command) {
        case COMMAND_POLL: {
            if (rumbleThisTransfer) {
                const uint8_t map = rumbleConfiguration[index];
                if (map == 0x00) {
                    smallMotor = (value & 0x01) ? 0xFF : 0x00; // Small motor is on/off (bit 0)
                    rumbleDirty = true;
                } 
                else if (map == 0x01) {
                    largeMotor = value; // Large motor is 0-255
                    rumbleDirty = true;
                }
            }
            break;
        }
        case COMMAND_CONFIGURE: {
            if (index == 0) {
                const bool enter = (value == 0x01);
                if (enter != configurationMode) {
                    std::cout << "DUALSHOCK: Config mode " << (enter ? "entered" : "exited") << std::endl;
                }
                configurationMode = enter;
            }
            break;
        }
        case COMMAND_SET_MODE: {
            if (index == 0) setAnalogMode(value == 0x01, analogLocked); // 0 = digital, 1 = analog
            else if (index == 1) analogLocked = (value == 0x03); // 3 = lock Analog button
            break;
        }
        case COMMAND_MAP_RUMBLE: {
            rumbleConfiguration[index] = value;
            break;
        }
        default: {
            break;
        } 
    }
}


void DualShock::finishTransfer() {
    if (command == COMMAND_MAP_RUMBLE) {
        // If a motor is no longer mapped to any byte, it must stop
        bool smallMapped = false, largeMapped = false;
        for (uint8_t m : rumbleConfiguration) {
            if (m == 0x00) smallMapped = true;
            if (m == 0x01) largeMapped = true;
        }
        if (!smallMapped) smallMotor = 0;
        if (!largeMapped) largeMotor = 0;
        rumbleDirty = true;
    }

    isTransferFinished = true;
    if (rumbleDirty) applyRumble();
}
