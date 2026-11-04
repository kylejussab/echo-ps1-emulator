#include "ui/WidgetRow.h"

void WidgetRow::build(float rasterScale, std::function<std::vector<ControllerStatus>()> controllerStatusSource) {
	clear();
	m_rasterScale = rasterScale;

	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());

	// Placeholder (custom tiles will be made for these)
	auto trophiesWidget = std::make_unique<Tile>(120.0f, 800.0f, 410.0f, 160.0f, SDL_FColor{0.0f, 0.0f, 0.0f, 0.8f});
	trophiesWidget->setCornerRadius(16.0f);
	m_trophiesTile = trophiesWidget.get();
	m_tiles.push_back(std::move(trophiesWidget));

	auto controllerWidget = std::make_unique<ControllerWidget>(565.0f, 800.0f, 160.0f, 160.0f,
	                                                           m_renderer.rectanglePipeline(), m_renderer.textPipeline(), m_renderer.sampler());
	controllerWidget->setStatusSource(std::move(controllerStatusSource))
	                ->setUpdateInterval(2.0f)
	                ->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, 0.8f})
	                ->setCornerRadius(16.0f)
	                ->setPadding(16.0f, 16.0f, 16.0f, 16.0f);
	controllerWidget->setRasterScale(m_rasterScale);
	controllerWidget->build(m_renderer.device(), uploadCmd);
	m_controllerWidget = controllerWidget.get();
	m_tiles.push_back(std::move(controllerWidget));

	SDL_SubmitGPUCommandBuffer(uploadCmd);
}

void WidgetRow::clear() {
	if (!m_tiles.empty() && m_renderer.device()) SDL_WaitForGPUIdle(m_renderer.device());

	m_tiles.clear();
	m_trophiesTile = nullptr;
	m_controllerWidget = nullptr;
}

void WidgetRow::update(float deltaSeconds) {
	for (auto& tile : m_tiles) tile->update(deltaSeconds);
}

void WidgetRow::draw(Renderer::Frame& frame) {
	for (auto& tile : m_tiles) frame.draw(*tile);
}

void WidgetRow::setRasterScale(float rasterScale) {
	m_rasterScale = rasterScale;
	if (!m_controllerWidget) return;

	// Only the controller widget has rasterized content. The trophies placeholder is a plain rectangle.
	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());
	m_controllerWidget->setRasterScale(m_rasterScale);
	m_controllerWidget->build(m_renderer.device(), uploadCmd);
	SDL_SubmitGPUCommandBuffer(uploadCmd);
}

void WidgetRow::setOpacity(float opacity) {
	for (auto& tile : m_tiles) {
		tile->setDrawOpacity(opacity);
		tile->setVisible(opacity > 0.0f);
	}

	// No controller, no widget. It keeps updating while hidden, so it comes back when one connects.
	if (m_controllerWidget && !m_controllerWidget->isConnected()) m_controllerWidget->setVisible(false);
}

void WidgetRow::requestControllerUpdate() {
	if (m_controllerWidget) m_controllerWidget->requestUpdate();
}