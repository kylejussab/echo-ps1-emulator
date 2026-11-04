#include "ui/RecentlyPlayedRow.h"
#include <algorithm>
#include <string>
#include "ui/Layout.h"

using namespace layout;

namespace {

constexpr size_t maxRecentlyPlayed = 7;
constexpr float tileSize = 125.0f;
constexpr float featuredTileSize = 215.0f; // selected tile only
constexpr float tileGap = 30.0f;

constexpr float featuredTitleX = 370.0f;
constexpr float featuredTitleY = 410.0f;
constexpr int featuredTitleFontSize = 32;

constexpr float titleFadeOutSeconds = 0.08f; // quick, so it clears out of the way
constexpr float titleFadeInSeconds  = 0.2f;

constexpr int playtimeFontSize = 18;

constexpr float playtimeIconSize = 18.0f;

constexpr int suspendedFontSize = 18;
constexpr float suspendedGapX = 28.0f; // between the end of the title and the tile
constexpr const char* suspendedLabel = "Ready to resume";

std::string formatTimePlayed(int64_t totalSeconds) {
	const int64_t totalMinutes = totalSeconds / 60;
	if (totalMinutes < 1) return "";

	const int64_t hours = totalMinutes / 60;
	const int64_t minutes = totalMinutes % 60;
	if (hours == 0) return std::to_string(minutes) + "m";

	return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

} // namespace

void RecentlyPlayedRow::build(const GameLibrary& library, float rasterScale) {
	clear();
	m_rasterScale = rasterScale;
	if (library.empty()) return;

	SDL_GPUDevice* device = m_renderer.device();
	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(device);

	for (const std::string& id : library.recentlyPlayed(maxRecentlyPlayed)) {
		auto tile = std::make_unique<GameTile>(0.0f, 0.0f, tileSize, tileSize, library.at(id));

		tile->setBoxArt(m_renderer.rectanglePipeline(), m_renderer.imagePipeline(), m_renderer.sampler());

		tile->setCornerRadius(tileCornerRadius)
		    ->setBorderColor(SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f});

		tile->build(device, uploadCmd);

		m_tiles.push_back(std::move(tile));
	}

	if (m_tiles.empty()) {
		SDL_SubmitGPUCommandBuffer(uploadCmd);
		return;
	}

	m_featuredTitle = std::make_unique<TextTile>(featuredTitleX, featuredTitleY, m_renderer.textPipeline(), m_renderer.sampler());
	m_featuredTitle->setRasterScale(m_rasterScale);
	m_featuredTitle->setText(m_tiles[m_selectedIndex]->getGameName())
	               ->setFont(fontRegular, featuredTitleFontSize)
	               ->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f})
	               ->setInteractable(false)
	               ->build(device, uploadCmd);

	m_titleShownIndex = m_selectedIndex;


	m_playtimeIcon = std::make_unique<IconTile>(featuredTitleX, featuredTitleY, m_renderer.textPipeline(), m_renderer.sampler());
	m_playtimeIcon->setRasterScale(m_rasterScale);
	m_playtimeIcon->setSvg("icons/clock.svg")
	              ->setIconSize(playtimeIconSize, playtimeIconSize)
	              ->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f}) // a plain Tile's default background is light grey
	              ->setInteractable(false)
	              ->build(device, uploadCmd);
	m_playtimeIcon->setDrawOpacity(0.5f);


	m_playtimeText = std::make_unique<TextTile>(featuredTitleX, featuredTitleY, m_renderer.textPipeline(), m_renderer.sampler());
	m_playtimeText->setRasterScale(m_rasterScale);
	m_playtimeText->setText("0m") // placeholder, replaced as soon as there is a play time to show
	              ->setFont(fontRegular, playtimeFontSize)
	              ->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f})
	              ->setInteractable(false)
	              ->build(device, uploadCmd);
	m_playtimeText->setOpacity(0.5f);

	m_suspendedText = std::make_unique<TextTile>(featuredTitleX, featuredTitleY, m_renderer.textPipeline(), m_renderer.sampler());
	m_suspendedText->setRasterScale(m_rasterScale);
	m_suspendedText->setText(suspendedLabel)
	               ->setFont(fontItalic, suspendedFontSize)
	               ->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f})
	               ->setInteractable(false)
	               ->build(device, uploadCmd);
	m_suspendedText->setOpacity(0.0f); // hidden until the featured game is suspended

	SDL_SubmitGPUCommandBuffer(uploadCmd);

	// Position, size and border width come from the selection
	layoutTiles(false);
	applyOpacity();
}

