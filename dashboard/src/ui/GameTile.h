#pragma once

#include "ui/Tile.h"
#include <string>
#include "library/GameInfo.h"
#include <memory>
#include "ui/TextTile.h"
#include <string>

class GameTile : public Tile {
public:
	GameTile(float x_, float y_, float width_, float height_, GameInfo info_);

    ~GameTile() override;

	// Lets the tile draw its box art (info.boxArtPath) in place of its color. Call before build().
    void setBoxArt(SDL_GPUGraphicsPipeline* rectanglePipeline, SDL_GPUGraphicsPipeline* imagePipeline, SDL_GPUSampler* sampler);

	const GameInfo& getInfo() const { return info; }
	const std::string& getGameName() const { return info.name; }


	// Where the tile is heading. It eases there over the next few frames.
	void setTarget(float x_, float y_, float width_, float height_, float borderWidth_);


	// Jump straight to the target with no animation (used for the first layout)
	void snapToTarget();


	void update(float deltaSeconds) override;


    bool isSettled() const;

    // Gives the tile a title label inside its own bounds, bottom left. Long titles are cut with an ellipsis.
	void setLabel(SDL_GPUGraphicsPipeline* textPipeline, SDL_GPUSampler* sampler, const std::string& fontPath, int fontSize);

	// Shows or hides the label. It fades unless animate is false.
	void setLabelVisible(bool visible, bool animate = true);

	void build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) override;
	void setRasterScale(float scale) override;
	void Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth = 1920.0f, float designHeight = 1080.0f) const override;

	// While a launch is driving the tile, update() leaves its position and size alone
	void setDriven(bool drivenValue) { driven = drivenValue; }

	// Puts the tile exactly here with no easing. The targets are untouched, so snapToTarget() puts it back.
	void placeAt(float x_, float y_, float width_, float height_);

	int64_t getTimePlayedSeconds() const { return info.timePlayedSeconds; }
	void setTimePlayedSeconds(int64_t seconds) { info.timePlayedSeconds = seconds; }

private:
	GameInfo info;

	void loadBoxArt(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd);

	SDL_GPUTexture* boxArtTexture = nullptr;
	SDL_GPUDevice* boxArtDevice = nullptr;
	SDL_GPUGraphicsPipeline* boxArtPipeline = nullptr;
	SDL_GPUSampler* boxArtSampler = nullptr;
    SDL_GPUGraphicsPipeline* rectanglePipeline = nullptr;
	bool boxArtLoadAttempted = false;

	float targetX;
	float targetY;
	float targetWidth;
	float targetHeight;
	float targetBorderWidth = 0.0f;

	// How fast tiles ease toward their target, in 1/seconds. Higher is snappier.
	static constexpr float animationSpeed = 20.0f;


    std::unique_ptr<TextTile> label;
	bool labelVisible = false;
	float labelAlpha = 0.0f;

	void positionLabel();

	static constexpr float labelInset = 0.0f;           // space between the box and the tile's left and right edges
	static constexpr float labelInsetBottom = 0.0f;
	static constexpr float labelPaddingLeft = 24.0f;
	static constexpr float labelPaddingRight = 24.0f;
	static constexpr float labelPaddingTop = 12.0f;
	static constexpr float labelPaddingBottom = 24.0f;
	static constexpr float labelCornerRadius = 0.0f;
	static constexpr float labelBackgroundOpacity = 0.3f;
	static constexpr float labelFadeSeconds = 0.2f;


	bool driven = false;
};