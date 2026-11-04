#include "input/ControllerInput.h"
#include <iostream>
#include <algorithm>

namespace {
// Returns -1, 0 or 1, with hysteresis between the press and release thresholds
int updateDirection(int current, Sint16 value, Sint16 press, Sint16 release) {
	if (current == 0) {
		if (value <= -press) return -1;
		if (value >= press) return 1;
		return 0;
	}
	if (current < 0) return value <= -release ? -1 : 0;
	return value >= release ? 1 : 0;
}

bool isDirection(NavInput input) {
	return input == NavInput::Up || input == NavInput::Down ||
	       input == NavInput::Left || input == NavInput::Right;
}

void readBattery(SDL_Gamepad* pad, int& percent, bool& charging) {
	int raw = -1;
	const SDL_PowerState state = SDL_GetGamepadPowerInfo(pad, &raw);

	if (state == SDL_POWERSTATE_NO_BATTERY || state == SDL_POWERSTATE_ERROR) {
		percent = -1;
		charging = false;
		return;
	}

	if (state == SDL_POWERSTATE_CHARGED && raw < 0) raw = 100; // full, but no number reported
	percent = raw;
	charging = (state == SDL_POWERSTATE_CHARGING);
}

} // namespace

ControllerInput::~ControllerInput() {
	close();
}

NavInput ControllerInput::handleEvent(const SDL_Event& event) {
	switch (event.type) {
	case SDL_EVENT_GAMEPAD_ADDED:
		open(event.gdevice.which);
		break;

	case SDL_EVENT_GAMEPAD_REMOVED:
		removePad(event.gdevice.which);
		break;

	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		if (gamepad && event.gbutton.which == gamepadId) {
			const NavInput input = handleButton(event.gbutton.button);
			return isDirection(input) ? press(input) : input;
		}
		break;

	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		if (gamepad && event.gbutton.which == gamepadId) {
			release(handleButton(event.gbutton.button));
		}
		break;

	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		if (gamepad && event.gaxis.which == gamepadId) return handleStickAxis(event.gaxis.axis, event.gaxis.value);
		break;
    
	default:
		break;
	}

	return NavInput::None;
}

// Closes every controller. Dashboard calls this before SDL_Quit().
void ControllerInput::close() {
	for (Pad& pad : pads) SDL_CloseGamepad(pad.handle);
	pads.clear();

	gamepad = nullptr;
	gamepadId = 0;
	stickX = stickY = 0;
	held = NavInput::None;
}

void ControllerInput::open(SDL_JoystickID id) {
	if (static_cast<int>(pads.size()) >= maxControllers) return;
	for (const Pad& pad : pads) if (pad.id == id) return; // already open

	SDL_Gamepad* handle = SDL_OpenGamepad(id);
	if (!handle) {
		std::cout << "Failed to open gamepad: " << SDL_GetError() << std::endl;
		return;
	}

	// Lowest slot not in use, so player 2 stays player 2 when player 1 unplugs
	int slot = 1;
	while (std::any_of(pads.begin(), pads.end(), [slot](const Pad& pad) { return pad.slot == slot; })) ++slot;

	pads.push_back({ handle, id, slot });

	const char* name = SDL_GetGamepadName(handle);
	std::cout << "DUALSHOCK: Controller Connected as player " << slot << " (" << (name ? name : "unknown") << ")" << std::endl;

	// The first controller to connect drives the menus
	if (!gamepad) {
		gamepad = handle;
		gamepadId = id;
		stickX = stickY = 0;
	}
}

NavInput ControllerInput::handleButton(Uint8 button) {
	switch (button) {
	case SDL_GAMEPAD_BUTTON_DPAD_UP:        return NavInput::Up;
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN:      return NavInput::Down;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT:      return NavInput::Left;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:     return NavInput::Right;
	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  return NavInput::JumpFirst;
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return NavInput::JumpLast;
	case SDL_GAMEPAD_BUTTON_NORTH:          return NavInput::Search;
	case SDL_GAMEPAD_BUTTON_SOUTH:          return NavInput::Select; // Cross on PlayStation, A on Xbox
	case SDL_GAMEPAD_BUTTON_EAST:           return NavInput::Back;    // Circle on PlayStation
	case SDL_GAMEPAD_BUTTON_START:          return NavInput::Options; // Options on PlayStation
	default:                                return NavInput::None;
	}
}

NavInput ControllerInput::handleStickAxis(Uint8 axis, Sint16 value) {
	if (axis == SDL_GAMEPAD_AXIS_LEFTX) {
		const int newX = updateDirection(stickX, value, stickPressThreshold, stickReleaseThreshold);
		if (newX != stickX) {
			const int oldX = stickX;
			stickX = newX;
			if (stickX < 0) return press(NavInput::Left);
			if (stickX > 0) return press(NavInput::Right);
			release(oldX < 0 ? NavInput::Left : NavInput::Right);
		}
	}
	else if (axis == SDL_GAMEPAD_AXIS_LEFTY) {
		const int newY = updateDirection(stickY, value, stickPressThreshold, stickReleaseThreshold);
		if (newY != stickY) {
			const int oldY = stickY;
			stickY = newY;
			if (stickY < 0) return press(NavInput::Up);     // SDL reports up as negative
			if (stickY > 0) return press(NavInput::Down);
			release(oldY < 0 ? NavInput::Up : NavInput::Down);
		}
	}

	return NavInput::None;
}


NavInput ControllerInput::press(NavInput direction) {
	held = direction;
	holdTimer = 0.0f;
	repeating = false;
	return direction;
}

void ControllerInput::release(NavInput direction) {
	if (direction != NavInput::None && direction == held) {
		held = NavInput::None;
		holdTimer = 0.0f;
		repeating = false;
	}
}

NavInput ControllerInput::update(float deltaSeconds) {
	if (held == NavInput::None) return NavInput::None;

	holdTimer += deltaSeconds;

	const float threshold = repeating ? repeatInterval : repeatDelay;
	if (holdTimer < threshold) return NavInput::None;

	// Keep the leftover time so the rhythm stays steady, but cap it so a very short interval can't build up a backlog
	holdTimer = std::min(holdTimer - threshold, repeatInterval);
	repeating = true;
	return held;
}

void ControllerInput::removePad(SDL_JoystickID id) {
	const auto it = std::find_if(pads.begin(), pads.end(), [id](const Pad& pad) { return pad.id == id; });
	if (it == pads.end()) return;

	std::cout << "DUALSHOCK: Controller Disconnected (player " << it->slot << ")" << std::endl;
	SDL_CloseGamepad(it->handle);

	const bool wasMenuPad = (it->id == gamepadId);
	pads.erase(it);

	if (wasMenuPad) {
		stickX = stickY = 0;
		held = NavInput::None;

		// Hand the menus to the next controller, if there is one
		gamepad = pads.empty() ? nullptr : pads.front().handle;
		gamepadId = pads.empty() ? 0 : pads.front().id;
	}
}

std::vector<ControllerInput::ControllerInfo> ControllerInput::getControllers() const {
	std::vector<ControllerInfo> controllers;
	controllers.reserve(pads.size());
	for (const Pad& pad : pads) {
		ControllerInfo info{ pad.slot, -1, false };
		readBattery(pad.handle, info.batteryPercent, info.charging);
		controllers.push_back(info);
	}

	std::sort(controllers.begin(), controllers.end(),
	          [](const ControllerInfo& a, const ControllerInfo& b) { return a.playerNumber < b.playerNumber; });
	return controllers;
}