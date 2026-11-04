// GameView.cpp
#include "gfx/GameView.h"
#include <algorithm>
#include <cstring>
#include "core/Constants.h"
#include "gfx/Renderer.h"

namespace {
	// The emulator's pixel buffer is always the size of VRAM
	constexpr int bufferWidth = Hardware::VRAM_WIDTH;
	constexpr int bufferHeight = Hardware::VRAM_HEIGHT;

	// The emulator describes where the picture goes in a 640x480 (4:3) space. The dashboard's design space is 1920x1080,
	// so the picture is scaled to fill the design height and centered, which leaves black bars left and right.
	constexpr float outputWidth = 640.0f;
}


bool GameView::build() {
	SDL_GPUDevice* device = m_renderer.device();
	if (!device) return false;

	SDL_GPUTextureCreateInfo textureInfo{};
	textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
	textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	textureInfo.width = bufferWidth;
	textureInfo.height = bufferHeight;
	textureInfo.layer_count_or_depth = 1;
	textureInfo.num_levels = 1;
	m_texture = SDL_CreateGPUTexture(device, &textureInfo);

	SDL_GPUTransferBufferCreateInfo transferInfo{};
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transferInfo.size = static_cast<Uint32>(bufferWidth * bufferHeight * 4);
	m_transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);

	return m_texture && m_transferBuffer;
}


void GameView::release() {
	SDL_GPUDevice* device = m_renderer.device();
	if (device) {
		if (m_texture) SDL_ReleaseGPUTexture(device, m_texture);
		if (m_transferBuffer) SDL_ReleaseGPUTransferBuffer(device, m_transferBuffer);
	}
	m_texture = nullptr;
	m_transferBuffer = nullptr;
	m_hasPicture = false;
}


void GameView::upload(const DisplayFrame& frame) {
	m_hasPicture = false;
	if (!m_texture || !m_transferBuffer || !frame.pixels) return;

	// Only the visible part of the buffer is converted and uploaded. Clamped so a bad display register can't read outside the buffer.
	const int sourceX = std::clamp(frame.sourceX, 0, bufferWidth - 1);
	const int sourceY = std::clamp(frame.sourceY, 0, bufferHeight - 1);
	const int width = std::min(frame.sourceWidth, bufferWidth - sourceX);
	const int height = std::min(frame.sourceHeight, bufferHeight - sourceY);
	if (width <= 0 || height <= 0) return;

	SDL_GPUDevice* device = m_renderer.device();
	Uint8* mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, m_transferBuffer, true));
	if (!mapped) return;

	for (int row = 0; row < height; row++) {
		const Uint8* sourceRow = frame.pixels + static_cast<size_t>(sourceY + row) * frame.pitchBytes;
		Uint8* out = mapped + static_cast<size_t>(row) * width * 4;

		if (frame.is24Bit) {
			// R, G, B byte triplets
			sourceRow += static_cast<size_t>(sourceX) * 3;
			for (int column = 0; column < width; column++) {
				out[0] = sourceRow[0];
				out[1] = sourceRow[1];
				out[2] = sourceRow[2];
				out[3] = 255;
				sourceRow += 3;
				out += 4;
			}
		}
		else {
			// PS1 VRAM halfwords: bit 15 mask (ignored), bits 14-10 blue, 9-5 green, 4-0 red. 5-bit channels expand by repeating the top bits.
			sourceRow += static_cast<size_t>(sourceX) * 2;
			for (int column = 0; column < width; column++) {
				uint16_t pixel;
				std::memcpy(&pixel, sourceRow, sizeof(pixel));
				const Uint8 red = pixel & 0x1F;
				const Uint8 green = (pixel >> 5) & 0x1F;
				const Uint8 blue = (pixel >> 10) & 0x1F;
				out[0] = static_cast<Uint8>((red << 3) | (red >> 2));
				out[1] = static_cast<Uint8>((green << 3) | (green >> 2));
				out[2] = static_cast<Uint8>((blue << 3) | (blue >> 2));
				out[3] = 255;
				sourceRow += 2;
				out += 4;
			}
		}
	}

	SDL_UnmapGPUTransferBuffer(device, m_transferBuffer);

	// Its own command buffer, submitted before the frame's render command buffer, so the pixels are there when the frame draws
	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
	if (!cmd) return;

	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

	SDL_GPUTextureTransferInfo source{};
	source.transfer_buffer = m_transferBuffer;
	source.offset = 0;
	source.pixels_per_row = static_cast<Uint32>(width);
	source.rows_per_layer = static_cast<Uint32>(height);

	SDL_GPUTextureRegion destination{};
	destination.texture = m_texture;
	destination.w = static_cast<Uint32>(width);
	destination.h = static_cast<Uint32>(height);
	destination.d = 1;

	// cycle = true: if last frame's draw is still reading the texture, write into a fresh copy instead of waiting
	SDL_UploadToGPUTexture(copyPass, &source, &destination, true);
	SDL_EndGPUCopyPass(copyPass);
	SDL_SubmitGPUCommandBuffer(cmd);

	// VideoOutput centers the cropped picture inside 640x480 with black bars above and below it.
	// Here it is scaled until the picture itself fills the full design height, the same scale in both directions so nothing stretches.
	const float scale = Renderer::designHeight / static_cast<float>(frame.destinationHeight);
	const float left = (Renderer::designWidth - outputWidth * scale) * 0.5f + frame.destinationX * scale;
	const float top = 0.0f;
	const float right = left + frame.destinationWidth * scale;
	const float bottom = Renderer::designHeight;

	m_rect[0] = left / Renderer::designWidth * 2.0f - 1.0f;
	m_rect[1] = 1.0f - top / Renderer::designHeight * 2.0f;
	m_rect[2] = right / Renderer::designWidth * 2.0f - 1.0f;
	m_rect[3] = 1.0f - bottom / Renderer::designHeight * 2.0f;

	// The converted region sits at the top left of the texture
	m_uvRect[0] = 0.0f;
	m_uvRect[1] = 0.0f;
	m_uvRect[2] = static_cast<float>(width) / bufferWidth;
	m_uvRect[3] = static_cast<float>(height) / bufferHeight;

	m_hasPicture = true;
}


void GameView::draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) {
	if (!m_hasPicture || !m_texture || !m_renderer.gamePipeline() || !m_renderer.pixelSampler()) return;

	SDL_BindGPUGraphicsPipeline(pass, m_renderer.gamePipeline());

	struct Uniforms {
		float rect[4];
		float uvRect[4];
	} uniforms;
	std::memcpy(uniforms.rect, m_rect, sizeof(uniforms.rect));
	std::memcpy(uniforms.uvRect, m_uvRect, sizeof(uniforms.uvRect));
	SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));

	SDL_GPUTextureSamplerBinding binding{};
	binding.texture = m_texture;
	binding.sampler = m_renderer.pixelSampler();
	SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

	SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
}
