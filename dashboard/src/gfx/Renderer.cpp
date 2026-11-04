// Renderer.cpp
#include "gfx/Renderer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include "gfx/GameView.h"
#include "ui/Tile.h"

Renderer::~Renderer() {
	shutdown();
}

bool Renderer::initialize(const char* title, int width, int height) {
	m_window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (!m_window) return false;

	SDL_SetWindowAspectRatio(m_window, 16.0f / 9.0f, 16.0f / 9.0f);

	m_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, true, nullptr);
	if (!m_device) return false;

	if (!SDL_ClaimWindowForGPUDevice(m_device, m_window)) return false;

	return createPipelines();
}

void Renderer::shutdown() {
	if (m_device) {
		SDL_WaitForGPUIdle(m_device);

		if (m_rectanglePipeline) SDL_ReleaseGPUGraphicsPipeline(m_device, m_rectanglePipeline);
		if (m_imagePipeline)     SDL_ReleaseGPUGraphicsPipeline(m_device, m_imagePipeline);
		if (m_textPipeline)      SDL_ReleaseGPUGraphicsPipeline(m_device, m_textPipeline);
		if (m_gamePipeline)      SDL_ReleaseGPUGraphicsPipeline(m_device, m_gamePipeline);
		if (m_sampler)           SDL_ReleaseGPUSampler(m_device, m_sampler);
		if (m_pixelSampler)      SDL_ReleaseGPUSampler(m_device, m_pixelSampler);

		if (m_window) SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
		SDL_DestroyGPUDevice(m_device);
	}

	if (m_window) SDL_DestroyWindow(m_window);

	m_rectanglePipeline = nullptr;
	m_imagePipeline = nullptr;
	m_textPipeline = nullptr;
	m_gamePipeline = nullptr;
	m_sampler = nullptr;
	m_pixelSampler = nullptr;
	m_device = nullptr;
	m_window = nullptr;
}

SDL_GPUShader* Renderer::loadShaderFromFile(SDL_GPUShaderStage stage, const char* filename, const char* entryPointName, Uint32 numUniformBuffers, Uint32 numSamplers) {
	const char* basePath = SDL_GetBasePath();
	if (!basePath) return nullptr;

	std::string fullPath = std::string(basePath) + filename;

	size_t codeSize = 0;
	void* codeData = SDL_LoadFile(fullPath.c_str(), &codeSize);
	if (!codeData) {
		std::cout << "Failed to load shader file: " << fullPath << std::endl;
		return nullptr;
	}

	SDL_GPUShaderCreateInfo shaderInfo{};
	shaderInfo.code = static_cast<const Uint8*>(codeData);
	shaderInfo.code_size = codeSize;
	shaderInfo.entrypoint = entryPointName;
	shaderInfo.format = SDL_GPU_SHADERFORMAT_MSL;
	shaderInfo.stage = stage;
	shaderInfo.num_uniform_buffers = numUniformBuffers;
	shaderInfo.num_samplers = numSamplers;

	SDL_GPUShader* shader = SDL_CreateGPUShader(m_device, &shaderInfo);
	SDL_free(codeData);
	return shader;
}

