#include "Rasterizer.h"
#include <algorithm>

using namespace std;

void Rasterizer::setTexturePage(uint16_t baseX, uint16_t baseY, uint8_t colorDepth, uint8_t semiTransparency) {
	texturePageBaseX = baseX;
	texturePageBaseY = baseY;
	texturePageColorDepth = colorDepth;
	texturePageSemiTransparency = semiTransparency;
}

void Rasterizer::setTextureFlip(bool flipX, bool flipY) {
	textureFlipX = flipX;
	textureFlipY = flipY;
}

void Rasterizer::setDrawingArea(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom) {
	drawingAreaLeft = left;
	drawingAreaTop = top;
	drawingAreaRight = right;
	drawingAreaBottom = bottom;
}

void Rasterizer::setDrawingOffset(int16_t offsetX, int16_t offsetY) {
	drawingOffsetX = offsetX;
	drawingOffsetY = offsetY;
}

void Rasterizer::setDrawingAreaTopLeft(uint16_t left, uint16_t top) {
	drawingAreaLeft = left;
	drawingAreaTop = top;
}

void Rasterizer::setDrawingAreaBottomRight(uint16_t right, uint16_t bottom) {
	drawingAreaRight = right;
	drawingAreaBottom = bottom;
}

void Rasterizer::drawPolygon(const Vertex* vertices, int vertexCount,
                              bool isTextured, bool isSemiTransparent, bool isRawTexture, bool isGouraud,
                              uint16_t colorLookupTableX, uint16_t colorLookupTableY) {
	(void)isSemiTransparent; // reserved for future semi-transparency blending — not yet wired up
	(void)isRawTexture;      // reserved for future raw-texture (unlit) sampling — not yet wired up

	Vertex offsetVertices[4];
	for (int index = 0; index < vertexCount; index++) {
		offsetVertices[index] = vertices[index];
		offsetVertices[index].x += drawingOffsetX;
		offsetVertices[index].y += drawingOffsetY;
	}

	drawTriangle(offsetVertices[0], offsetVertices[1], offsetVertices[2], isTextured, isGouraud, colorLookupTableX, colorLookupTableY);

	// Quads are drawn as two triangles with a V1, V3, V2 winding, matching PS1 hardware.
	if (vertexCount == 4) {
		drawTriangle(offsetVertices[1], offsetVertices[3], offsetVertices[2], isTextured, isGouraud, colorLookupTableX, colorLookupTableY);
	}
}

void Rasterizer::drawRectangle(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t flatColor,
                                bool isTextured, bool isRawTexture,
                                uint8_t textureCoordinateU, uint8_t textureCoordinateV,
                                uint16_t colorLookupTableX, uint16_t colorLookupTableY) {
	(void)isRawTexture; // reserved for future raw-texture (unlit) sampling — not yet wired up

	int16_t originX = x + drawingOffsetX;
	int16_t originY = y + drawingOffsetY;

	for (uint16_t row = 0; row < height; row++) {
		for (uint16_t column = 0; column < width; column++) {
			int16_t pixelX = originX + column;
			int16_t pixelY = originY + row;

			if (pixelX < static_cast<int16_t>(drawingAreaLeft) || pixelX > static_cast<int16_t>(drawingAreaRight) ||
			    pixelY < static_cast<int16_t>(drawingAreaTop)  || pixelY > static_cast<int16_t>(drawingAreaBottom)) {
				continue;
			}

			uint16_t color;

			if (isTextured) {
				int textureCoordinateX = textureCoordinateU + (textureFlipX ? -column : column);
				int textureCoordinateY = textureCoordinateV + (textureFlipY ? -row : row);
				uint16_t texel = sampleTexture(textureCoordinateX, textureCoordinateY, colorLookupTableX, colorLookupTableY);

				if (texel == 0) continue;
				color = texel;
			} else {
				color = flatColor;
			}

			vram.writePixel(pixelX, pixelY, color);
		}
	}
}

void Rasterizer::fillRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color) {
	for (uint16_t row = 0; row < height; row++) {
		for (uint16_t column = 0; column < width; column++) {
			uint16_t pixelX = x + column;
			uint16_t pixelY = y + row;

			if (pixelX >= static_cast<int16_t>(drawingAreaLeft) && pixelX <= static_cast<int16_t>(drawingAreaRight) &&
			    pixelY >= static_cast<int16_t>(drawingAreaTop)  && pixelY <= static_cast<int16_t>(drawingAreaBottom)) {
				vram.writePixel(pixelX, pixelY, color);
			}
		}
	}
}

