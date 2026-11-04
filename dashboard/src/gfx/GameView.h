#pragma once

#include <SDL3/SDL.h>
#include "core/VideoOutput.h"

class Renderer;

// Shows the emulator's picture. Each frame the visible part of the emulator's display is converted to RGBA8, uploaded to a
// GPU texture, and drawn as one rectangle through the Renderer's game pipeline.
class GameView {
public:
	explicit GameView(Renderer& renderer) : m_renderer(renderer) {}
	~GameView() { release(); }
	GameView(const GameView&) = delete;
	GameView& operator=(const GameView&) = delete;

	// Creates the texture and upload buffer. The Renderer must already be initialized.
	bool build();

	// Frees the GPU resources. Must run before the Renderer shuts down. Safe to call twice.
	void release();

	// Call once per frame, BEFORE Renderer::beginFrame (a copy pass can't happen inside a render pass).
	// Copies what it needs out of the frame, so the frame's pixel pointer isn't kept.
	void upload(const DisplayFrame& frame);

	// Draws the last uploaded picture. Called through Renderer::Frame::draw(GameView&).
	void draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass);

private:
	Renderer& m_renderer;
	SDL_GPUTexture* m_texture = nullptr;
	SDL_GPUTransferBuffer* m_transferBuffer = nullptr;

	bool m_hasPicture = false;
	float m_rect[4] = {};    // left, top, right, bottom in clip space
	float m_uvRect[4] = {};  // u0, v0, u1, v1
};
