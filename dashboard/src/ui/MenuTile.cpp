#include "ui/MenuTile.h"
#include "gfx/Renderer.h"
#include <algorithm>
#include <utility>

namespace {

// Placeholder values in design units. Replace them with the Figma numbers.
constexpr float menuWidth = 320.0f;
constexpr float panelPaddingTop = 0.0f;
constexpr float panelPaddingBottom = 0.0f;
constexpr float panelPaddingSide = 0.0f;
constexpr float panelCornerRadius = 4.0f;

constexpr float rowHeight = 80.0f;
constexpr float rowGap = 0.0f;
constexpr float rowCornerRadius = 4.0f;

constexpr float iconSize = 20.0f;
constexpr float iconLeft = 20.0f;   // from the left edge of the row
constexpr float labelLeft = 60.0f;  // from the left edge of the row
constexpr float labelRight = 20.0f;

constexpr float anchorGap = 24.0f;   // space between the game tile and the menu
constexpr float screenMargin = 48.0f; // the menu never gets closer than this to the screen edge

const SDL_FColor panelColor{ 0.10f, 0.11f, 0.13f, 1.0f };
const SDL_FColor rowFocusedColor{ 0.0f, 0.0f, 0.0f, 0.0f };

} // namespace


MenuTile::MenuTile(SDL_GPUGraphicsPipeline* rectanglePipeline, SDL_GPUGraphicsPipeline* textPipeline, SDL_GPUSampler* sampler)
	: Tile(0.0f, 0.0f, menuWidth, 0.0f, panelColor),
	  m_rectanglePipeline(rectanglePipeline), m_textPipeline(textPipeline), m_sampler(sampler)
{
	setCornerRadius(panelCornerRadius);
}


MenuTile* MenuTile::setFont(const std::string& fontPath, int fontSize) {
	m_fontPath = fontPath;
	m_fontSize = fontSize;
	return this;
}


MenuTile* MenuTile::setRows(std::vector<MenuRowInfo> rowInfos) {
	m_rows.clear();
	m_selectedIndex = 0;

	const float labelMaxWidth = width - panelPaddingSide * 2.0f - labelLeft - labelRight;

	for (MenuRowInfo& info : rowInfos) {
		Row row;
		row.info = std::move(info);

		row.highlight = std::make_unique<Tile>(0.0f, 0.0f, 0.0f, 0.0f, rowFocusedColor);
		row.highlight->setCornerRadius(rowCornerRadius);

		row.icon = std::make_unique<IconTile>(0.0f, 0.0f, m_textPipeline, m_sampler);
		row.icon->setSvg(row.info.iconPath)->setIconSize(iconSize, iconSize)->setIconColor(row.info.iconColor)->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f});
		row.icon->setRasterScale(m_rasterScale);

		row.label = std::make_unique<TextTile>(0.0f, 0.0f, m_textPipeline, m_sampler);
		row.label->setText(row.info.label)->setFont(m_fontPath, m_fontSize)->setMaxWidth(labelMaxWidth)->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f});
		row.label->setRasterScale(m_rasterScale);

		m_rows.push_back(std::move(row));
	}

	// The height only depends on the row count, so the menu is the right size before anything is rasterized
	const float rowCount = static_cast<float>(m_rows.size());
	height = m_rows.empty() ? 0.0f : panelPaddingTop + panelPaddingBottom + rowCount * rowHeight + (rowCount - 1.0f) * rowGap;

	applySelection();
	layout();
	return this;
}


void MenuTile::openBeside(const Tile& anchor) {
	float menuX = anchor.getX() + anchor.getWidth() + anchorGap;

	// No room on the right (the last column of the grid), so it opens on the left instead
	if (menuX + width > Renderer::designWidth - screenMargin) {
		menuX = anchor.getX() - anchorGap - width;
	}

	float menuY = std::min(anchor.getY(), Renderer::designHeight - screenMargin - height);
	menuY = std::max(menuY, screenMargin);

	setPosition(menuX, menuY);
	layout();
}


void MenuTile::moveSelection(int step) {
	if (m_rows.empty()) return;

	m_selectedIndex = std::clamp(m_selectedIndex + step, 0, static_cast<int>(m_rows.size()) - 1);
	applySelection();
}


const MenuRowInfo* MenuTile::selectedRow() const {
	if (m_rows.empty()) return nullptr;
	return &m_rows[m_selectedIndex].info;
}


void MenuTile::applySelection() {
	for (int index = 0; index < static_cast<int>(m_rows.size()); ++index) {
		m_rows[index].highlight->setFocused(index == m_selectedIndex);
	}
}


// Puts every row's tiles where they belong. The text is centered with its real height, so this runs again after build().
void MenuTile::layout() {
	const float rowX = x + panelPaddingSide;
	float rowY = y + panelPaddingTop;

	for (Row& row : m_rows) {
		row.highlight->setPosition(rowX, rowY)->setSize(width - panelPaddingSide * 2.0f, rowHeight);
		row.icon->setPosition(rowX + iconLeft, rowY + (rowHeight - iconSize) / 2.0f);
		row.label->setPosition(rowX + labelLeft, rowY + (rowHeight - row.label->getHeight()) / 2.0f);

		rowY += rowHeight + rowGap;
	}
}


void MenuTile::build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) {
	for (Row& row : m_rows) {
		row.icon->build(gpuDevice, uploadCmd);
		row.label->build(gpuDevice, uploadCmd);
	}
	layout();
}


void MenuTile::setRasterScale(float scale) {
	m_rasterScale = scale > 0.0f ? scale : 1.0f;
	for (Row& row : m_rows) {
		row.icon->setRasterScale(m_rasterScale);
		row.label->setRasterScale(m_rasterScale);
	}
}


// The menu fades as one piece, so its draw opacity goes down to everything inside it
void MenuTile::update(float /*deltaSeconds*/) {
	for (Row& row : m_rows) {
		row.highlight->setDrawOpacity(drawOpacity);
		row.icon->setDrawOpacity(drawOpacity);
		row.label->setDrawOpacity(drawOpacity);
		row.label->setOpacity(drawOpacity);
	}
}


void MenuTile::Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth, float designHeight) const {
	Tile::Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);

	for (const Row& row : m_rows) {
		// The previous row's text left the text pipeline bound, and the highlight and the icon's background are plain rectangles
		SDL_BindGPUGraphicsPipeline(renderPass, m_rectanglePipeline);

		if (row.highlight->isFocused()) {
			row.highlight->Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);
		}
		row.icon->Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);
		row.label->Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);
	}
}