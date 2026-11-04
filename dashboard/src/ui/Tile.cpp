#include "ui/Tile.h"
#include <algorithm>

struct TileUniforms {
	float centerX;
	float centerY;
	float halfWidth;
	float halfHeight;
	float color[4];
	float borderColor[4];
	float cornerRadius;
	float borderWidth;
	float pixelsPerUnit;
	float alignmentPad;
	float viewportWidth;
	float viewportHeight;
    float padding[2];
};

static_assert(sizeof(TileUniforms) == 80, "TileUniforms must match the MSL struct");


Tile::Tile(float x_, float y_, float width_, float height_, SDL_FColor color_) : x(x_), y(y_), width(width_), height(height_), color(color_) {}


void Tile::Draw(SDL_GPUCommandBuffer* cmdBuffer, SDL_GPURenderPass* renderPass, float pixelsPerUnit, float designWidth, float designHeight) const {
	TileUniforms uniforms{};
	
	uniforms.centerX = x + (width / 2.0f);
	uniforms.centerY = y + (height / 2.0f);
	uniforms.halfWidth = width / 2.0f;
	uniforms.halfHeight = height / 2.0f;
	
	uniforms.color[0] = color.r;
    uniforms.color[1] = color.g;
    uniforms.color[2] = color.b;
    uniforms.color[3] = color.a * drawOpacity;

	const SDL_FColor drawBorderColor = focused ? SDL_FColor{ 1.0f, 1.0f, 1.0f, 1.0f } : borderColor;
	const float drawBorderWidth = focused ? std::max(borderWidth, focusBorderWidth) : borderWidth;

	uniforms.borderColor[0] = drawBorderColor.r;
	uniforms.borderColor[1] = drawBorderColor.g;
	uniforms.borderColor[2] = drawBorderColor.b;
	uniforms.borderColor[3] = drawBorderColor.a * drawOpacity;
	uniforms.borderWidth = drawBorderWidth;

	uniforms.cornerRadius = cornerRadius;
	uniforms.pixelsPerUnit = pixelsPerUnit;
	uniforms.alignmentPad = 0.0f;
	
	uniforms.viewportWidth = designWidth;
	uniforms.viewportHeight = designHeight;

	SDL_PushGPUVertexUniformData(cmdBuffer, 0, &uniforms, sizeof(uniforms));
	SDL_PushGPUFragmentUniformData(cmdBuffer, 0, &uniforms, sizeof(uniforms));

	SDL_DrawGPUPrimitives(renderPass, 6, 1, 0, 0);
}
