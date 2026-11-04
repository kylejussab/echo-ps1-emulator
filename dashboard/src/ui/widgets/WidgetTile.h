#pragma once

#include "ui/Tile.h"
#include <memory>
#include <utility>
#include <vector>

// A tile made of child tiles (icons, text...) whose content refreshes on a timer, or on demand.
// Subclasses create their children in the constructor, then fill in refresh() and layoutChildren().
class WidgetTile : public Tile {
public:
	WidgetTile(float x, float y, float width, float height, SDL_GPUGraphicsPipeline* rectanglePipeline);

	// How often refresh() runs by itself, in seconds. 0 means only when requestUpdate() is called.
	WidgetTile* setUpdateInterval(float seconds);

	// Forces a refresh on the next update(), whatever the timer says
	void requestUpdate() { m_updatePending = true; }

	void build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) override;
	void setRasterScale(float scale) override;
	void update(float deltaSeconds) override;
	void Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth = 1920.0f, float designHeight = 1080.0f) const override;

protected:
	// Pull fresh data and apply it to the children. If a child's texture changes (new text, new icon),
	// build() it here with the command buffer you are given. Called from update(), never from the draw.
	virtual void refresh(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) {}

	// Position the children relative to this tile. Called every frame, so it follows the widget if it ever moves.
	virtual void layoutChildren() {}

	// Creates a child owned by this widget. Takes the same arguments as the child's constructor.
	template <typename T, typename... Args>
	T* addChild(Args&&... args) {
		auto child = std::make_unique<T>(std::forward<Args>(args)...);
		child->setRasterScale(m_rasterScale);
		T* raw = child.get();
		m_children.push_back(std::move(child));
		return raw;
	}

private:
	std::vector<std::unique_ptr<Tile>> m_children;
	SDL_GPUGraphicsPipeline* m_rectanglePipeline;
	SDL_GPUDevice* m_device = nullptr;

	float m_rasterScale = 1.0f;
	float m_updateInterval = 0.0f;
	float m_updateTimer = 0.0f;
	bool m_updatePending = true; // so the first update() fills the widget in before it is ever drawn
};
