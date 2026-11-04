#pragma once

#include "ui/Tile.h"
#include <string>

class IconTile : public Tile {
public:
	IconTile(float x, float y, SDL_GPUGraphicsPipeline* pipeline, SDL_GPUSampler* sampler);
	~IconTile() override;

	// Icon-specific setters (call these first in a chain)
	IconTile* setSvg(const std::string& svgPath);               // relative to the app's base path, like fonts
	IconTile* setIconSize(float iconWidth, float iconHeight);   // in design units, excluding padding
	IconTile* setIconColor(SDL_FColor iconColor);

	void build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) override;

    void setRasterScale(float scale) override;

	void Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth = 1920.0f, float designHeight = 1080.0f) const override;

private:
	void releaseTexture();

	std::string m_svgPath;
	float m_iconWidth = 24.0f;
	float m_iconHeight = 24.0f;
	SDL_FColor m_iconColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	float m_rasterScale = 1.0f;

	SDL_GPUTexture* m_texture = nullptr;
	SDL_GPUGraphicsPipeline* m_pipeline;
	SDL_GPUSampler* m_sampler;
	SDL_GPUDevice* m_device = nullptr;
};