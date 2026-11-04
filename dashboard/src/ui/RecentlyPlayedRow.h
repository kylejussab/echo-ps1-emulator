#pragma once

#include <memory>
#include <vector>
#include "gfx/Renderer.h"
#include "library/GameLibrary.h"
#include "ui/GameTile.h"
#include "ui/TextTile.h"
#include "ui/FocusableRow.h"
#include "ui/IconTile.h"

// The "Recently Played" tab: a row of games where the selected one is large and the row slides under it,
// with the selected game's title underneath.
class RecentlyPlayedRow : public FocusableRow {
public:
	explicit RecentlyPlayedRow(Renderer& renderer) : m_renderer(renderer) {}

	// Builds one tile per recently played game. Replaces whatever was there.
	void build(const GameLibrary& library, float rasterScale);

	// Destroys every tile (they hold GPU resources) and puts the selection back at the start
	void clear();

	void update(float deltaSeconds);
	void draw(Renderer::Frame& frame);

	// Re-rasterizes the title after the window size changed
	void setRasterScale(float rasterScale);

	// The tab fade. 0 hides the whole row.
	void setOpacity(float opacity);

	bool empty() const { return m_tiles.empty(); }

	// The game that is currently selected, or null if there are none
	const GameInfo* selectedGame() const { return m_selectedIndex < m_tiles.size() ? &m_tiles[m_selectedIndex]->getInfo() : nullptr; }
		GameTile* selectedTile() { return m_selectedIndex < m_tiles.size() ? m_tiles[m_selectedIndex].get() : nullptr; }

	size_t size() const override { return m_tiles.size(); }
	void moveHorizontal(int delta) override;
	bool moveVertical(int) override { return false; }   // one row, so focus always leaves
	void jumpToFirst() override;
	void jumpToLast() override;
	float anchorX() const override;
	void setFocused(bool focused, bool animate) override;

	void setTimePlayedSeconds(const std::string& gameId, int64_t seconds) {
		for (auto& tile : m_tiles) {
			if (tile->getInfo().id == gameId) tile->setTimePlayedSeconds(seconds);
		}
	}

	// Whether the featured game is loaded in the console and paused behind the dashboard
	void setSuspended(bool isSuspended) { m_isSuspended = isSuspended; }

private:
	void layoutTiles(bool animate);
	void select(size_t index);
	void updateFeaturedTitle();
	void applyOpacity();

	Renderer& m_renderer;
	float m_rasterScale = 1.0f;
	float m_opacity = 1.0f;
	bool m_focused = false;

	std::vector<std::unique_ptr<GameTile>> m_tiles;
	std::unique_ptr<TextTile> m_featuredTitle;
	size_t m_selectedIndex = 0;
	size_t m_titleShownIndex = 0;

	std::unique_ptr<TextTile> m_playtimeText;
	std::string m_playtimeShownText;
	std::unique_ptr<IconTile> m_playtimeIcon;

	std::unique_ptr<TextTile> m_suspendedText;
	bool m_isSuspended = false;
};