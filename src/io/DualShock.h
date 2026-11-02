#pragma once

#include <array>
#include <cstdint>
#include <SDL2/SDL.h>

/// @brief Emulates a PlayStation DualShock (SCPH-1200) controller on the Serial Input/Output 0 bus, fed by a host SDL2 gamepad.
/// The class manages both the host side and console side operations.
/// Host side operations include SDL gamepad hotplug detection, button and axis polling, and rumble motor output.
/// Console side operations include the controller's serial protocol, such as polling, configuration mode, mode lock, rumble mapping, and status queries. The Serial Input/Output feeds it one byte at a time via the transfer method.
class DualShock {
public:
    /// @brief Default constructor for the DualShock class.
    DualShock();


    /// @brief Destructor for the DualShock class.
    ~DualShock();


    /// @brief Deleted copy constructor to prevent copying of the controller instance.
    DualShock(const DualShock&) = delete;


    /// @brief Deleted copy assignment operator to prevent copying of the controller instance.
    DualShock& operator=(const DualShock&) = delete;


    /// @brief Processes raw Simple DirectMedia Layer events for gamepad hotplugging and Guide button presses.
    /// @param event The operating system event to process.
    void handleEvent(const SDL_Event& event);


    /// @brief Polls the active host controller and refreshes buttons, analog sticks, and the Select plus Start analog-toggle timer.
    void updateState();


    /// @brief Exchanges one byte with the controller. Call for every byte after the initial address byte.
    /// @param dataIn The byte the Central Processing Unit transmitted.
    /// @param dataOut The byte the controller replies with.
    /// @return True if the controller pulls Acknowledge after this byte, signaling an Interrupt Request.
    bool transfer(uint8_t dataIn, uint8_t& dataOut);


    /// @brief Aborts or restarts the current packet transfer. Call when the Central Processing Unit addresses the controller or when Data Terminal Ready is dropped.
    void resetTransfer();


    /// @brief Sets the analog or digital operating mode and determines if the analog toggle button is locked.
    /// @param enable True for Analog mode, false for Digital mode.
    /// @param lock True to lock the analog mode, preventing user changes.
    void setAnalogMode(bool enable, bool lock);


    /// @brief Toggles between Analog and Digital mode, simulating a press of the Analog button. Ignored if locked.
    void toggleAnalogMode();


    /// @brief Retrieves the 16-bit digital button mask where a zero bit represents a pressed button.
    /// @return The current controller button state.
    uint16_t getButtonState() const { return controllerButtonState; }


    /// @brief Retrieves the right analog stick horizontal axis value.
    /// @return The X axis value from 0 to 255.
    uint8_t getRightX() const { return rightX; }


    /// @brief Retrieves the right analog stick vertical axis value.
    /// @return The Y axis value from 0 to 255.
    uint8_t getRightY() const { return rightY; }


    /// @brief Retrieves the left analog stick horizontal axis value.
    /// @return The X axis value from 0 to 255.
    uint8_t getLeftX() const { return leftX; }


    /// @brief Retrieves the left analog stick vertical axis value.
    /// @return The Y axis value from 0 to 255.
    uint8_t getLeftY() const { return leftY; }


    /// @brief Checks if the controller is operating in Analog mode.
    /// @return True if in Analog mode, false if in Digital mode.
    bool isAnalogMode() const { return analogMode; }


    /// @brief Checks if the analog mode toggle is locked by the software.
    /// @return True if the analog mode cannot be changed by the user.
    bool isAnalogLocked() const { return analogLocked; }


    /// @brief Checks if the controller is currently in configuration mode.
    /// @return True if configuration mode is active.
    bool isConfigurationMode() const { return configurationMode; }


    /// @brief Enables or disables configuration mode.
    /// @param enable True to enter configuration mode, false to exit.
    void setConfigurationMode(bool enable) { configurationMode = enable; }


    /// @brief Checks if a physical host controller is currently connected and mapped.
    /// @return True if a host controller is present.
    bool hasHostController() const { return controller != nullptr; }


    /// @brief Retrieves the current target vibration intensity for the small motor.
    /// @return The small motor intensity requested by the game.
    uint8_t getSmallMotor() const { return smallMotor; }


