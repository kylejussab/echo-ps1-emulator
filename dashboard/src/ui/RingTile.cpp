#include "ui/RingTile.h"
#include "gfx/GPUTexture.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

namespace {

constexpr float pi = 3.14159265358979f;

std::string trackSvg(float diameter, float thickness) {
	const float c = diameter / 2.0f;
	const float r = c - thickness / 2.0f;

	char buffer[512];
	std::snprintf(buffer, sizeof(buffer),
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.3f\" height=\"%.3f\" viewBox=\"0 0 %.3f %.3f\">"
		"<circle cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\" fill=\"none\" stroke=\"#ffffff\" stroke-width=\"%.3f\"/>"
		"</svg>",
		diameter, diameter, diameter, diameter, c, c, r, thickness);
	return buffer;
}

// An arc that starts at 12 o'clock and runs clockwise, with rounded ends
std::string arcSvg(float diameter, float thickness, float progress) {
	// A full circle can't be drawn as one arc (start and end are the same point)
	if (progress >= 0.9999f) return trackSvg(diameter, thickness);

	const float c = diameter / 2.0f;
	const float r = c - thickness / 2.0f;
	const float angle = progress * 2.0f * pi;
	const float endX = c + r * std::sin(angle);
	const float endY = c - r * std::cos(angle);
	const int largeArc = angle > pi ? 1 : 0;

	char buffer[640];
	std::snprintf(buffer, sizeof(buffer),
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.3f\" height=\"%.3f\" viewBox=\"0 0 %.3f %.3f\">"
		"<path d=\"M %.3f %.3f A %.3f %.3f 0 %d 1 %.3f %.3f\" fill=\"none\" stroke=\"#ffffff\" stroke-width=\"%.3f\" stroke-linecap=\"round\"/>"
		"</svg>",
		diameter, diameter, diameter, diameter,
		c, c - r, r, r, largeArc, endX, endY, thickness);
	return buffer;
}

SDL_GPUTexture* textureFromSvg(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd, const std::string& svg, int pixelSize) {
	SDL_IOStream* io = SDL_IOFromConstMem(svg.data(), svg.size());
	if (!io) return nullptr;

	SDL_Surface* rawSurface = IMG_LoadSizedSVG_IO(io, pixelSize, pixelSize);
	SDL_CloseIO(io); // the loader doesn't close the stream for us
	if (!rawSurface) {
		std::cout << "Failed to rasterize ring: " << SDL_GetError() << std::endl;
		return nullptr;
	}

	SDL_Surface* surface = SDL_ConvertSurface(rawSurface, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(rawSurface);
	if (!surface) return nullptr;

	SDL_GPUTexture* texture = createTextureFromSurface(gpuDevice, uploadCmd, surface);
	SDL_DestroySurface(surface);
	return texture;
}

} // namespace


// Transparent background, so it never covers what is behind it
RingTile::RingTile(float x, float y, SDL_GPUGraphicsPipeline* pipeline, SDL_GPUSampler* sampler)
	: Tile(x, y, 0, 0, SDL_FColor{ 0.0f, 0.0f, 0.0f, 0.0f }), m_pipeline(pipeline), m_sampler(sampler) {}

RingTile::~RingTile() {
	releaseTexture(m_trackTexture);
	releaseTexture(m_arcTexture);
}

void RingTile::releaseTexture(SDL_GPUTexture*& texture) {
	if (texture && m_device) SDL_ReleaseGPUTexture(m_device, texture);
	texture = nullptr;
}

// --- Chainable setters ---
RingTile* RingTile::setRingSize(float diameter, float thickness) {
	m_diameter = diameter;
	m_thickness = thickness;
	width = height = diameter; // the tile's bounds are the ring's bounds
	return this;
}

RingTile* RingTile::setTrackColor(SDL_FColor color) { m_trackColor = color; return this; }
RingTile* RingTile::setProgressColor(SDL_FColor color) { m_progressColor = color; return this; }
RingTile* RingTile::setProgress(float progress) { m_progress = std::clamp(progress, 0.0f, 1.0f); return this; }

void RingTile::setRasterScale(float scale) {
	m_rasterScale = scale > 0.0f ? scale : 1.0f;
}

void RingTile::build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) {
	if (m_diameter <= 0.0f || m_thickness <= 0.0f) return;
	m_device = gpuDevice;

	const int pixelSize = std::max(1, static_cast<int>(std::lround(m_diameter * m_rasterScale)));
	const bool scaleChanged = (m_builtScale != m_rasterScale);

	// The track only depends on the size, so it is only redone when the window scale changes
	if (!m_trackTexture || scaleChanged) {
		releaseTexture(m_trackTexture);
		m_trackTexture = textureFromSvg(gpuDevice, uploadCmd, trackSvg(m_diameter, m_thickness), pixelSize);
	}

	if (m_progress <= 0.0f) {
		releaseTexture(m_arcTexture); // nothing to draw
	}
	else if (!m_arcTexture || scaleChanged || m_builtProgress != m_progress) {
		releaseTexture(m_arcTexture);
		m_arcTexture = textureFromSvg(gpuDevice, uploadCmd, arcSvg(m_diameter, m_thickness, m_progress), pixelSize);
	}

	m_builtScale = m_rasterScale;
	m_builtProgress = m_progress;
}

void RingTile::Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth, float designHeight) const {
	Tile::Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);

	if (!m_pipeline || !m_sampler) return;
	if (!m_trackTexture && !m_arcTexture) return;

	// Same pipeline and shader as text and icons
	SDL_BindGPUGraphicsPipeline(renderPass, m_pipeline);

	struct TintUniforms { float tint[4]; float opacity; float pad[3]; };
	struct TextureUniforms { float x, y, w, h, viewportW, viewportH; };

	TextureUniforms vertexUniforms{};
	vertexUniforms.x = std::round(x * pixelsPerUnit) / pixelsPerUnit;
	vertexUniforms.y = std::round(y * pixelsPerUnit) / pixelsPerUnit;
	vertexUniforms.w = m_diameter;
	vertexUniforms.h = m_diameter;
	vertexUniforms.viewportW = designWidth;
	vertexUniforms.viewportH = designHeight;

	// Track first, arc on top
	const auto drawLayer = [&](SDL_GPUTexture* texture, SDL_FColor tint) {
		if (!texture) return;

		SDL_GPUTextureSamplerBinding samplerBinding{};
		samplerBinding.texture = texture;
		samplerBinding.sampler = m_sampler;
		SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);

		TintUniforms fragUniforms{ { tint.r, tint.g, tint.b, tint.a }, drawOpacity, {} };
		SDL_PushGPUFragmentUniformData(cmdBuffer, 0, &fragUniforms, sizeof(fragUniforms));
		SDL_PushGPUVertexUniformData(cmdBuffer, 0, &vertexUniforms, sizeof(vertexUniforms));

		SDL_DrawGPUPrimitives(renderPass, 6, 1, 0, 0);
	};

	drawLayer(m_trackTexture, m_trackColor);
	drawLayer(m_arcTexture, m_progressColor);
}