void RecentlyPlayedRow::clear() {
	// The tiles hold GPU textures, so make sure the GPU is done with them first
	if ((!m_tiles.empty() || m_featuredTitle || m_playtimeText || m_suspendedText) && m_renderer.device()) {
		SDL_WaitForGPUIdle(m_renderer.device());
	}

	m_featuredTitle.reset();
	m_playtimeText.reset();
	m_playtimeShownText.clear();
	m_playtimeIcon.reset();
	m_suspendedText.reset();
	m_tiles.clear();
	m_selectedIndex = 0;
	m_titleShownIndex = 0;
}

void RecentlyPlayedRow::update(float deltaSeconds) {
	updateFeaturedTitle();

	for (auto& tile : m_tiles) tile->update(deltaSeconds);
	if (m_featuredTitle) m_featuredTitle->update(deltaSeconds);
	if (m_playtimeIcon && m_playtimeText) m_playtimeIcon->setDrawOpacity(m_playtimeText->getOpacity());
	if (m_playtimeText) m_playtimeText->update(deltaSeconds);
	if (m_suspendedText) m_suspendedText->update(deltaSeconds);
}

void RecentlyPlayedRow::draw(Renderer::Frame& frame) {
	for (auto& tile : m_tiles) frame.draw(*tile);
	if (m_featuredTitle) frame.draw(*m_featuredTitle);
	if (m_playtimeIcon) frame.draw(*m_playtimeIcon);
	if (m_playtimeText) frame.draw(*m_playtimeText);
	if (m_suspendedText) frame.draw(*m_suspendedText);
}

void RecentlyPlayedRow::setRasterScale(float rasterScale) {
	m_rasterScale = rasterScale;
	if (!m_featuredTitle) return;

	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());
	m_featuredTitle->setRasterScale(m_rasterScale);
	m_featuredTitle->build(m_renderer.device(), uploadCmd);

	if (m_playtimeIcon) {
		m_playtimeIcon->setRasterScale(m_rasterScale);
		m_playtimeIcon->build(m_renderer.device(), uploadCmd);
	}

	if (m_playtimeText) {
		m_playtimeText->setRasterScale(m_rasterScale);
		m_playtimeText->build(m_renderer.device(), uploadCmd);
	}

	if (m_suspendedText) {
		m_suspendedText->setRasterScale(m_rasterScale);
		m_suspendedText->build(m_renderer.device(), uploadCmd);
	}

	SDL_SubmitGPUCommandBuffer(uploadCmd);
}

void RecentlyPlayedRow::setOpacity(float opacity) {
	if (opacity == m_opacity) return;
	m_opacity = opacity;
	applyOpacity();
}

void RecentlyPlayedRow::applyOpacity() {
	// Fully faded tiles are hidden so they cost nothing to draw
	for (auto& tile : m_tiles) {
		tile->setDrawOpacity(m_opacity);
		tile->setVisible(m_opacity > 0.0f);
	}
}

void RecentlyPlayedRow::setFocused(bool focused, bool animate) {
	m_focused = focused;
	layoutTiles(animate);
}

