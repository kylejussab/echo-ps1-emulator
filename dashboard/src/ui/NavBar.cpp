#include "ui/NavBar.h"
#include "ui/TextTile.h"
#include "ui/IconTile.h"
#include "ui/Layout.h"

using namespace layout;

namespace {

constexpr int tabFontSize = 32;

} // namespace

void NavBar::build(float rasterScale) {
	clear();
	m_rasterScale = rasterScale;

	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());

	m_recentlyPlayedTab = makeTab(100.0f, 120.0f, "Recently Played", fontSemiBold, uploadCmd);
	m_collectionTab = makeTab(410.0f, 120.0f, "Your Collection", fontRegular, uploadCmd);
	m_searchIcon = makeIcon(1675.0f, 140.0f, "icons/search.svg", uploadCmd);
	m_trophiesIcon = makeIcon(1765.0f, 140.0f, "icons/trophy.svg", uploadCmd);

	SDL_SubmitGPUCommandBuffer(uploadCmd);
}

void NavBar::clear() {
	if (!m_tiles.empty() && m_renderer.device()) SDL_WaitForGPUIdle(m_renderer.device());

	m_tiles.clear();
	m_recentlyPlayedTab = nullptr;
	m_collectionTab = nullptr;
	m_searchIcon = nullptr;
	m_trophiesIcon = nullptr;
}

void NavBar::update(float deltaSeconds) {
	for (auto& tile : m_tiles) tile->update(deltaSeconds);
}

void NavBar::draw(Renderer::Frame& frame) {
	for (auto& tile : m_tiles) frame.draw(*tile);
}

void NavBar::setRasterScale(float rasterScale) {
	m_rasterScale = rasterScale;
	if (m_tiles.empty()) return;

	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());
	for (auto& tile : m_tiles) {
		tile->setRasterScale(m_rasterScale);
		tile->build(m_renderer.device(), uploadCmd);
	}
	SDL_SubmitGPUCommandBuffer(uploadCmd);
}

void NavBar::setActiveTab(Tab tab) {
	if (!m_recentlyPlayedTab || !m_collectionTab) return;

	// The active tab gets the bold font. Changing it re-rasterizes the text, which also changes its width.
	const bool recent = (tab == Tab::RecentlyPlayed);

	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());
	m_recentlyPlayedTab->setFont(recent ? fontSemiBold : fontRegular, tabFontSize)->build(m_renderer.device(), uploadCmd);
	m_collectionTab->setFont(recent ? fontRegular : fontSemiBold, tabFontSize)->build(m_renderer.device(), uploadCmd);
	SDL_SubmitGPUCommandBuffer(uploadCmd);
}

std::optional<Tab> NavBar::tabOf(const Tile* tile) const {
	if (!tile) return std::nullopt;
	if (tile == m_recentlyPlayedTab) return Tab::RecentlyPlayed;
	if (tile == m_collectionTab) return Tab::Collection;
	return std::nullopt;
}

Tile* NavBar::recentlyPlayedTab() const { return m_recentlyPlayedTab; }
Tile* NavBar::collectionTab() const { return m_collectionTab; }
Tile* NavBar::searchIcon() const { return m_searchIcon; }
Tile* NavBar::trophiesIcon() const { return m_trophiesIcon; }

TextTile* NavBar::makeTab(float x, float y, const char* text, const char* font, SDL_GPUCommandBuffer* uploadCmd) {
	auto tile = std::make_unique<TextTile>(x, y, m_renderer.textPipeline(), m_renderer.sampler());
	tile->setRasterScale(m_rasterScale);

	tile->setText(text)
	    ->setFont(font, tabFontSize)
	    ->setPadding(20.0f, 20.0f, 20.0f, 20.0f)
	    ->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f})
	    ->setCornerRadius(6.0f)
	    ->setBorder(2.0f)
	    ->setBorderColor(SDL_FColor{1.0f, 1.0f, 1.0f, 0.0f})
	    ->build(m_renderer.device(), uploadCmd);

	TextTile* raw = tile.get();
	m_tiles.push_back(std::move(tile));
	return raw;
}

IconTile* NavBar::makeIcon(float x, float y, const char* svgPath, SDL_GPUCommandBuffer* uploadCmd) {
	auto tile = std::make_unique<IconTile>(x, y, m_renderer.textPipeline(), m_renderer.sampler());
	tile->setRasterScale(m_rasterScale);

	tile->setSvg(svgPath)
	    ->setIconSize(32.0f, 32.0f)
	    ->setIconColor(SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f})
	    ->setPadding(12.0f, 12.0f, 12.0f, 12.0f)
	    ->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f})
	    ->setCornerRadius(6.0f)
	    ->setBorder(2.0f)
	    ->setBorderColor(SDL_FColor{1.0f, 1.0f, 1.0f, 0.0f})
	    ->build(m_renderer.device(), uploadCmd);

	IconTile* raw = tile.get();
	m_tiles.push_back(std::move(tile));
	return raw;
}