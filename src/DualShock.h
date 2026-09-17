#pragma once
#include <SDL2/SDL.h>
#include <cstdint>

/// @brief Interfaces with host OS gamepads via SDL2 and translates inputs into PS1 hardware formats.
class DualShock {
public:
    DualShock();
    ~DualShock();


    /// @brief Processes raw SDL OS events for controller hotplugging.
    /// @param event The SDL_Event polled from the main application loop.
    void handleEvent(const SDL_Event& event);
    
    /// @brief Polls the active SDL controller and updates the internal bitmasks and analog axes.
    void updateState();


    /// @brief Gets the current 16-bit digital button mask (active-low).
    /// @return 0xFFFF if nothing is pressed. Bits are cleared to 0 when buttons are held.
    uint16_t getButtons() const { return controllerState; }


    uint8_t getRightX() const { return rightX; }
    uint8_t getRightY() const { return rightY; }
    uint8_t getLeftX() const { return leftX; }
    uint8_t getLeftY() const { return leftY; }
    

    /// @brief Checks if the controller is currently outputting analog axis data.
    /// @return True if in Analog mode (0x73), false if in Digital mode (0x41).
    bool isAnalogMode() const { return analogMode; }

private:
    SDL_GameController* controller = nullptr;

    // Core Controller State
    uint16_t controllerState = 0xFFFF;
    
    uint8_t rightX = 128;
    uint8_t rightY = 128;
    uint8_t leftX = 128;
    uint8_t leftY = 128;

    // Mode Toggle State
    bool analogMode = false; // Boot in Digital Mode (0x41) for maximum compatibility
    uint32_t selectStartHoldStart = 0;
    bool toggleLocked = false;

    // Internal Helpers
    void openController(int deviceIndex);

    void closeController();

    // TODO: Add motor speed variables and configure SDL_GameControllerRumble for SPU/Vibration pass
};
