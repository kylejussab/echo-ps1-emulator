#pragma once

#include <SDL3/SDL.h>
#include <vector>

enum class NavInput { None, Up, Down, Left, Right, JumpFirst, JumpLast, Search, Select, Back, Options };

class ControllerInput {
public:
	ControllerInput() = default;
	~ControllerInput();

	ControllerInput(const ControllerInput&) = delete;
	ControllerInput& operator=(const ControllerInput&) = delete;

	// Feed every SDL event to this. Returns a direction when the d-pad or left stick was just pressed.
	NavInput handleEvent(const SDL_Event& event);

	// Safe to call more than once. Dashboard calls it before SDL_Quit().
	void close();


    // Call once per frame. Returns a direction each time a held d-pad/stick direction repeats.
	NavInput update(float deltaSeconds);

	// delay: how long you hold before it starts repeating. interval: time between repeats after that.
	void setRepeat(float delaySeconds, float intervalSeconds) {
		repeatDelay = delaySeconds;
		repeatInterval = intervalSeconds;
	}

	static constexpr int maxControllers = 8;

	struct ControllerInfo {
		int playerNumber;    // 1 to 8. Stays the same until that controller unplugs.
		int batteryPercent;  // 0 to 100, or -1 when there is no battery or it can't be read
		bool charging;
	};

	// Every connected controller, in player number order
	std::vector<ControllerInfo> getControllers() const;

private:
	void open(SDL_JoystickID id);
	NavInput handleButton(Uint8 button);
	NavInput handleStickAxis(Uint8 axis, Sint16 value);

	SDL_Gamepad* gamepad = nullptr;
	SDL_JoystickID gamepadId = 0;

	static constexpr Sint16 stickPressThreshold = 16000;
	static constexpr Sint16 stickReleaseThreshold = 10000;

	int stickX = 0; // -1 left, 0 neutral, 1 right
	int stickY = 0; // -1 up,   0 neutral, 1 down


    NavInput press(NavInput direction);
	void release(NavInput direction);

	NavInput held = NavInput::None;
	float holdTimer = 0.0f;
	bool repeating = false;

	float repeatDelay = 0.35f;
	float repeatInterval = 0.10f;


	void removePad(SDL_JoystickID id);

	struct Pad {
		SDL_Gamepad* handle;
		SDL_JoystickID id;
		int slot;
	};
	std::vector<Pad> pads;   // in the order they connected. The first one drives the menus.
};