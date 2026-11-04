#include "ui/TextTile.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <cstring>

TextTile::TextTile(float x, float y, SDL_GPUGraphicsPipeline* textPipeline, SDL_GPUSampler* sampler)
	: Tile(x, y, 0, 0),
	  m_text(""), m_fontPath(""), m_fontSize(24),
	  m_textTexture(nullptr), m_textPipeline(textPipeline), m_sampler(sampler),
	  m_textWidth(0), m_textHeight(0)
{
}

TextTile::~TextTile() {
	releaseTexture();
}

void TextTile::releaseTexture() {
	if (m_textTexture && m_device) {
		SDL_ReleaseGPUTexture(m_device, m_textTexture);
	}
	m_textTexture = nullptr;
}

// --- Chainable Setters ---
TextTile* TextTile::setText(const std::string& text) { m_text = text; return this; }

// Removed the weight parameter to match the header!
TextTile* TextTile::setFont(const std::string& fontPath, int size) { 
	m_fontPath = fontPath; 
	m_fontSize = size; 
	return this; 
}

TextTile* TextTile::setMaxWidth(float maxWidth) { m_maxWidth = maxWidth; return this; }

TextTile* TextTile::setMinWidth(float minWidth) { m_minWidth = minWidth; return this; }

void TextTile::setRasterScale(float scale) {
	m_rasterScale = scale > 0.0f ? scale : 1.0f;
}



void TextTile::build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd){
	if (m_text.empty() || m_fontPath.empty()) return;

	// Safe to call build() more than once without leaking the old texture
	releaseTexture();

	// 1. Open the font at its on-screen pixel size
	const char* basePath = SDL_GetBasePath();
	if (!basePath) return;

	std::string fullPath = std::string(basePath) + m_fontPath;
	TTF_Font* font = TTF_OpenFont(fullPath.c_str(), m_fontSize * m_rasterScale);
	if (!font) {
		std::cout << "Failed to load font " << m_fontPath << ": " << SDL_GetError() << std::endl;
		return;
	}

	// 2. Fit the text to the maximum width (if one is set), then measure in pixels and convert back to design units
	std::string displayText = m_text;

	if (m_maxWidth > 0.0f) {
		const int maximumPixelWidth = static_cast<int>(m_maxWidth * m_rasterScale);

		int fullWidth = 0;
		TTF_GetStringSize(font, m_text.c_str(), m_text.length(), &fullWidth, nullptr);

		if (fullWidth > maximumPixelWidth) {
			// Use the real ellipsis character if the font has it
			const char* ellipsis = TTF_FontHasGlyph(font, 0x2026) ? "\xE2\x80\xA6" : "...";

			int ellipsisWidth = 0;
			TTF_GetStringSize(font, ellipsis, std::strlen(ellipsis), &ellipsisWidth, nullptr);

			// A max width of 0 means "unlimited" to SDL_ttf, so never pass less than 1
			const int availableWidth = std::max(maximumPixelWidth - ellipsisWidth, 1);

			int fittingWidth = 0;
			size_t fittingLength = 0;
			TTF_MeasureString(font, m_text.c_str(), m_text.length(), availableWidth, &fittingWidth, &fittingLength);

			displayText = m_text.substr(0, fittingLength);

			// Don't leave a gap between the last word and the ellipsis
			while (!displayText.empty() && displayText.back() == ' ') displayText.pop_back();
			displayText += ellipsis;
		}
	}

	int texW = 0, texH = 0;
	if (!TTF_GetStringSize(font, displayText.c_str(), displayText.length(), &texW, &texH)) {
		std::cout << "TTF_GetStringSize failed: " << SDL_GetError() << std::endl;
		TTF_CloseFont(font);
		return;
	}
	m_textWidth = texW / m_rasterScale;
	m_textHeight = texH / m_rasterScale;
	width = std::max(m_textWidth + paddingLeft + paddingRight, m_minWidth);
	height = m_textHeight + paddingTop + paddingBottom;

	// 3. Render CPU surface (the font isn't needed after this)
	SDL_Color textColor = { 255, 255, 255, 255 };
	SDL_Surface* rawTextSurface = TTF_RenderText_Blended(font, displayText.c_str(), displayText.length(), textColor);
	TTF_CloseFont(font);
	if (!rawTextSurface) {
		std::cout << "TTF_RenderText_Blended failed: " << SDL_GetError() << std::endl;
		return;
	}

	SDL_Surface* textSurface = SDL_ConvertSurface(rawTextSurface, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(rawTextSurface);
	if (!textSurface) {
		std::cout << "SDL_ConvertSurface failed: " << SDL_GetError() << std::endl;
		return;
	}

	// 4. Upload to GPU texture
	SDL_GPUTextureCreateInfo texInfo{};
	texInfo.type = SDL_GPU_TEXTURETYPE_2D;
	texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	texInfo.width = textSurface->w;
	texInfo.height = textSurface->h;
	texInfo.layer_count_or_depth = 1;
	texInfo.num_levels = 1;
	texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

	SDL_GPUTexture* texture = SDL_CreateGPUTexture(gpuDevice, &texInfo);
	if (!texture) {
		std::cout << "SDL_CreateGPUTexture failed: " << SDL_GetError() << std::endl;
		SDL_DestroySurface(textSurface);
		return;
	}

	SDL_GPUTransferBufferCreateInfo transferInfo{};
	transferInfo.size = textSurface->pitch * textSurface->h;
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

	SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(gpuDevice, &transferInfo);
	void* map = transferBuffer ? SDL_MapGPUTransferBuffer(gpuDevice, transferBuffer, false) : nullptr;
	if (!map) {
		std::cout << "Failed to map transfer buffer: " << SDL_GetError() << std::endl;
		if (transferBuffer) SDL_ReleaseGPUTransferBuffer(gpuDevice, transferBuffer);
		SDL_ReleaseGPUTexture(gpuDevice, texture);
		SDL_DestroySurface(textSurface);
		return;
	}
	memcpy(map, textSurface->pixels, transferInfo.size);
	SDL_UnmapGPUTransferBuffer(gpuDevice, transferBuffer);

	SDL_GPUTextureTransferInfo copySource{};
	copySource.transfer_buffer = transferBuffer;
	copySource.offset = 0;
	copySource.pixels_per_row = 0;
	copySource.rows_per_layer = textSurface->h;

	SDL_GPUTextureRegion copyDest{};
	copyDest.texture = texture;
	copyDest.w = textSurface->w;
	copyDest.h = textSurface->h;
	copyDest.d = 1;

	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmd);
	SDL_UploadToGPUTexture(copyPass, &copySource, &copyDest, false);
	SDL_EndGPUCopyPass(copyPass);

	// 5. Cleanup local memory, then take ownership of the texture
	SDL_ReleaseGPUTransferBuffer(gpuDevice, transferBuffer);
	SDL_DestroySurface(textSurface);

	m_device = gpuDevice;
	m_textTexture = texture;
}


