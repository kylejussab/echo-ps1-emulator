#pragma once

#include <memory>
#include <optional>
#include <vector>
#include "gfx/Renderer.h"
#include "ui/Tile.h"
#include "ui/TabFader.h"

class TextTile;
class IconTile;

// The two tabs, and the search and trophy icons beside them.
class NavBar {
public:
	explicit NavBar(Renderer& renderer) : m_renderer(renderer) {}

	void build(float rasterScale);
	void clear();

	void update(float deltaSeconds);
	void draw(Renderer::Frame& frame);
	void setRasterScale(float rasterScale);

	// The active tab gets the bold font
	void setActiveTab(Tab tab);

	// Which tab this tile is, or nothing if it isn't a tab
	std::optional<Tab> tabOf(const Tile* tile) const;

	// For the focus manager
	Tile* recentlyPlayedTab() const;
	Tile* collectionTab() const;
	Tile* searchIcon() const;
	Tile* trophiesIcon() const;

private:
	TextTile* makeTab(float x, float y, const char* text, const char* font, SDL_GPUCommandBuffer* uploadCmd);
	IconTile* makeIcon(float x, float y, const char* svgPath, SDL_GPUCommandBuffer* uploadCmd);

	Renderer& m_renderer;
	float m_rasterScale = 1.0f;

	std::vector<std::unique_ptr<Tile>> m_tiles; // draw order
	TextTile* m_recentlyPlayedTab = nullptr;    // non-owning, owned by m_tiles
	TextTile* m_collectionTab = nullptr;
	IconTile* m_searchIcon = nullptr;
	IconTile* m_trophiesIcon = nullptr;
};