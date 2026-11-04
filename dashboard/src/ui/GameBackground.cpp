#include "ui/GameBackground.h"
#include "gfx/Renderer.h"
#include "gfx/GPUTexture.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <iostream>

namespace {

constexpr const char* backgroundFolder = "backgrounds"; // next to the executable, like boxart
const char* const imageExtensions[] = { ".png", ".jpg" };

constexpr float overlayOpacity = 0.7f;
constexpr float fadeOutSeconds = 0.2f;
constexpr float fadeInSeconds = 0.2f;

constexpr float landDelaySeconds = 0.3f; // how long the selection must stay on a game before its background replaces the current one

constexpr float creditLeft = 120.0f;
constexpr float creditBottom = 50.0f;
constexpr float creditMaxWidth = 960.0f;

// The first line of a text file, with spaces and an editor's byte order mark removed. Empty if the file doesn't exist.
std::string readFirstLine(const std::string& path) {
	size_t size = 0;
	void* data = SDL_LoadFile(path.c_str(), &size);
	if (!data) return std::string();

	std::string text(static_cast<const char*>(data), size);
	SDL_free(data);

	if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);

	text = text.substr(0, text.find_first_of("\r\n"));

	const size_t first = text.find_first_not_of(" \t");
	if (first == std::string::npos) return std::string();
	const size_t last = text.find_last_not_of(" \t");
	return text.substr(first, last - first + 1);
}
} // namespace


GameBackground::GameBackground(SDL_GPUDevice* device, SDL_GPUGraphicsPipeline* rectanglePipeline, SDL_GPUGraphicsPipeline* imagePipeline, SDL_GPUGraphicsPipeline* textPipeline, SDL_GPUSampler* sampler)
	: Tile(0.0f, 0.0f, Renderer::designWidth, Renderer::designHeight, SDL_FColor{ 1.0f, 1.0f, 1.0f, 1.0f }),
	  m_device(device), m_rectanglePipeline(rectanglePipeline), m_imagePipeline(imagePipeline), m_sampler(sampler),
	  m_overlay(0.0f, 0.0f, Renderer::designWidth, Renderer::designHeight, SDL_FColor{ 0.0f, 0.0f, 0.0f, overlayOpacity })
{
	m_creditLabel = std::make_unique<TextTile>(0.0f, 0.0f, textPipeline, sampler);
	m_creditLabel->setMaxWidth(creditMaxWidth)->setBackgroundColor(SDL_FColor{ 0.0f, 0.0f, 0.0f, 0.0f });
}


GameBackground::~GameBackground() {
	releaseTexture();
}


void GameBackground::releaseTexture() {
	if (m_texture && m_device) SDL_ReleaseGPUTexture(m_device, m_texture);
	m_texture = nullptr;
}


void GameBackground::setGame(const std::string& gameId) {
	if (gameId == m_wantedGameId) return;

	m_wantedGameId = gameId;
	m_dwellSeconds = 0.0f; // landing on a new tile restarts the wait
}


void GameBackground::update(float deltaSeconds) {
	const bool swapPending = (m_wantedGameId != m_shownGameId);
	if (swapPending) m_dwellSeconds += deltaSeconds;

	if (swapPending && m_dwellSeconds >= landDelaySeconds) {
		// Stayed on the tile long enough. Fade the current image out, and swap once it is gone.
		m_fade = std::max(m_fade - deltaSeconds / fadeOutSeconds, 0.0f);
		if (m_fade <= 0.0f) swapTo(m_wantedGameId);
	}
	else if (m_texture) {
		// Nothing to change yet, or the selection moved on again before the wait was over. What is showing stays, or comes back.
		m_fade = std::min(m_fade + deltaSeconds / fadeInSeconds, 1.0f);
	}

	drawOpacity = m_fade * m_visibility;
    m_creditLabel->setOpacity(drawOpacity);
	m_creditLabel->setDrawOpacity(drawOpacity);
	m_overlay.setDrawOpacity(m_visibility); // the dark layer doesn't fade with the images
}


// Loads the game's image if it has one. Runs while the screen shows nothing of the old image, so a slow decode is hidden.
void GameBackground::swapTo(const std::string& gameId) {
	releaseTexture();
	m_hasCredit = false;
	m_shownGameId = gameId;
	if (gameId.empty()) return;

	const char* basePath = SDL_GetBasePath();
	if (!basePath) return;

	const std::string pathWithoutExtension = std::string(basePath) + backgroundFolder + "/" + gameId;

	for (const char* extension : imageExtensions) {
		const std::string path = pathWithoutExtension + extension;

		SDL_PathInfo pathInfo;
		if (!SDL_GetPathInfo(path.c_str(), &pathInfo)) continue; // no such file, so the game has no background

		if (loadTexture(path)) {
			m_creditText = readFirstLine(pathWithoutExtension + ".txt");
			buildCredit();
			return;
		}
	}
}


bool GameBackground::loadTexture(const std::string& path) {
	SDL_Surface* loadedSurface = IMG_Load(path.c_str());
	if (!loadedSurface) {
		std::cout << "Failed to load background " << path << ": " << SDL_GetError() << std::endl;
		return false;
	}

	SDL_Surface* surface = SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(loadedSurface);
	if (!surface) return false;

	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_device);
	if (!uploadCmd) {
		SDL_DestroySurface(surface);
		return false;
	}

	SDL_GPUTexture* texture = createTextureFromSurface(m_device, uploadCmd, surface);
	SDL_SubmitGPUCommandBuffer(uploadCmd);
	SDL_DestroySurface(surface);
	if (!texture) return false;

	m_texture = texture;
	return true;
}


void GameBackground::Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth, float designHeight) const {
	if (m_visibility <= 0.0f) return;

	if (m_texture && drawOpacity > 0.0f) {
		// The image, through the same pipeline the box art uses
		SDL_BindGPUGraphicsPipeline(renderPass, m_imagePipeline);

		SDL_GPUTextureSamplerBinding samplerBinding{};
		samplerBinding.texture = m_texture;
		samplerBinding.sampler = m_sampler;
		SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);

		Tile::Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);

		SDL_BindGPUGraphicsPipeline(renderPass, m_rectanglePipeline);
	}

	// The dark layer is always there, with or without an image, so it never pops in and out
	m_overlay.Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);

    if (m_hasCredit && drawOpacity > 0.0f) {
		m_creditLabel->Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);
	}
}


void GameBackground::setCreditFont(const std::string& fontPath, int fontSize) {
	m_creditLabel->setFont(fontPath, fontSize);
}


void GameBackground::setRasterScale(float scale) {
	m_rasterScale = scale > 0.0f ? scale : 1.0f;
	buildCredit();
}


// Rasterizes the credit text and pins it to the bottom left. With no text, nothing is drawn.
void GameBackground::buildCredit() {
	m_hasCredit = false;
	if (m_creditText.empty()) return;

	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_device);
	if (!uploadCmd) return;

	m_creditLabel->setText(m_creditText);
	m_creditLabel->setRasterScale(m_rasterScale);
	m_creditLabel->build(m_device, uploadCmd);
	SDL_SubmitGPUCommandBuffer(uploadCmd);

	m_creditLabel->setPosition(creditLeft, Renderer::designHeight - creditBottom - m_creditLabel->getHeight());
	m_hasCredit = true;
}