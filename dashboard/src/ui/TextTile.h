#pragma once

#include "ui/Tile.h"
#include <string>
#include <SDL3_ttf/SDL_ttf.h>

class TextTile : public Tile {
public:
	TextTile(float x, float y, SDL_GPUGraphicsPipeline* textPipeline, SDL_GPUSampler* sampler);
	~TextTile() override;

	// Text-specific setters (call these first in a chain)
	TextTile* setText(const std::string& text);
	TextTile* setFont(const std::string& fontPath, int size);
    

	void build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) override;

	void setRasterScale(float scale) override;

	void Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth = 1920.0f, float designHeight = 1080.0f) const override;

	void fadeTo(float target, float durationSeconds);
	void setOpacity(float opacity);
	float getOpacity() const { return m_opacity; }

	void update(float deltaSeconds) override;

	// Makes the tile (and its background) at least this wide, in design units. The text stays left aligned. 0 means no minimum.
	TextTile* setMinWidth(float minWidth);

	// Longest the text may be, in design units. Anything longer is cut with an ellipsis. 0 means no limit.
	TextTile* setMaxWidth(float maxWidth);

private:
	float m_maxWidth = 0.0f;
	float m_minWidth = 0.0f;

	std::string m_text;
	std::string m_fontPath;
	int m_fontSize;

	SDL_GPUTexture* m_textTexture;
	SDL_GPUGraphicsPipeline* m_textPipeline;
	SDL_GPUSampler* m_sampler;

	float m_textWidth;
	float m_textHeight;
	float m_rasterScale = 1.0f;

    void releaseTexture();
	SDL_GPUDevice* m_device = nullptr;

	float m_opacity = 1.0f;
	float m_targetOpacity = 1.0f;
	float m_opacityRate = 0.0f; // opacity units per second
};
