#include "ui/CollectionGrid.h"
#include <algorithm>
#include <string>
#include "ui/Layout.h"

using namespace layout;

namespace {

constexpr float tileSize = 300.0f;
constexpr float tileGap = 52.0f;
constexpr size_t columns = 5;

// How many rows fit on screen. Moving past them scrolls the grid by one row.
constexpr size_t visibleRows = 2;

constexpr int labelFontSize = 26;
constexpr int headerFontSize = 24;

// Room above the first row for the headers
constexpr float gridOffsetY = 50.0f;

} // namespace

void CollectionGrid::build(const GameLibrary& library, float rasterScale) {
	clear();
	m_rasterScale = rasterScale;
	if (library.empty()) return;

	SDL_GPUDevice* device = m_renderer.device();
	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(device);

	addHeader(rowStartX, rowStartY, "Total Games: " + std::to_string(library.count()), uploadCmd);
	addHeader(1395.0f, rowStartY, "Sorting Order: Alphabetical Ascending", uploadCmd);

	for (const std::string& id : library.allGames()) {
		auto tile = std::make_unique<GameTile>(0.0f, 0.0f, tileSize, tileSize, library.at(id));

		tile->setLabel(m_renderer.textPipeline(), m_renderer.sampler(), fontSemiBold, labelFontSize);
		tile->setRasterScale(m_rasterScale);

		tile->setCornerRadius(tileCornerRadius)
		    ->setBorderColor(SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f})
		    ->setClippedToContent(true);

		tile->setBoxArt(m_renderer.rectanglePipeline(), m_renderer.imagePipeline(), m_renderer.sampler());

		tile->build(device, uploadCmd);

		m_tiles.push_back(std::move(tile));
	}

	SDL_SubmitGPUCommandBuffer(uploadCmd);

	// Position, size and border width come from the selection and scroll position
	layoutTiles(false);

	// Remember where each header sits relative to the first row, so it can scroll with the grid
	if (!m_tiles.empty()) {
		for (Header& header : m_headers) {
			header.offsetY = header.tile->getY() - m_tiles[0]->getY();
		}
	}

	applyOpacity();
}

void CollectionGrid::clear() {
	// The tiles hold GPU textures, so make sure the GPU is done with them first
	if ((!m_tiles.empty() || !m_headers.empty()) && m_renderer.device()) {
		SDL_WaitForGPUIdle(m_renderer.device());
	}

	m_headers.clear();
	m_tiles.clear();
	m_selectedIndex = 0;
	m_topRow = 0;
}

void CollectionGrid::addHeader(float x, float y, const std::string& text, SDL_GPUCommandBuffer* uploadCmd) {
	auto tile = std::make_unique<TextTile>(x, y, m_renderer.textPipeline(), m_renderer.sampler());
	tile->setRasterScale(m_rasterScale);

	tile->setText(text)
	    ->setFont(fontRegular, headerFontSize)
	    ->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f})
	    ->setInteractable(false)
	    ->setClippedToContent(true)
	    ->build(m_renderer.device(), uploadCmd);

	m_headers.push_back({ std::move(tile), 0.0f });
}

void CollectionGrid::update(float deltaSeconds) {
	for (auto& tile : m_tiles) tile->update(deltaSeconds);
	for (Header& header : m_headers) header.tile->update(deltaSeconds);

	updateHeaders();
}

void CollectionGrid::draw(Renderer::Frame& frame) {
	for (Header& header : m_headers) frame.draw(*header.tile);
	for (auto& tile : m_tiles) frame.draw(*tile);
}

void CollectionGrid::setRasterScale(float rasterScale) {
	m_rasterScale = rasterScale;
	if (m_tiles.empty() && m_headers.empty()) return;

	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());
	for (auto& tile : m_tiles) {
		tile->setRasterScale(m_rasterScale);
		tile->build(m_renderer.device(), uploadCmd);
	}
	for (Header& header : m_headers) {
		header.tile->setRasterScale(m_rasterScale);
		header.tile->build(m_renderer.device(), uploadCmd);
	}
	SDL_SubmitGPUCommandBuffer(uploadCmd);
}

void CollectionGrid::setOpacity(float opacity) {
	if (opacity == m_opacity) return;
	m_opacity = opacity;
	applyOpacity();
}

void CollectionGrid::applyOpacity() {
	// Fully faded tiles are hidden so they cost nothing to draw
	for (auto& tile : m_tiles) {
		tile->setDrawOpacity(m_opacity);
		tile->setVisible(m_opacity > 0.0f);
	}
	for (Header& header : m_headers) {
		header.tile->setOpacity(m_opacity);
		header.tile->setVisible(m_opacity > 0.0f);
	}
}

void CollectionGrid::setFocused(bool focused, bool animate) {
	m_focused = focused;
	layoutTiles(animate);
}

void CollectionGrid::layoutTiles(bool animate) {
	const float step = tileSize + tileGap;

	for (size_t i = 0; i < m_tiles.size(); ++i) {
		const float column = static_cast<float>(i % columns);
		const float visibleRow = static_cast<float>(i / columns) - static_cast<float>(m_topRow);

		// Only the selected tile, and only while the grid itself has focus, gets its border and title
		const bool hasFocus = m_focused && (i == m_selectedIndex);

		GameTile* tile = m_tiles[i].get();
		tile->setTarget(rowStartX + column * step, rowStartY + gridOffsetY + visibleRow * step,
		                tileSize, tileSize, hasFocus ? selectedBorderWidth : 0.0f);

		tile->setLabelVisible(hasFocus, animate);

		if (!animate) tile->snapToTarget();
	}
}

void CollectionGrid::select(size_t index) {
	if (m_tiles.empty()) return;

	index = std::min(index, m_tiles.size() - 1);
	if (index == m_selectedIndex) return;

	m_selectedIndex = index;

	// Scroll the least amount needed to keep the selected row on screen
	const size_t selectedRow = index / columns;
	if (selectedRow < m_topRow) {
		m_topRow = selectedRow;
	}
	else if (selectedRow >= m_topRow + visibleRows) {
		m_topRow = selectedRow - visibleRows + 1;
	}

	layoutTiles(true);
}

void CollectionGrid::moveHorizontal(int delta) {
	const size_t column = m_selectedIndex % columns;

	// select() clamps to the last tile, so a partly filled last row is safe
	if (delta < 0 && column > 0) select(m_selectedIndex - 1);
	else if (delta > 0 && column + 1 < columns) select(m_selectedIndex + 1);
}

bool CollectionGrid::moveVertical(int delta) {
	if (m_tiles.empty()) return false;

	const size_t row = m_selectedIndex / columns;
	const size_t lastRow = (m_tiles.size() - 1) / columns;

	// Going down into a shorter last row lands on its last tile (clamped by select)
	if (delta > 0 && row < lastRow) select(m_selectedIndex + columns);
	else if (delta < 0 && row > 0) select(m_selectedIndex - columns);
	else return false;

	return true;
}

void CollectionGrid::jumpToFirst() {
	select(0);
}

void CollectionGrid::jumpToLast() {
	if (m_tiles.empty()) return;
	select(m_tiles.size() - 1);
}

float CollectionGrid::anchorX() const {
	if (m_tiles.empty()) return 0.0f;
	return m_tiles[m_selectedIndex]->getCenterX();
}

void CollectionGrid::updateHeaders() {
	if (m_tiles.empty()) return;

	const float firstTileY = m_tiles[0]->getY();
	for (const Header& header : m_headers) {
		header.tile->setPosition(header.tile->getX(), firstTileY + header.offsetY);
	}
}