void Rasterizer::drawTriangle(const Vertex& vertex0, const Vertex& vertex1, const Vertex& vertex2,
                               bool isTextured, bool isGouraud,
                               uint16_t colorLookupTableX, uint16_t colorLookupTableY) {
	int16_t minX = min({vertex0.x, vertex1.x, vertex2.x});
	int16_t maxX = max({vertex0.x, vertex1.x, vertex2.x});
	int16_t minY = min({vertex0.y, vertex1.y, vertex2.y});
	int16_t maxY = max({vertex0.y, vertex1.y, vertex2.y});

	minX = max((int16_t)0, minX);
	maxX = min((int16_t)(Hardware::VRAM_WIDTH - 1), maxX);
	minY = max((int16_t)0, minY);
	maxY = min((int16_t)(Hardware::VRAM_HEIGHT - 1), maxY);

	clipToDrawingArea(minX, maxX, minY, maxY);

	int32_t areaTotal = edgeFunction(vertex0.x, vertex0.y, vertex1.x, vertex1.y, vertex2.x, vertex2.y);
	if (areaTotal == 0) return;

	for (int16_t pixelY = minY; pixelY <= maxY; pixelY++) {
		for (int16_t pixelX = minX; pixelX <= maxX; pixelX++) {
			int32_t edge0 = edgeFunction(vertex1.x, vertex1.y, vertex2.x, vertex2.y, pixelX, pixelY);
			int32_t edge1 = edgeFunction(vertex2.x, vertex2.y, vertex0.x, vertex0.y, pixelX, pixelY);
			int32_t edge2 = edgeFunction(vertex0.x, vertex0.y, vertex1.x, vertex1.y, pixelX, pixelY);

			bool inside = (edge0 >= 0 && edge1 >= 0 && edge2 >= 0) || (edge0 <= 0 && edge1 <= 0 && edge2 <= 0);
			if (!inside) continue;

			float weight0 = static_cast<float>(edge0) / areaTotal;
			float weight1 = static_cast<float>(edge1) / areaTotal;
			float weight2 = static_cast<float>(edge2) / areaTotal;

			uint16_t finalColor = vertex0.color;

			if (isGouraud) {
				auto interpolateChannel = [&](int shift) {
					int channel0 = (vertex0.color >> shift) & 0x1F;
					int channel1 = (vertex1.color >> shift) & 0x1F;
					int channel2 = (vertex2.color >> shift) & 0x1F;
					return static_cast<int>(weight0 * channel0 + weight1 * channel1 + weight2 * channel2) & 0x1F;
				};
				finalColor = interpolateChannel(0) | (interpolateChannel(5) << 5) | (interpolateChannel(10) << 10);
			}

			if (isTextured) {
				int textureCoordinateX = static_cast<int>(weight0 * vertex0.u + weight1 * vertex1.u + weight2 * vertex2.u);
				int textureCoordinateY = static_cast<int>(weight0 * vertex0.v + weight1 * vertex1.v + weight2 * vertex2.v);

				uint16_t texel = sampleTexture(textureCoordinateX, textureCoordinateY, colorLookupTableX, colorLookupTableY);
				if (texel == 0) continue;

				finalColor = texel;
			}

			vram.writePixel(pixelX, pixelY, finalColor);
		}
	}
}

void Rasterizer::clipToDrawingArea(int16_t& minX, int16_t& maxX, int16_t& minY, int16_t& maxY) {
	minX = max(minX, (int16_t)max(0, (int)drawingAreaLeft));
	maxX = min(maxX, (int16_t)min((int)Hardware::VRAM_WIDTH - 1, (int)drawingAreaRight));
	minY = max(minY, (int16_t)max(0, (int)drawingAreaTop));
	maxY = min(maxY, (int16_t)min((int)Hardware::VRAM_HEIGHT - 1, (int)drawingAreaBottom));
}

uint16_t Rasterizer::sampleTexture(int textureX, int textureY, uint16_t colorLookupTableX, uint16_t colorLookupTableY) {
	switch (texturePageColorDepth) {
		case 0: { // 4-bit CLUT
			int videoRamX = texturePageBaseX + (textureX / 4);
			int videoRamY = texturePageBaseY + textureY;

			uint16_t pixelDataWord = vram.readPixel(videoRamX, videoRamY);
			int bitShift = (textureX % 4) * 4;
			uint8_t colorLookupTableIndex = (pixelDataWord >> bitShift) & 0xF;

			return vram.readPixel(colorLookupTableX + colorLookupTableIndex, colorLookupTableY);
		}
		case 1: { // 8-bit CLUT
			int videoRamX = texturePageBaseX + (textureX / 2);
			int videoRamY = texturePageBaseY + textureY;

			uint16_t pixelDataWord = vram.readPixel(videoRamX, videoRamY);
			int bitShift = (textureX % 2) * 8;
			uint8_t colorLookupTableIndex = (pixelDataWord >> bitShift) & 0xFF;

			return vram.readPixel(colorLookupTableX + colorLookupTableIndex, colorLookupTableY);
		}
		default: { // 15-bit Direct
			int videoRamX = texturePageBaseX + textureX;
			int videoRamY = texturePageBaseY + textureY;

			return vram.readPixel(videoRamX, videoRamY);
		}
	}
}