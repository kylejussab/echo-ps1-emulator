#include "gfx/GPUTexture.h"
#include <cstring>
#include <iostream>

SDL_GPUTexture* createTextureFromSurface(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd, SDL_Surface* surface) {
	SDL_GPUTextureCreateInfo texInfo{};
	texInfo.type = SDL_GPU_TEXTURETYPE_2D;
	texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	texInfo.width = surface->w;
	texInfo.height = surface->h;
	texInfo.layer_count_or_depth = 1;
	texInfo.num_levels = 1;
	texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

	SDL_GPUTexture* texture = SDL_CreateGPUTexture(gpuDevice, &texInfo);
	if (!texture) {
		std::cout << "SDL_CreateGPUTexture failed: " << SDL_GetError() << std::endl;
		return nullptr;
	}

	const Uint32 rowBytes = static_cast<Uint32>(surface->w) * 4;

	SDL_GPUTransferBufferCreateInfo transferInfo{};
	transferInfo.size = rowBytes * surface->h;
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

	SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(gpuDevice, &transferInfo);
	Uint8* map = transferBuffer ? static_cast<Uint8*>(SDL_MapGPUTransferBuffer(gpuDevice, transferBuffer, false)) : nullptr;
	if (!map) {
		std::cout << "Failed to map transfer buffer: " << SDL_GetError() << std::endl;
		if (transferBuffer) SDL_ReleaseGPUTransferBuffer(gpuDevice, transferBuffer);
		SDL_ReleaseGPUTexture(gpuDevice, texture);
		return nullptr;
	}

	// Copy row by row so the surface's pitch can't throw the layout off
	const Uint8* source = static_cast<const Uint8*>(surface->pixels);
	for (int row = 0; row < surface->h; ++row) {
		std::memcpy(map + row * rowBytes, source + row * surface->pitch, rowBytes);
	}
	SDL_UnmapGPUTransferBuffer(gpuDevice, transferBuffer);

	SDL_GPUTextureTransferInfo copySource{};
	copySource.transfer_buffer = transferBuffer;
	copySource.offset = 0;
	copySource.pixels_per_row = 0;
	copySource.rows_per_layer = surface->h;

	SDL_GPUTextureRegion copyDest{};
	copyDest.texture = texture;
	copyDest.w = surface->w;
	copyDest.h = surface->h;
	copyDest.d = 1;

	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmd);
	SDL_UploadToGPUTexture(copyPass, &copySource, &copyDest, false);
	SDL_EndGPUCopyPass(copyPass);

	SDL_ReleaseGPUTransferBuffer(gpuDevice, transferBuffer);
	return texture;
}