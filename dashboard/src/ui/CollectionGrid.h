#pragma once

#include <memory>
#include <vector>
#include "gfx/Renderer.h"
#include "library/GameLibrary.h"
#include "ui/GameTile.h"
#include "ui/TextTile.h"
#include "ui/FocusableRow.h"

// The "Your Collection" tab: a scrolling grid of every game, plus a few headers that scroll with it.
class CollectionGrid : public FocusableRow {
public:
	explicit CollectionGrid(Renderer& renderer) : m_renderer(renderer) {}

	// Builds the headers and one tile per game. Replaces whatever was there.
	void build(const GameLibrary& library, float rasterScale);

	// Destroys every tile (they hold GPU resources) and puts the selection back at the start
	void clear();

	void update(float deltaSeconds);
	void draw(Renderer::Frame& frame);

	// Re-rasterizes labels and headers after the window size changed
	void setRasterScale(float rasterScale);

	// The tab fade. 0 hides the whole grid.
	void setOpacity(float opacity);

	// Whether the grid has input focus. Only then does the selected tile get its border and title.
	void setFocused(bool focused);

	bool empty() const { return m_tiles.empty(); }

	// The game that is currently selected, or null if there are none
	const GameInfo* selectedGame() const { return m_selectedIndex < m_tiles.size() ? &m_tiles[m_selectedIndex]->getInfo() : nullptr; }
		GameTile* selectedTile() { return m_selectedIndex < m_tiles.size() ? m_tiles[m_selectedIndex].get() : nullptr; }

	size_t size() const override { return m_tiles.size(); }
	void moveHorizontal(int delta) override;
	bool moveVertical(int delta) override;
	void jumpToFirst() override;
	void jumpToLast() override;
	float anchorX() const override;
	void setFocused(bool focused, bool animate) override;

	void setTimePlayedSeconds(const std::string& gameId, int64_t seconds) {
		for (auto& tile : m_tiles) {
			if (tile->getInfo().id == gameId) tile->setTimePlayedSeconds(seconds);
		}
	}

private:
	struct Header {
		std::unique_ptr<TextTile> tile;
		float offsetY = 0.0f; // distance from the first tile's y
	};

	void addHeader(float x, float y, const std::string& text, SDL_GPUCommandBuffer* uploadCmd);
	void layoutTiles(bool animate);
	void select(size_t index);
	void updateHeaders();
	void applyOpacity();

	Renderer& m_renderer;
	float m_rasterScale = 1.0f;
	float m_opacity = 0.0f;
	bool m_focused = false;

	std::vector<std::unique_ptr<GameTile>> m_tiles;
	std::vector<Header> m_headers;
	size_t m_selectedIndex = 0;
	size_t m_topRow = 0; // first row currently shown. Scrolling changes this.
};