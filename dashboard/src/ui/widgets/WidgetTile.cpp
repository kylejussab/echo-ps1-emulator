#include "ui/widgets/WidgetTile.h"
#include "ui/TextTile.h"

WidgetTile::WidgetTile(float x_, float y_, float width_, float height_, SDL_GPUGraphicsPipeline* rectanglePipeline)
	: Tile(x_, y_, width_, height_), m_rectanglePipeline(rectanglePipeline) {}

WidgetTile* WidgetTile::setUpdateInterval(float seconds) {
	m_updateInterval = seconds > 0.0f ? seconds : 0.0f;
	return this;
}

void WidgetTile::setRasterScale(float scale) {
	m_rasterScale = scale > 0.0f ? scale : 1.0f;
	for (auto& child : m_children) child->setRasterScale(m_rasterScale);
}

// Dashboard calls this on first build and again after every window resize
void WidgetTile::build(SDL_GPUDevice* gpuDevice, SDL_GPUCommandBuffer* uploadCmd) {
	m_device = gpuDevice;
	for (auto& child : m_children) child->build(gpuDevice, uploadCmd);
	layoutChildren();
}

void WidgetTile::update(float deltaSeconds) {
	if (m_updateInterval > 0.0f) {
		m_updateTimer += deltaSeconds;
		if (m_updateTimer >= m_updateInterval) {
			m_updateTimer = 0.0f;
			m_updatePending = true;
		}
	}

	// Waits for build() to have run, because refresh() needs the GPU device
	if (m_updatePending && m_device) {
		m_updatePending = false;

		SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_device);
		if (uploadCmd) {
			refresh(m_device, uploadCmd);
			SDL_SubmitGPUCommandBuffer(uploadCmd);
		}
	}

	for (auto& child : m_children) {
		// The widget fades with its tab, so its children fade with the widget.
		// TextTile takes its fade through setOpacity, everything else through draw opacity.
		child->setDrawOpacity(drawOpacity);
		if (auto* text = dynamic_cast<TextTile*>(child.get())) text->setOpacity(drawOpacity);

		child->update(deltaSeconds);
	}

	layoutChildren();
}

void WidgetTile::Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth, float designHeight) const {
	// Background, border and focus ring
	Tile::Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);

	for (const auto& child : m_children) {
		if (!child->isVisible()) continue;

		// The previous child left the text pipeline bound, and every child draws a rectangle first
		SDL_BindGPUGraphicsPipeline(renderPass, m_rectanglePipeline);
		child->Draw(cmdBuffer, renderPass, pixelsPerUnit, designWidth, designHeight);
	}
}
