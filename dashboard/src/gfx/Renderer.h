#pragma once

#include <SDL3/SDL.h>

class Tile;
class GameView;

// Owns the window, the GPU device and everything created from it (pipelines, sampler),
// and wraps the start and end of each frame.
class Renderer {
public:
	// Everything is laid out in these units. The window scales them to fit.
	static constexpr float designWidth = 1920.0f;
	static constexpr float designHeight = 1080.0f;

	// The dashboard's background. Game mode uses black instead (see setClearColor).
	static constexpr SDL_FColor defaultClearColor{ 0.05f, 0.06f, 0.09f, 1.0f };

	// Handed out by beginFrame(). Everything that gets drawn goes through it.
	class Frame {
	public:
		// Draws the tile if it's visible, switching the clip and pipeline as needed
		void draw(Tile& tile);

		// Draws the emulator's picture. Never clipped to the content area.
		void draw(GameView& view);

	private:
		friend class Renderer;

		SDL_GPUCommandBuffer* m_cmd = nullptr;
		SDL_GPURenderPass* m_pass = nullptr;
		SDL_GPUGraphicsPipeline* m_rectanglePipeline = nullptr;
		float m_pixelsPerUnit = 1.0f;
		SDL_Rect m_fullScissor{};
		SDL_Rect m_contentScissor{};
		bool m_contentScissorActive = false;
	};

	Renderer() = default;
	~Renderer();
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	// SDL_Init(SDL_INIT_VIDEO) must already have run
	bool initialize(const char* title, int width, int height);

	// Releases the GPU side. Anything holding GPU resources (the tiles, the GameView) must be destroyed first.
	// Safe to call twice, and the destructor calls it too.
	void shutdown();

	// Returns false when there is nothing to draw to this frame (for example a minimized window)
	bool beginFrame(Frame& frame, float contentClipTop);
	void endFrame(Frame& frame);

	// The color the frame is cleared to, which is also the color of the bars around the 16:9 area
	void setClearColor(SDL_FColor color) { m_clearColor = color; }

	// Window pixels per design unit. Text and images are rasterized at this scale.
	float computeRasterScale() const;

	SDL_Window* window() const { return m_window; }
	SDL_GPUDevice* device() const { return m_device; }
	SDL_GPUGraphicsPipeline* rectanglePipeline() const { return m_rectanglePipeline; }
	SDL_GPUGraphicsPipeline* imagePipeline() const { return m_imagePipeline; }
	SDL_GPUGraphicsPipeline* textPipeline() const { return m_textPipeline; }
	SDL_GPUGraphicsPipeline* gamePipeline() const { return m_gamePipeline; }
	SDL_GPUSampler* sampler() const { return m_sampler; }
	SDL_GPUSampler* pixelSampler() const { return m_pixelSampler; }

private:
	bool createPipelines();
	SDL_GPUShader* loadShaderFromFile(SDL_GPUShaderStage stage, const char* filename, const char* entryPointName, Uint32 numUniformBuffers, Uint32 numSamplers);

	SDL_Window* m_window = nullptr;
	SDL_GPUDevice* m_device = nullptr;
	SDL_GPUGraphicsPipeline* m_rectanglePipeline = nullptr;
	SDL_GPUGraphicsPipeline* m_imagePipeline = nullptr;
	SDL_GPUGraphicsPipeline* m_textPipeline = nullptr;
	SDL_GPUGraphicsPipeline* m_gamePipeline = nullptr;
	SDL_GPUSampler* m_sampler = nullptr;
	SDL_GPUSampler* m_pixelSampler = nullptr; // nearest filtering, for the game picture
	SDL_FColor m_clearColor = defaultClearColor;
};
