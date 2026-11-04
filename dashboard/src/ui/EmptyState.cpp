#include "ui/EmptyState.h"
#include <filesystem>
#include <string>
#include "ui/Layout.h"

using namespace layout;

void EmptyState::build(const GameLibrary& library, float rasterScale) {
	clear();
	m_rasterScale = rasterScale;
	if (!library.empty()) return;

	const std::string& folder = library.folder();
	std::error_code ec;
	const bool folderExists = !folder.empty() && std::filesystem::is_directory(folder, ec);

	std::string hint;
	if (folderExists)    hint = "No .cue files in: " + folder + ". Drag a different folder here to change it.";
	else if (folder.empty()) hint = "Drag your games folder onto this window.";
	else                 hint = "Looked in: " + folder + ". Drag your games folder onto this window to choose a new one.";

	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());

	m_message = makeText(rowStartX, rowStartY + 40.0f,
	                     folderExists ? "No games found" : "Couldn't find your games folder",
	                     fontSemiBold, 36, uploadCmd);
	m_detail = makeText(rowStartX, rowStartY + 100.0f, hint, fontRegular, 24, uploadCmd);

	SDL_SubmitGPUCommandBuffer(uploadCmd);
}

void EmptyState::clear() {
	if ((m_message || m_detail) && m_renderer.device()) SDL_WaitForGPUIdle(m_renderer.device());

	m_message.reset();
	m_detail.reset();
}

void EmptyState::update(float deltaSeconds) {
	if (m_message) m_message->update(deltaSeconds);
	if (m_detail) m_detail->update(deltaSeconds);
}

void EmptyState::draw(Renderer::Frame& frame) {
	if (m_message) frame.draw(*m_message);
	if (m_detail) frame.draw(*m_detail);
}

void EmptyState::setRasterScale(float rasterScale) {
	m_rasterScale = rasterScale;
	if (!m_message && !m_detail) return;

	SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_renderer.device());
	for (TextTile* tile : { m_message.get(), m_detail.get() }) {
		if (!tile) continue;
		tile->setRasterScale(m_rasterScale);
		tile->build(m_renderer.device(), uploadCmd);
	}
	SDL_SubmitGPUCommandBuffer(uploadCmd);
}

std::unique_ptr<TextTile> EmptyState::makeText(float x, float y, const std::string& text, const char* font, int fontSize,
                                               SDL_GPUCommandBuffer* uploadCmd) {
	auto tile = std::make_unique<TextTile>(x, y, m_renderer.textPipeline(), m_renderer.sampler());
	tile->setRasterScale(m_rasterScale);

	tile->setText(text)
	    ->setFont(font, fontSize)
	    ->setBackgroundColor(SDL_FColor{0.0f, 0.0f, 0.0f, 0.0f})
	    ->setInteractable(false)
	    ->build(m_renderer.device(), uploadCmd);

	return tile;
}