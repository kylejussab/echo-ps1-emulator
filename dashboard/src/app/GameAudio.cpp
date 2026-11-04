#include "app/GameAudio.h"
#include <iostream>

GameAudio::~GameAudio() {
	close();
}


bool GameAudio::open() {
	if (m_stream) return true;

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		std::cout << "Echo: Could not start SDL audio: " << SDL_GetError() << std::endl;
		return false;
	}

	// 1024 frame device buffer. Has to be set before the device opens.
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");

	SDL_AudioSpec desired = {};
	desired.freq = 44100;
	desired.format = SDL_AUDIO_S16;
	desired.channels = 2;
	m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, nullptr, nullptr);

	if (!m_stream) {
		std::cout << "Echo: Could not open the audio device: " << SDL_GetError() << std::endl;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	// Streams start paused
	SDL_ResumeAudioStreamDevice(m_stream);
	return true;
}


void GameAudio::close() {
	if (!m_stream) return;

	SDL_DestroyAudioStream(m_stream); // Also closes the audio device it opened
	m_stream = nullptr;
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}


void GameAudio::pause() {
	if (m_stream) SDL_PauseAudioStreamDevice(m_stream);
}


void GameAudio::resume() {
	if (m_stream) SDL_ResumeAudioStreamDevice(m_stream);
}


void GameAudio::queue(const std::vector<int16_t>& samples) {
	if (!m_stream || samples.empty()) return;

	SDL_PutAudioStreamData(m_stream, samples.data(), static_cast<int>(samples.size() * sizeof(int16_t)));

	// Hold the emulator back until the queue is down to about 50 ms, so audio and video stay in step.
	// The deadline means a stalled audio device (unplugged, asleep) can never freeze the app.
	constexpr int TARGET_QUEUED_BYTES = 44100 * 4 / 20;
	const Uint64 deadline = SDL_GetTicks() + 100;
	while (SDL_GetAudioStreamQueued(m_stream) > TARGET_QUEUED_BYTES && SDL_GetTicks() < deadline) {
		SDL_Delay(1);
	}
}