    /// @brief Retrieves the current target vibration intensity for the large motor.
    /// @return The large motor intensity requested by the game.
    uint8_t getLargeMotor() const { return largeMotor; }

private:
    /// @brief Internal controller command bytes for the serial protocol.    
    enum Command : uint8_t {
        COMMAND_POLL           = 0x42, // Read buttons/sticks (+ motor values)
        COMMAND_CONFIGURE      = 0x43, // Enter/exit config (escape) mode
        COMMAND_SET_MODE       = 0x44, // Set analog/digital + lock (config mode only)
        COMMAND_GET_STATUS     = 0x45, // Get status (config mode only)
        COMMAND_GET_CONSTANT_1 = 0x46, // Get constant 1 (config mode only)
        COMMAND_GET_CONSTANT_2 = 0x47, // Get constant 2 (config mode only)
        COMMAND_GET_CONSTANT_3 = 0x4C, // Get constant 3 (config mode only)
        COMMAND_MAP_RUMBLE     = 0x4D, // Map TX bytes to motors (config mode only)
    };


    /// @brief Maps a physical SDL controller button to its specific bit in the active-low PS1 button state word.
    struct ButtonMap {
        SDL_GameControllerButton sdlButton;
        int bit;
    };


    static constexpr int kStickDeadzone = 4000;
    static constexpr int kTriggerThreshold = 16000;
    static constexpr uint32_t kSelectStartHoldMilliseconds = 2000;
    static constexpr uint32_t kRumbleDurationMilliseconds = 200;
    static constexpr uint32_t kRumbleRefreshMilliseconds  = 60;

    static constexpr ButtonMap kButtonMap[] = {
        {SDL_CONTROLLER_BUTTON_BACK,          0},  // Select
        {SDL_CONTROLLER_BUTTON_LEFTSTICK,     1},  // L3
        {SDL_CONTROLLER_BUTTON_RIGHTSTICK,    2},  // R3
        {SDL_CONTROLLER_BUTTON_START,         3},  // Start
        {SDL_CONTROLLER_BUTTON_DPAD_UP,       4},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    5},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN,     6},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT,     7},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  10}, // L1
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 11}, // R1
        {SDL_CONTROLLER_BUTTON_Y,             12}, // Triangle
        {SDL_CONTROLLER_BUTTON_B,             13}, // Circle
        {SDL_CONTROLLER_BUTTON_A,             14}, // Cross
        {SDL_CONTROLLER_BUTTON_X,             15}, // Square
    };

    
    /// @brief Converts a raw 16-bit SDL axis value to an 8-bit PS1 axis byte.
    /// @param raw The raw SDL axis value.
    /// @return The converted 8-bit value (0-255).
    static uint8_t axisToByte(int16_t raw);


    SDL_GameController* controller = nullptr;
    SDL_JoystickID instanceIdentifier = -1;

    /// @brief Opens and initializes a game controller by its device index.
    /// @param deviceIndex The system index of the controller to open.
    void openController(int deviceIndex);


    /// @brief Closes the currently active game controller and releases its resources.
    void closeController();


    /// @brief Applies the requested vibration intensities to the physical host controller.
    void applyRumble();


    // Pad state
    uint16_t controllerButtonState = 0xFFFF;
    uint8_t rightX = 128;
    uint8_t rightY = 128;
    uint8_t leftX = 128;
    uint8_t leftY = 128;

    bool analogMode = false; // Real controllers boot in Digital mode
    bool analogLocked = false;
    bool configurationMode = false;

    // Select + Start (2s) fallback for controllers without a Guide button
    uint32_t selectStartHoldStartTime = 0;
    bool toggleLocked = false;


    // Rumble
    std::array<uint8_t, 6> rumbleConfiguration = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Maps which motor each polling transmit byte drives (bytes 3 through 8). 0x00 is small, 0x01 is large, 0xFF is none.
    uint8_t smallMotor = 0;
    uint8_t largeMotor = 0;
    bool rumbleDirty = false;
    bool rumbleThisTransfer = false;
    bool rumbleWarned = false;
    uint16_t lastLow = 0;
    uint16_t lastHigh = 0;
    uint32_t lastRumbleTick = 0;


    // Protocol state
    int transferStep = 0;
    bool isTransferFinished = true;
    uint8_t command = 0;
    int responseLength = 0;
    std::array<uint8_t, 6> response = {};


    /// @brief Determines the appropriate identifier byte based on the current analog or digital mode.
    /// @return The controller identifier byte.
    uint8_t getIdentifierByte() const;


    /// @brief Checks if a specific command byte is supported by the controller implementation.
    /// @param command The command byte to verify.
    /// @return True if the command is supported.
    bool isCommandSupported(uint8_t cmd) const;


    /// @brief Populates the response buffer based on the requested command and current configuration.
    /// @param firstByte The initial data byte received after the command.
    void buildResponse(uint8_t firstByte);


    /// @brief Processes a single parameter byte received from the console during a configuration command.
    /// @param index The current parameter index.
    /// @param value The parameter byte value.
    void consumeByte(int index, uint8_t value);


    /// @brief Finalizes the current transfer packet and applies any pending state changes.
    void finishTransfer();
};