void TextTile::Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth, float designHeight) const {
	// 1. Draw the background tile, unless it's completely transparent
	if (color.a > 0.0f || borderColor.a > 0.0f || focused) {
		Tile::Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);
	}

	if (!m_textTexture || !m_textPipeline || !m_sampler) return;
	if (m_opacity <= 0.0f) return;

	// 2. Swap to the text pipeline
	SDL_BindGPUGraphicsPipeline(renderPass, m_textPipeline);

	// 3. Bind the text texture and sampler
	SDL_GPUTextureSamplerBinding samplerBinding{};
	samplerBinding.texture = m_textTexture;
	samplerBinding.sampler = m_sampler;
	SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);


	struct TintUniforms { float tint[4]; float opacity; float pad[3]; };
	TintUniforms fragUniforms{ { 1.0f, 1.0f, 1.0f, 1.0f }, m_opacity, {} };
	SDL_PushGPUFragmentUniformData(cmdBuffer, 0, &fragUniforms, sizeof(fragUniforms));


	// 4. Clean Top-Left Figma-style positioning!
	struct TextureUniforms { float x, y, w, h, viewportW, viewportH; };
	TextureUniforms uniforms{};
	
	uniforms.x = std::round((x + paddingLeft) * pixelsPerUnit) / pixelsPerUnit;
	uniforms.y = std::round((y + paddingTop) * pixelsPerUnit) / pixelsPerUnit;

	uniforms.w = m_textWidth;
	uniforms.h = m_textHeight;
	uniforms.viewportW = designWidth;
	uniforms.viewportH = designHeight;

	SDL_PushGPUVertexUniformData(cmdBuffer, 0, &uniforms, sizeof(uniforms));
	
	// 5. Draw the text quad
	SDL_DrawGPUPrimitives(renderPass, 6, 1, 0, 0);
}


void TextTile::fadeTo(float target, float durationSeconds) {
	m_targetOpacity = std::clamp(target, 0.0f, 1.0f);
	m_opacityRate = durationSeconds > 0.0f ? 1.0f / durationSeconds : 1.0e6f;
}


void TextTile::setOpacity(float opacity) {
	m_opacity = m_targetOpacity = std::clamp(opacity, 0.0f, 1.0f);
}


void TextTile::update(float deltaSeconds) {
	if (m_opacity == m_targetOpacity) return;

	const float step = m_opacityRate * deltaSeconds;
	if (m_opacity < m_targetOpacity) m_opacity = std::min(m_opacity + step, m_targetOpacity);
	else                             m_opacity = std::max(m_opacity - step, m_targetOpacity);
}