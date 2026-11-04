#pragma once

#include "ui/Tile.h"
#include "ui/IconTile.h"
#include "ui/TextTile.h"
#include <memory>
#include <string>
#include <vector>

// What a row does when it is picked. Dashboard decides what each one means.
enum class MenuAction {
    CloseGame,
	PlayDisc,
	ManageSaves,
    ViewTrophies,
	GameInformation,
	ToggleFavorite
};

struct MenuRowInfo {
	MenuAction action;
	std::string label;
	std::string iconPath;                                // svg, relative to the app's base path
	SDL_FColor iconColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	int argument = 0;                                    // for rows that need a number, like which disc to play
};

class MenuTile : public Tile {
public:
	MenuTile(SDL_GPUGraphicsPipeline* rectanglePipeline, SDL_GPUGraphicsPipeline* textPipeline, SDL_GPUSampler* sampler);

	// Call before setRows(), the rows pick the font up when they are created
	MenuTile* setFont(const std::string& fontPath, int fontSize);

	// Replaces every row and resizes the menu to fit them. The selection goes back to the first row.
	// Call build() afterwards so the icons and text get rasterized.
	MenuTile* setRows(std::vector<MenuRowInfo> rowInfos);

	// Places the menu to the right of the anchor tile, or to the left if there is no room.
	// Use this instead of setPosition(), because the rows have to move with the menu.
	void openBeside(const Tile& anchor);

	// Moves the selection up (negative) or down (positive). Stops at the first and last row.
	void moveSelection(int step);

	// The row that is selected, or nullptr when there are no rows
	const MenuRowInfo* selectedRow() const;

	void build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) override;
	void setRasterScale(float scale) override;
	void update(float deltaSeconds) override;
	void Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth = 1920.0f, float designHeight = 1080.0f) const override;

private:
	struct Row {
		MenuRowInfo info;
		std::unique_ptr<Tile> highlight; // fill and focus border, only drawn while the row is selected
		std::unique_ptr<IconTile> icon;
		std::unique_ptr<TextTile> label;
	};

	void layout();
	void applySelection();

	SDL_GPUGraphicsPipeline* m_rectanglePipeline;
	SDL_GPUGraphicsPipeline* m_textPipeline;
	SDL_GPUSampler* m_sampler;

	std::string m_fontPath;
	int m_fontSize = 24;
	float m_rasterScale = 1.0f;

	std::vector<Row> m_rows;
	int m_selectedIndex = 0;
};