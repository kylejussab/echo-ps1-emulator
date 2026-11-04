#include "ui/GameTile.h"
#include <cmath>
#include <utility>
#include <algorithm>
#include <SDL3_image/SDL_image.h>
#include <iostream>

namespace {

SDL_FColor randomTileColor() {
	return SDL_FColor{
		0.25f + SDL_randf() * 0.35f,
		0.25f + SDL_randf() * 0.35f,
		0.25f + SDL_randf() * 0.35f,
		1.0f
	};
}

float approach(float current, float target, float t) {
	const float next = current + (target - current) * t;
	return std::abs(target - next) < 0.05f ? target : next;
}

} // namespace

GameTile::GameTile(float x_, float y_, float width_, float height_, GameInfo info_)
	: Tile(x_, y_, width_, height_, randomTileColor()),
	  info(std::move(info_)),
	  targetX(x_), targetY(y_), targetWidth(width_), targetHeight(height_) {}


GameTile::~GameTile() {
	if (boxArtTexture && boxArtDevice) {
		SDL_ReleaseGPUTexture(boxArtDevice, boxArtTexture);
	}
}



void GameTile::setTarget(float x_, float y_, float width_, float height_, float borderWidth_) {
	targetX = x_;
	targetY = y_;
	targetWidth = width_;
	targetHeight = height_;
	targetBorderWidth = borderWidth_;
}

void GameTile::snapToTarget() {
	x = targetX;
	y = targetY;
	width = targetWidth;
	height = targetHeight;
	borderWidth = targetBorderWidth;
    positionLabel();
}

void GameTile::update(float deltaSeconds) {
	// Frame-rate independent: the same fraction of time always closes the same fraction of distance
	const float t = 1.0f - std::exp(-animationSpeed * deltaSeconds);

	if (!driven) {
		x = approach(x, targetX, t);
		y = approach(y, targetY, t);
		width = approach(width, targetWidth, t);
		height = approach(height, targetHeight, t);
		borderWidth = approach(borderWidth, targetBorderWidth, t);
	}

    if (label) {
		const float labelTarget = labelVisible ? 1.0f : 0.0f;
		const float labelStep = deltaSeconds / labelFadeSeconds;
		if (labelAlpha < labelTarget) labelAlpha = std::min(labelAlpha + labelStep, labelTarget);
		else                          labelAlpha = std::max(labelAlpha - labelStep, labelTarget);

		// The label also fades with its tab, through the tile's own draw opacity
		label->setOpacity(labelAlpha * drawOpacity);
        label->setDrawOpacity(labelAlpha * drawOpacity);
		positionLabel();
	}
}


bool GameTile::isSettled() const {
	// Only position and size matter. Border changes (focus gaining/losing) shouldn't hide the title.
	// approach() snaps exactly onto the target when it gets close, so == is reliable here
	return x == targetX && y == targetY &&
	       width == targetWidth && height == targetHeight;
}



void GameTile::setLabel(SDL_GPUGraphicsPipeline* textPipeline, SDL_GPUSampler* sampler, const std::string& fontPath, int fontSize) {
	const float labelBoxWidth = targetWidth - labelInset * 2.0f;
    label = std::make_unique<TextTile>(x, y, textPipeline, sampler);
	
	label->setText(info.name)
	     ->setFont(fontPath, fontSize)
	     ->setMaxWidth(labelBoxWidth - labelPaddingLeft - labelPaddingRight)
	     ->setMinWidth(labelBoxWidth)
	     ->setPadding(labelPaddingLeft, labelPaddingRight, labelPaddingTop, labelPaddingBottom)
	     ->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, labelBackgroundOpacity})
	     ->setCornerRadius(labelCornerRadius);
	label->setOpacity(0.0f);
}

void GameTile::setLabelVisible(bool visible, bool animate) {
	labelVisible = visible;
	if (!animate) labelAlpha = visible ? 1.0f : 0.0f;
}

void GameTile::build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) {
	// Box art doesn't depend on the window size, so it only loads once. Resizes only rebuild the label.
	if (boxArtPipeline && !boxArtLoadAttempted) loadBoxArt(gpuDevice, uploadCmd);

	if (!label) return;
	label->build(gpuDevice, uploadCmd);
	positionLabel();
}

void GameTile::setRasterScale(float scale) {
	if (label) label->setRasterScale(scale);
}

// Pins the label to the bottom left of the tile, so it follows the tile while it animates
void GameTile::positionLabel() {
	if (!label) return;
	label->setPosition(x + labelInset, y + height - labelInsetBottom - label->getHeight());
}

