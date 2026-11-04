#include "ui/IconTile.h"
#include "gfx/GPUTexture.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <iostream>

IconTile::IconTile(float x, float y, SDL_GPUGraphicsPipeline* pipeline, SDL_GPUSampler* sampler)
	: Tile(x, y, 0, 0), m_pipeline(pipeline), m_sampler(sampler) {}

IconTile::~IconTile() {
	releaseTexture();
}

void IconTile::releaseTexture() {
	if (m_texture && m_device) {
		SDL_ReleaseGPUTexture(m_device, m_texture);
	}
	m_texture = nullptr;
}

// --- Chainable setters ---
IconTile* IconTile::setSvg(const std::string& svgPath) { m_svgPath = svgPath; return this; }
IconTile* IconTile::setIconSize(float iconWidth, float iconHeight) { m_iconWidth = iconWidth; m_iconHeight = iconHeight; return this; }
IconTile* IconTile::setIconColor(SDL_FColor iconColor) { m_iconColor = iconColor; return this; }

void IconTile::setRasterScale(float scale) {
	m_rasterScale = scale > 0.0f ? scale : 1.0f;
}

void IconTile::build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) {
	if (m_svgPath.empty() || m_iconWidth <= 0.0f || m_iconHeight <= 0.0f) return;

	// Safe to call build() more than once without leaking the old texture
	releaseTexture();

	const char* basePath = SDL_GetBasePath();
	if (!basePath) return;

	const std::string fullPath = std::string(basePath) + m_svgPath;
	SDL_IOStream* io = SDL_IOFromFile(fullPath.c_str(), "rb");
	if (!io) {
		std::cout << "Failed to open icon " << m_svgPath << ": " << SDL_GetError() << std::endl;
		return;
	}

	// Rasterize at the icon's on-screen pixel size so it stays sharp at any window size
	const int pixelW = std::max(1, static_cast<int>(std::lround(m_iconWidth * m_rasterScale)));
	const int pixelH = std::max(1, static_cast<int>(std::lround(m_iconHeight * m_rasterScale)));

	SDL_Surface* rawSurface = IMG_LoadSizedSVG_IO(io, pixelW, pixelH);
	SDL_CloseIO(io); // the loader doesn't close the stream for us
	if (!rawSurface) {
		std::cout << "Failed to rasterize " << m_svgPath << ": " << SDL_GetError() << std::endl;
		return;
	}

	SDL_Surface* surface = SDL_ConvertSurface(rawSurface, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(rawSurface);
	if (!surface) {
		std::cout << "SDL_ConvertSurface failed: " << SDL_GetError() << std::endl;
		return;
	}

	SDL_GPUTexture* texture = createTextureFromSurface(gpuDevice, uploadCmd, surface);
	SDL_DestroySurface(surface);
	if (!texture) return;

	// The bounding rectangle wraps the icon, the same way TextTile wraps its text
	width = m_iconWidth + paddingLeft + paddingRight;
	height = m_iconHeight + paddingTop + paddingBottom;

	m_device = gpuDevice;
	m_texture = texture;
}

void IconTile::Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth, float designHeight) const {
	// 1. Background, border and focus ring
	Tile::Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);

	if (!m_texture || !m_pipeline || !m_sampler) return;

	// 2. Same pipeline and shader as text
	SDL_BindGPUGraphicsPipeline(renderPass, m_pipeline);

	SDL_GPUTextureSamplerBinding samplerBinding{};
	samplerBinding.texture = m_texture;
	samplerBinding.sampler = m_sampler;
	SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);

	struct TintUniforms { float tint[4]; float opacity; float pad[3]; };
	TintUniforms fragUniforms{ { m_iconColor.r, m_iconColor.g, m_iconColor.b, m_iconColor.a }, drawOpacity, {} };
	SDL_PushGPUFragmentUniformData(cmdBuffer, 0, &fragUniforms, sizeof(fragUniforms));

	struct TextureUniforms { float x, y, w, h, viewportW, viewportH; };
	TextureUniforms uniforms{};
	uniforms.x = std::round((x + paddingLeft) * pixelsPerUnit) / pixelsPerUnit;
	uniforms.y = std::round((y + paddingTop) * pixelsPerUnit) / pixelsPerUnit;
	uniforms.w = m_iconWidth;
	uniforms.h = m_iconHeight;
	uniforms.viewportW = designWidth;
	uniforms.viewportH = designHeight;
	SDL_PushGPUVertexUniformData(cmdBuffer, 0, &uniforms, sizeof(uniforms));

	SDL_DrawGPUPrimitives(renderPass, 6, 1, 0, 0);
}