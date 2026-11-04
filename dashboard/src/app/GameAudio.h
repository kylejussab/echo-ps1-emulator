#pragma once

#include <cstdint>
#include <vector>
#include <SDL3/SDL.h>

// The running game's sound: one SDL audio stream on the default output device.
// It is opened when a game starts, paused while the game idles behind the dashboard, and closed when the game ends.
class GameAudio {
public:
	GameAudio() = default;
	~GameAudio();
	GameAudio(const GameAudio&) = delete;
	GameAudio& operator=(const GameAudio&) = delete;

	// Returns false if there is no audio output. The game can still run without sound.
	bool open();

	// Releases the device. Safe to call twice. Must run before SDL_Quit.
	void close();

	void pause();
	void resume();

	// Queues one frame of interleaved stereo 16-bit samples at 44100 Hz, then holds the caller back until the queue is down to about 50 ms.
	// This is what keeps the emulator running at the right speed: the sound card's clock is the pacing.
	void queue(const std::vector<int16_t>& samples);

private:
	SDL_AudioStream* m_stream = nullptr;
};