void GameTile::Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth, float designHeight) const {
	if (boxArtTexture) {
		SDL_BindGPUGraphicsPipeline(renderPass, boxArtPipeline);

		SDL_GPUTextureSamplerBinding samplerBinding{};
		samplerBinding.texture = boxArtTexture;
		samplerBinding.sampler = boxArtSampler;
		SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);
	}

	// With no box art the rectangle pipeline Dashboard bound is used, so the tile keeps its color
	Tile::Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);

	if (label && label->getOpacity() > 0.0f) {
		// The label's background is a plain rectangle, so it must not be drawn by the box art pipeline
		if (boxArtTexture) SDL_BindGPUGraphicsPipeline(renderPass, rectanglePipeline);

		label->Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);
	}
}


void GameTile::setBoxArt(SDL_GPUGraphicsPipeline* rectanglePipeline_, SDL_GPUGraphicsPipeline* imagePipeline, SDL_GPUSampler* sampler) {
	rectanglePipeline = rectanglePipeline_;
	boxArtPipeline = imagePipeline;
	boxArtSampler = sampler;
}

void GameTile::loadBoxArt(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) {
	boxArtLoadAttempted = true;
	if (info.boxArtPath.empty()) return; // no art, the tile keeps its color

	SDL_Surface* loadedSurface = IMG_Load(info.boxArtPath.c_str());
	if (!loadedSurface) {
		std::cout << "Failed to load box art " << info.boxArtPath << ": " << SDL_GetError() << std::endl;
		return;
	}

	SDL_Surface* surface = SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(loadedSurface);
	if (!surface) return;

	// A full mip chain keeps the art smooth when the window is smaller than the image
	Uint32 mipLevels = 1;
	for (int size = std::max(surface->w, surface->h); size > 1; size >>= 1) ++mipLevels;

	SDL_GPUTextureCreateInfo textureInfo{};
	textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
	textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	textureInfo.width = surface->w;
	textureInfo.height = surface->h;
	textureInfo.layer_count_or_depth = 1;
	textureInfo.num_levels = mipLevels;
	textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; // color target is needed to generate mips

	SDL_GPUTexture* texture = SDL_CreateGPUTexture(gpuDevice, &textureInfo);
	if (!texture) {
		std::cout << "SDL_CreateGPUTexture failed for box art: " << SDL_GetError() << std::endl;
		SDL_DestroySurface(surface);
		return;
	}

	SDL_GPUTransferBufferCreateInfo transferInfo{};
	transferInfo.size = surface->pitch * surface->h;
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

	SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(gpuDevice, &transferInfo);
	void* mapped = transferBuffer ? SDL_MapGPUTransferBuffer(gpuDevice, transferBuffer, false) : nullptr;
	if (!mapped) {
		std::cout << "Failed to map transfer buffer for box art: " << SDL_GetError() << std::endl;
		if (transferBuffer) SDL_ReleaseGPUTransferBuffer(gpuDevice, transferBuffer);
		SDL_ReleaseGPUTexture(gpuDevice, texture);
		SDL_DestroySurface(surface);
		return;
	}
	SDL_memcpy(mapped, surface->pixels, transferInfo.size);
	SDL_UnmapGPUTransferBuffer(gpuDevice, transferBuffer);

	SDL_GPUTextureTransferInfo copySource{};
	copySource.transfer_buffer = transferBuffer;
	copySource.pixels_per_row = surface->pitch / 4;
	copySource.rows_per_layer = surface->h;

	SDL_GPUTextureRegion copyDestination{};
	copyDestination.texture = texture;
	copyDestination.w = surface->w;
	copyDestination.h = surface->h;
	copyDestination.d = 1;

	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmd);
	SDL_UploadToGPUTexture(copyPass, &copySource, &copyDestination, false);
	SDL_EndGPUCopyPass(copyPass);

	// Has to happen outside any pass, after the upload
	SDL_GenerateMipmapsForGPUTexture(uploadCmd, texture);

	SDL_ReleaseGPUTransferBuffer(gpuDevice, transferBuffer);
	SDL_DestroySurface(surface);

	boxArtDevice = gpuDevice;
	boxArtTexture = texture;
}

void GameTile::placeAt(float x_, float y_, float width_, float height_) {
	x = x_;
	y = y_;
	width = width_;
	height = height_;
	positionLabel();
	borderWidth = 0.0f;
}

