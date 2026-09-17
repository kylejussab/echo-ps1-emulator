#include "DualShock.h"
#include <iostream>

DualShock::DualShock() {
    // Force SDL to use physical button positions
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
}


DualShock::~DualShock() {
    closeController();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}


void DualShock::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERDEVICEADDED) {
        if (!controller) {
            openController(event.cdevice.which);
        }
    } 
    else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
        if (joystick && event.cdevice.which == SDL_JoystickInstanceID(joystick)) {
            closeController();
        }
    }
}


void DualShock::updateState() {
    controllerState = 0xFFFF; // Reset to all unpressed
    rightX = 128; rightY = 128; leftX = 128; leftY = 128;

    if (!controller) return;

    // D-Pad and Center Buttons
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK)) controllerState &= ~(1 << 0);  // Select
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START)) controllerState &= ~(1 << 3);  // Start
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) controllerState &= ~(1 << 4);  // Up
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) controllerState &= ~(1 << 5);  // Right
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) controllerState &= ~(1 << 6);  // Down
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) controllerState &= ~(1 << 7);  // Left

    // Mode Toggle Logic (Select + Start for 2 Seconds)
    bool selectPressed = !(controllerState & (1 << 0));
    bool startPressed  = !(controllerState & (1 << 3));

    if (selectPressed && startPressed) {
        if (selectStartHoldStart == 0) {
            selectStartHoldStart = SDL_GetTicks(); // Start the timer
        } 
        else if (!toggleLocked) {
            if (SDL_GetTicks() - selectStartHoldStart >= 2000) {
                analogMode = !analogMode; // Toggle mode
                toggleLocked = true; // Lock until buttons are released
                std::cout << "DUALSHOCK: " << (analogMode ? "Analog Mode (0x73)" : "Digital Mode (0x41)") << std::endl;
            }
        }
    } 
    else {
        // Reset timer and unlock if either button is released
        selectStartHoldStart = 0;
        toggleLocked = false;
    }

    // Force sticks to center if analog mode is disabled, preventing ghost inputs
    if (!analogMode) {
        rightX = 128; rightY = 128; leftX = 128; leftY = 128;
    }

    // Face Buttons
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y)) controllerState &= ~(1 << 12); // Triangle
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B)) controllerState &= ~(1 << 13); // Circle
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A)) controllerState &= ~(1 << 14); // Cross
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X)) controllerState &= ~(1 << 15); // Square


    // L1 and R1 (Standard Bumpers)
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  controllerState &= ~(1 << 10); // L1
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) controllerState &= ~(1 << 11); // R1

    // L2 and R2 (Triggers) 
    // SDL axes range from -32768 to 32767. Triggers rest at 0 and go up to 32767. 
    // Set a threshold of 16000 (roughly half-pulled) to count as a digital press.
    if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000)  controllerState &= ~(1 << 8); // L2
    if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) controllerState &= ~(1 << 9); // R2

    // L3 and R3
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSTICK)) controllerState &= ~(1 << 1); // L3
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK))controllerState &= ~(1 << 2); // R3


    // Map Analog Axes (Only if Analog Mode is active)
    if (analogMode) {
        leftX  = (uint8_t)(((int32_t)SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) + 32768) / 256);
        leftY  = (uint8_t)(((int32_t)SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) + 32768) / 256);
        rightX = (uint8_t)(((int32_t)SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX) + 32768) / 256);
        rightY = (uint8_t)(((int32_t)SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY) + 32768) / 256);
    }
}



void DualShock::openController(int deviceIndex) {
    if (SDL_IsGameController(deviceIndex)) {
        controller = SDL_GameControllerOpen(deviceIndex);
        if (controller) {
            std::cout << "DUALSHOCK: Controller Connected" << std::endl;
        }
    }
}


void DualShock::closeController() {
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = nullptr;
        std::cout << "DUALSHOCK: Controller Disconnected" << std::endl;
    }
}
