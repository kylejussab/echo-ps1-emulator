#pragma once

#include "ui/Tile.h"

// A circular progress ring: a full track with an arc on top that fills clockwise from 12 o'clock.
// Both parts are rasterized from generated SVG (white, then tinted), so they stay sharp at any window size.
class RingTile : public Tile {
public:
	RingTile(float x, float y, SDL_GPUGraphicsPipeline* pipeline, SDL_GPUSampler* sampler);
	~RingTile() override;

	// Outside diameter and band thickness, in design units (call these first in a chain)
	RingTile* setRingSize(float diameter, float thickness);
	RingTile* setTrackColor(SDL_FColor color);
	RingTile* setProgressColor(SDL_FColor color); // a plain tint, so changing it never re-rasterizes

	// 0 to 1. The arc is only re-rasterized, on the next build(), if the value actually changed.
	RingTile* setProgress(float progress);

	void build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) override;
	void setRasterScale(float scale) override;
	void Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth = 1920.0f, float designHeight = 1080.0f) const override;

private:
	void releaseTexture(SDL_GPUTexture*& texture);

	SDL_GPUGraphicsPipeline* m_pipeline;
	SDL_GPUSampler* m_sampler;
	SDL_GPUDevice* m_device = nullptr;

	SDL_GPUTexture* m_trackTexture = nullptr;
	SDL_GPUTexture* m_arcTexture = nullptr;

	float m_diameter = 0.0f;
	float m_thickness = 0.0f;
	SDL_FColor m_trackColor    = { 0.63f, 0.63f, 0.63f, 1.0f };
	SDL_FColor m_progressColor = { 1.0f, 1.0f, 1.0f, 1.0f };

	float m_progress = 0.0f;
	float m_rasterScale = 1.0f;

	// What the current textures were made from
	float m_builtScale = 0.0f;
	float m_builtProgress = -1.0f;
};