bool Renderer::createPipelines() {
	// --- Rectangle pipeline ---
	SDL_GPUShader* vertexShader = loadShaderFromFile(SDL_GPU_SHADERSTAGE_VERTEX, "shaders/tile.msl", "vertexMain", 1, 0);
	SDL_GPUShader* fragmentShader = loadShaderFromFile(SDL_GPU_SHADERSTAGE_FRAGMENT, "shaders/tile.msl", "fragmentMain", 1, 0);

	if (!vertexShader || !fragmentShader) {
		if (vertexShader) SDL_ReleaseGPUShader(m_device, vertexShader);
		if (fragmentShader) SDL_ReleaseGPUShader(m_device, fragmentShader);
		return false;
	}

	SDL_GPUColorTargetDescription colorTargetDesc{};
	colorTargetDesc.format = SDL_GetGPUSwapchainTextureFormat(m_device, m_window);
	colorTargetDesc.blend_state.enable_blend = true;
	colorTargetDesc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	colorTargetDesc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	colorTargetDesc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
	colorTargetDesc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	colorTargetDesc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	colorTargetDesc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

	SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.vertex_shader = vertexShader;
	pipelineInfo.fragment_shader = fragmentShader;
	pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	pipelineInfo.target_info.color_target_descriptions = &colorTargetDesc;
	pipelineInfo.target_info.num_color_targets = 1;

	m_rectanglePipeline = SDL_CreateGPUGraphicsPipeline(m_device, &pipelineInfo);

	// --- Box art pipeline: same vertex shader, but the fragment shader samples the image ---
	SDL_GPUShader* imageFragmentShader = loadShaderFromFile(SDL_GPU_SHADERSTAGE_FRAGMENT, "shaders/tile.msl", "fragmentImage", 1, 1);
	if (imageFragmentShader) {
		SDL_GPUGraphicsPipelineCreateInfo imagePipelineInfo = pipelineInfo;
		imagePipelineInfo.fragment_shader = imageFragmentShader;
		m_imagePipeline = SDL_CreateGPUGraphicsPipeline(m_device, &imagePipelineInfo);
		SDL_ReleaseGPUShader(m_device, imageFragmentShader);
	}

	SDL_ReleaseGPUShader(m_device, vertexShader);
	SDL_ReleaseGPUShader(m_device, fragmentShader);

	if (!m_rectanglePipeline) return false;
	if (!m_imagePipeline) return false;

	// --- Text pipeline ---
	SDL_GPUShader* texVertShader = loadShaderFromFile(SDL_GPU_SHADERSTAGE_VERTEX, "shaders/texture.msl", "vertexMain", 1, 0);
	SDL_GPUShader* texFragShader = loadShaderFromFile(SDL_GPU_SHADERSTAGE_FRAGMENT, "shaders/texture.msl", "fragmentMain", 1, 1);

	if (!texVertShader || !texFragShader) {
		if (texVertShader) SDL_ReleaseGPUShader(m_device, texVertShader);
		if (texFragShader) SDL_ReleaseGPUShader(m_device, texFragShader);
		return false;
	}

	SDL_GPUGraphicsPipelineCreateInfo texPipelineInfo = pipelineInfo; // Copy base settings
	texPipelineInfo.vertex_shader = texVertShader;
	texPipelineInfo.fragment_shader = texFragShader;
	m_textPipeline = SDL_CreateGPUGraphicsPipeline(m_device, &texPipelineInfo);

	SDL_ReleaseGPUShader(m_device, texVertShader);
	SDL_ReleaseGPUShader(m_device, texFragShader);

	if (!m_textPipeline) return false;

	// --- Game picture pipeline: the emulator's frame as one opaque rectangle, with no vertex buffers ---
	SDL_GPUShader* gameVertexShader = loadShaderFromFile(SDL_GPU_SHADERSTAGE_VERTEX, "shaders/game.msl", "gameVertex", 1, 0);
	SDL_GPUShader* gameFragmentShader = loadShaderFromFile(SDL_GPU_SHADERSTAGE_FRAGMENT, "shaders/game.msl", "gameFragment", 0, 1);

	if (!gameVertexShader || !gameFragmentShader) {
		if (gameVertexShader) SDL_ReleaseGPUShader(m_device, gameVertexShader);
		if (gameFragmentShader) SDL_ReleaseGPUShader(m_device, gameFragmentShader);
		return false;
	}

	SDL_GPUColorTargetDescription gameTargetDesc = colorTargetDesc;
	gameTargetDesc.blend_state.enable_blend = false; // the picture is opaque

	SDL_GPUGraphicsPipelineCreateInfo gamePipelineInfo = pipelineInfo; // Copy base settings
	gamePipelineInfo.vertex_shader = gameVertexShader;
	gamePipelineInfo.fragment_shader = gameFragmentShader;
	gamePipelineInfo.target_info.color_target_descriptions = &gameTargetDesc;
	m_gamePipeline = SDL_CreateGPUGraphicsPipeline(m_device, &gamePipelineInfo);

	SDL_ReleaseGPUShader(m_device, gameVertexShader);
	SDL_ReleaseGPUShader(m_device, gameFragmentShader);

	if (!m_gamePipeline) return false;

	// --- Sampler ---
	SDL_GPUSamplerCreateInfo samplerInfo{};
	samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
	samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
	samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
	samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.max_lod = 1000.0f;
	m_sampler = SDL_CreateGPUSampler(m_device, &samplerInfo);

	// --- Pixel sampler: same, but nearest filtering, to keep the game's pixels sharp. Change to LINEAR here for a softer picture. ---
	SDL_GPUSamplerCreateInfo pixelSamplerInfo = samplerInfo;
	pixelSamplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
	pixelSamplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
	pixelSamplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	m_pixelSampler = SDL_CreateGPUSampler(m_device, &pixelSamplerInfo);

	return m_sampler != nullptr && m_pixelSampler != nullptr;
}