void RecentlyPlayedRow::layoutTiles(bool animate) {
	const float step = tileSize + tileGap;
	const float selected = static_cast<float>(m_selectedIndex);
	const float featuredBorder = m_focused ? selectedBorderWidth : 0.0f;

	for (size_t i = 0; i < m_tiles.size(); ++i) {
		GameTile* tile = m_tiles[i].get();
		const float index = static_cast<float>(i);

		if (i == m_selectedIndex) {
			tile->setTarget(rowStartX, rowStartY, featuredTileSize, featuredTileSize, featuredBorder);
		}
		else {
			// Tiles before the selection sit to its left (and may be off screen),
			// tiles after it follow to its right.
			const float x = (i < m_selectedIndex)
				? rowStartX - (selected - index) * step
				: rowStartX + featuredTileSize + tileGap + (index - selected - 1.0f) * step;

			tile->setTarget(x, rowStartY, tileSize, tileSize, 0.0f);
		}

		if (!animate) tile->snapToTarget();
	}
}

void RecentlyPlayedRow::select(size_t index) {
	if (m_tiles.empty()) return;

	index = std::min(index, m_tiles.size() - 1);
	if (index == m_selectedIndex) return;

	m_selectedIndex = index;
	layoutTiles(true);
}

void RecentlyPlayedRow::moveHorizontal(int delta) {
	if (delta < 0 && m_selectedIndex > 0) select(m_selectedIndex - 1);
	else if (delta > 0) select(m_selectedIndex + 1); // select() clamps at the end of the row
}

void RecentlyPlayedRow::jumpToFirst() {
	select(0);
}

void RecentlyPlayedRow::jumpToLast() {
	if (m_tiles.empty()) return;
	select(m_tiles.size() - 1);
}

float RecentlyPlayedRow::anchorX() const {
	return rowStartX + featuredTileSize / 2.0f;
}

void RecentlyPlayedRow::updateFeaturedTitle() {
	if (!m_featuredTitle) return;

	// Swap the name only while the title is invisible, so the new name never flashes in
	if (m_titleShownIndex != m_selectedIndex && m_featuredTitle->getOpacity() <= 0.0f) {
		SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());
		m_featuredTitle->setText(m_tiles[m_selectedIndex]->getGameName())->build(m_renderer.device(), uploadCmd);
		SDL_SubmitGPUCommandBuffer(uploadCmd);
		m_titleShownIndex = m_selectedIndex;
	}

	const bool settled = std::all_of(m_tiles.begin(), m_tiles.end(),
	                                 [](const std::unique_ptr<GameTile>& tile) { return tile->isSettled(); });
	const bool showTitle = settled && m_titleShownIndex == m_selectedIndex && m_opacity >= 1.0f;


	// The play time belongs to the game whose title is on screen right now
	const std::string playtime = formatTimePlayed(m_tiles[m_titleShownIndex]->getTimePlayedSeconds());

	// Like the title, the text only changes while the tile is invisible
	if (!playtime.empty() && playtime != m_playtimeShownText && m_playtimeText->getOpacity() <= 0.0f) {
		SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());
		m_playtimeText->setText(playtime)->build(m_renderer.device(), uploadCmd);
		SDL_SubmitGPUCommandBuffer(uploadCmd);
		m_playtimeShownText = playtime;
	}

	m_playtimeIcon->setPosition(featuredTitleX, 457.5f)->setDrawOpacity(0.5f);
	m_playtimeText->setPosition(featuredTitleX + 30.0f, 455.0f)->setDrawOpacity(0.5f);

	const bool showPlaytime = showTitle && !playtime.empty() && playtime == m_playtimeShownText;
	m_playtimeText->fadeTo(showPlaytime ? 1.0f : 0.0f, showPlaytime ? titleFadeInSeconds : titleFadeOutSeconds);

	m_featuredTitle->fadeTo(showTitle ? 1.0f : 0.0f, showTitle ? titleFadeInSeconds : titleFadeOutSeconds);

	// To the right of the title, vertically centered on it. showTitle already covers "the title on screen is the selected game".
	const float suspendedX = featuredTitleX + m_featuredTitle->getWidth() + suspendedGapX;
	const float suspendedY = featuredTitleY + 10.0f;
	m_suspendedText->setPosition(suspendedX, suspendedY)->setDrawOpacity(0.2f);

	const bool showSuspended = showTitle && m_isSuspended;
	m_suspendedText->fadeTo(showSuspended ? 1.0f : 0.0f, showSuspended ? titleFadeInSeconds : titleFadeOutSeconds);
}