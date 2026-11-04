#pragma once

#include <SDL3/SDL.h>

// Uploads an RGBA32 surface into a new sampler texture. Returns nullptr on failure.
// The surface is not freed, and the caller still owns it.
SDL_GPUTexture* createTextureFromSurface(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd, SDL_Surface* surface);