#pragma once

#include "ui/Tile.h"
#include <string>
#include "ui/TextTile.h"
#include <memory>

// The picture behind the Recently Played tab: the featured game's background image with a dark layer over it.
// Draw it first, before everything else.
class GameBackground : public Tile {
public:
    GameBackground(SDL_GPUDevice* device, SDL_GPUGraphicsPipeline* rectanglePipeline, SDL_GPUGraphicsPipeline* imagePipeline, SDL_GPUGraphicsPipeline* textPipeline, SDL_GPUSampler* sampler);
	~GameBackground() override;

	// Which game's background should be showing, or an empty string for none. Call it every frame, it only does work when the game changes.
	void setGame(const std::string& gameId);

	// Fades the whole background, for the tabs. 0 is hidden, 1 is fully shown.
	void setOpacity(float opacity) { m_visibility = opacity; }

	void update(float deltaSeconds) override;
	void Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth = 1920.0f, float designHeight = 1080.0f) const override;

    void setCreditFont(const std::string& fontPath, int fontSize);
	void setRasterScale(float scale) override; // re-rasterizes the credit so it stays sharp when the window changes size

private:
	void swapTo(const std::string& gameId);
	bool loadTexture(const std::string& path);
	void releaseTexture();

	SDL_GPUDevice* m_device;
	SDL_GPUGraphicsPipeline* m_rectanglePipeline;
	SDL_GPUGraphicsPipeline* m_imagePipeline;
	SDL_GPUSampler* m_sampler;
	SDL_GPUTexture* m_texture = nullptr;

	std::string m_wantedGameId; // what the dashboard asked for
	std::string m_shownGameId;  // the game whose image is loaded (m_texture is null if it has none)

	float m_fade = 0.0f;       // fades out and in as the game changes
    float m_dwellSeconds = 0.0f; // how long the dashboard has been on a game that isn't showing yet
	float m_visibility = 1.0f; // fades with the tabs

	Tile m_overlay; // the dark layer over the image

    void buildCredit();

	std::unique_ptr<TextTile> m_creditLabel; // the artist's name, bottom left
	std::string m_creditText;
	bool m_hasCredit = false;
	float m_rasterScale = 1.0f;
};