float Renderer::computeRasterScale() const {
	int pixelW = 0, pixelH = 0;
	SDL_GetWindowSizeInPixels(m_window, &pixelW, &pixelH);
	return std::min(pixelW / designWidth, pixelH / designHeight);
}

bool Renderer::beginFrame(Frame& frame, float contentClipTop) {
	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
	SDL_GPUTexture* swapchainTexture = nullptr;
	Uint32 swapchainWidth = 0, swapchainHeight = 0;

	if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, m_window, &swapchainTexture, &swapchainWidth, &swapchainHeight) || !swapchainTexture) {
		SDL_SubmitGPUCommandBuffer(cmd);
		return false;
	}

	SDL_GPUColorTargetInfo colorTargetInfo{};
	colorTargetInfo.texture = swapchainTexture;
	colorTargetInfo.clear_color = m_clearColor;
	colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
	colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

	SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTargetInfo, 1, nullptr);

	const float pixelsPerUnit = std::min(static_cast<float>(swapchainWidth) / designWidth,
	                                     static_cast<float>(swapchainHeight) / designHeight);
	SDL_GPUViewport viewport{};
	viewport.w = designWidth * pixelsPerUnit;
	viewport.h = designHeight * pixelsPerUnit;
	viewport.x = (static_cast<float>(swapchainWidth) - viewport.w) / 2.0f;
	viewport.y = (static_cast<float>(swapchainHeight) - viewport.h) / 2.0f;
	viewport.min_depth = 0.0f;
	viewport.max_depth = 1.0f;
	SDL_SetGPUViewport(pass, &viewport);

	const int contentClipTopPixels = static_cast<int>(std::ceil(viewport.y + contentClipTop * pixelsPerUnit));

	frame = Frame{};
	frame.m_cmd = cmd;
	frame.m_pass = pass;
	frame.m_rectanglePipeline = m_rectanglePipeline;
	frame.m_pixelsPerUnit = pixelsPerUnit;
	frame.m_fullScissor = SDL_Rect{ 0, 0, static_cast<int>(swapchainWidth), static_cast<int>(swapchainHeight) };
	frame.m_contentScissor = SDL_Rect{ 0, contentClipTopPixels, static_cast<int>(swapchainWidth), static_cast<int>(swapchainHeight) - contentClipTopPixels };
	return true;
}

void Renderer::endFrame(Frame& frame) {
	SDL_EndGPURenderPass(frame.m_pass);
	SDL_SubmitGPUCommandBuffer(frame.m_cmd);
	frame = Frame{};
}

void Renderer::Frame::draw(Tile& tile) {
	if (!tile.isVisible()) return;

	// Only touch the scissor when it actually changes
	if (tile.isClippedToContent() != m_contentScissorActive) {
		m_contentScissorActive = tile.isClippedToContent();
		SDL_SetGPUScissor(m_pass, m_contentScissorActive ? &m_contentScissor : &m_fullScissor);
	}

	SDL_BindGPUGraphicsPipeline(m_pass, m_rectanglePipeline);
	tile.Draw(m_cmd, m_pass, m_pixelsPerUnit, designWidth, designHeight);
}

void Renderer::Frame::draw(GameView& view) {
	// The picture is never clipped to the content area
	if (m_contentScissorActive) {
		m_contentScissorActive = false;
		SDL_SetGPUScissor(m_pass, &m_fullScissor);
	}

	view.draw(m_cmd, m_pass);
}
