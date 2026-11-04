#pragma once

#include <memory>
#include "gfx/Renderer.h"
#include "library/GameLibrary.h"
#include "ui/TextTile.h"

// The message shown in place of the games when the library is empty: either the games folder
// couldn't be found, or it has no games in it. Shown on both tabs.
class EmptyState {
public:
	explicit EmptyState(Renderer& renderer) : m_renderer(renderer) {}

	// Shows the message if the library is empty, and nothing otherwise
	void build(const GameLibrary& library, float rasterScale);
	void clear();

	void update(float deltaSeconds);
	void draw(Renderer::Frame& frame);
	void setRasterScale(float rasterScale);

private:
	std::unique_ptr<TextTile> makeText(float x, float y, const std::string& text, const char* font, int fontSize,
	                                   SDL_GPUCommandBuffer* uploadCmd);

	Renderer& m_renderer;
	float m_rasterScale = 1.0f;
	std::unique_ptr<TextTile> m_message;
	std::unique_ptr<TextTile> m_detail;
};