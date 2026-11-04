#pragma once

#include <functional>
#include <memory>
#include <vector>
#include "gfx/Renderer.h"
#include "ui/Tile.h"
#include "ui/widgets/ControllerWidget.h"

// The widgets along the bottom of the Recently Played tab: the trophies placeholder and the controller.
class WidgetRow {
public:
	explicit WidgetRow(Renderer& renderer) : m_renderer(renderer) {}

	// Where the controller widget gets its data on every refresh
	void build(float rasterScale, std::function<std::vector<ControllerStatus>()> controllerStatusSource);
	void clear();

	void update(float deltaSeconds);
	void draw(Renderer::Frame& frame);
	void setRasterScale(float rasterScale);

	// Fades the widgets, and hides them completely at 0
	void setOpacity(float opacity);

	// Call when a controller is added or removed
	void requestControllerUpdate();

	// For the focus manager. The controller widget isn't interactable, so it has no accessor.
	Tile* trophiesTile() const { return m_trophiesTile; }

private:
	Renderer& m_renderer;
	float m_rasterScale = 1.0f;

	std::vector<std::unique_ptr<Tile>> m_tiles; // draw order
	Tile* m_trophiesTile = nullptr;             // non-owning, owned by m_tiles
	ControllerWidget* m_controllerWidget = nullptr;
};