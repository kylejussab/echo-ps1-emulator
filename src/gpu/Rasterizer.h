#pragma once
#include <cstdint>
#include "../Constants.h"
#include "VRAM.h"

struct Vertex {
	int16_t x = 0;
	int16_t y = 0;
	uint8_t u = 0;
	uint8_t v = 0;
	uint16_t color = 0; // 15-bit PS1 color (5-5-5-1 format)
};

class Rasterizer {
public:
	explicit Rasterizer(VRAM& vram) : vram(vram) {}

	// ---------------------------------------------------------
	// Drawing environment (persistent state set by GP0 0xE1-0xE6)
	// ---------------------------------------------------------
	void setTexturePage(uint16_t baseX, uint16_t baseY, uint8_t colorDepth, uint8_t semiTransparency);
	void setTextureFlip(bool flipX, bool flipY);
	void setDrawingArea(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom);
	void setDrawingOffset(int16_t offsetX, int16_t offsetY);

    void setDrawingAreaTopLeft(uint16_t left, uint16_t top);
    void setDrawingAreaBottomRight(uint16_t right, uint16_t bottom);

	// ---------------------------------------------------------
	// Primitive drawing entry points
	// ---------------------------------------------------------
	// vertices are RAW (pre-drawing-offset) coordinates as parsed off the FIFO;
	// the persistent drawingOffsetX/Y is applied internally, once, here.
	void drawPolygon(const Vertex* vertices, int vertexCount,
	                  bool isTextured, bool isSemiTransparent, bool isRawTexture, bool isGouraud,
	                  uint16_t colorLookupTableX, uint16_t colorLookupTableY);

	// x, y are RAW (pre-drawing-offset) coordinates; the persistent offset is applied internally.
	void drawRectangle(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t flatColor,
	                    bool isTextured, bool isRawTexture,
	                    uint8_t textureCoordinateU, uint8_t textureCoordinateV,
	                    uint16_t colorLookupTableX, uint16_t colorLookupTableY);

	// x, y are absolute VRAM coordinates; unlike drawRectangle, the Fill VRAM
	// command does NOT apply the drawing offset on real hardware.
	void fillRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);

private:
	VRAM& vram;

	// Persistent draw-mode state (GP0 0xE1)
	uint16_t texturePageBaseX = 0;
	uint16_t texturePageBaseY = 0;
	uint8_t texturePageColorDepth = 0;
	uint8_t texturePageSemiTransparency = 0;
	bool textureFlipX = false;
	bool textureFlipY = false;

	// Persistent drawing area / offset state (GP0 0xE3-0xE5)
	uint16_t drawingAreaLeft = 0;
	uint16_t drawingAreaTop = 0;
	uint16_t drawingAreaRight = 0;
	uint16_t drawingAreaBottom = 0;
	int16_t drawingOffsetX = 0;
	int16_t drawingOffsetY = 0;

	void drawTriangle(const Vertex& vertex0, const Vertex& vertex1, const Vertex& vertex2,
	                   bool isTextured, bool isGouraud,
	                   uint16_t colorLookupTableX, uint16_t colorLookupTableY);
	void clipToDrawingArea(int16_t& minX, int16_t& maxX, int16_t& minY, int16_t& maxY);
	uint16_t sampleTexture(int textureX, int textureY, uint16_t colorLookupTableX, uint16_t colorLookupTableY);
	int32_t edgeFunction(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t pointX, int16_t pointY) {
		return (x1 - x0) * (pointY - y0) - (y1 - y0) * (pointX - x0);
	}
};