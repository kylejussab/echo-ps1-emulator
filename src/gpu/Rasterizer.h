#pragma once
#include <cstdint>
#include "../Constants.h"
#include "VRAM.h"

/// @brief Represents a single point in 2D space with associated texture and color data.
struct Vertex {
	int16_t  x = 0;
	int16_t  y = 0;
	uint8_t  u = 0;
	uint8_t  v = 0;
	uint16_t color = 0; // 15-bit PS1 color (5-5-5-1 format)
};

class Rasterizer {
public:
	explicit Rasterizer(VRAM& vram) : vram(vram) {}

	/// @brief Sets the base VRAM coordinates and color depth for the current texture page.
    /// @param baseX The X offset in VRAM (in 64-pixel blocks).
    /// @param baseY The Y offset in VRAM (in 256-pixel blocks).
    /// @param colorDepth The bit depth of the texture (4-bit, 8-bit, or 15-bit).
    /// @param semiTransparency The blending mode to use when drawing semi-transparent pixels.
	void setTexturePage(uint16_t baseX, uint16_t baseY, uint8_t colorDepth, uint8_t semiTransparency);


	/// @brief Configures hardware-level texture flipping during rendering.
    /// @param flipX If true, flips the texture horizontally.
    /// @param flipY If true, flips the texture vertically.
	void setTextureFlip(bool flipX, bool flipY);


	/// @brief Defines the bounding box in VRAM where rendering is allowed. Pixels outside are clipped.
    /// @param left The minimum X coordinate.
    /// @param top The minimum Y coordinate.
    /// @param right The maximum X coordinate.
    /// @param bottom The maximum Y coordinate.
	void setDrawingArea(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom);


	/// @brief Sets the top-left corner of the hardware clipping plane.
    /// @param left The minimum X coordinate.
    /// @param top The minimum Y coordinate.
    void setDrawingAreaTopLeft(uint16_t left, uint16_t top);


	/// @brief Sets the bottom-right corner of the hardware clipping plane.
    /// @param right The maximum X coordinate.
    /// @param bottom The maximum Y coordinate.
    void setDrawingAreaBottomRight(uint16_t right, uint16_t bottom);


	/// @brief Sets the base coordinate offset applied to all vertices before rendering.
    /// @param offsetX The X offset applied to incoming vertices.
    /// @param offsetY The Y offset applied to incoming vertices.
	void setDrawingOffset(int16_t offsetX, int16_t offsetY);



	/// @brief Renders a standard polygon (triangle or quad) using the current environment settings. 
    /// Coordinates should be RAW (pre-drawing-offset); the persistent offset is applied internally.
    /// @param vertices Array of raw vertices parsed from the FIFO.
    /// @param vertexCount Number of vertices (3 for triangle, 4 for quad).
    /// @param isTextured True if the polygon should map a texture.
    /// @param isSemiTransparent True if the polygon uses alpha blending.
    /// @param isRawTexture True if texture colors bypass illumination/shading.
    /// @param isGouraud True if vertex colors are smoothly interpolated across the face.
    /// @param colorLookupTableX The CLUT X base coordinate in VRAM (for 4/8-bit textures).
    /// @param colorLookupTableY The CLUT Y base coordinate in VRAM (for 4/8-bit textures).
	void drawPolygon(const Vertex* vertices, int vertexCount, bool isTextured, bool isSemiTransparent, bool isRawTexture, bool isGouraud, uint16_t colorLookupTableX, uint16_t colorLookupTableY);


	/// @brief Renders an un-rotated rectangle (sprite) using specialized, faster rasterization.
    /// Coordinates should be RAW (pre-drawing-offset); the persistent offset is applied internally.
    /// @param x Raw pre-offset X coordinate.
    /// @param y Raw pre-offset Y coordinate.
    /// @param width Width of the rectangle.
    /// @param height Height of the rectangle.
    /// @param flatColor The base color (if untextured) or tint (if textured).
    /// @param isTextured True if the rectangle maps a texture.
    /// @param isRawTexture True if texture colors bypass illumination/shading.
    /// @param textureCoordinateU The base U coordinate of the texture.
    /// @param textureCoordinateV The base V coordinate of the texture.
    /// @param colorLookupTableX The CLUT X base coordinate in VRAM.
    /// @param colorLookupTableY The CLUT Y base coordinate in VRAM.
	void drawRectangle(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t flatColor, bool isTextured, bool isRawTexture, uint8_t textureCoordinateU, uint8_t textureCoordinateV, uint16_t colorLookupTableX, uint16_t colorLookupTableY);

	
	/// @brief Fills a block of VRAM with a solid color. Unlike standard drawing commands, 
    /// the drawing offset is NOT applied on real hardware for this operation.
    /// @param x Absolute VRAM X coordinate.
    /// @param y Absolute VRAM Y coordinate.
    /// @param width Width of the fill block.
    /// @param height Height of the fill block.
    /// @param color 15-bit color to fill the region with.
	void fillRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);

private:
	VRAM& vram;

	// Persistent draw-mode state (GP0 0xE1)
	uint16_t texturePageBaseX = 0;
	uint16_t texturePageBaseY = 0;
	uint8_t  texturePageColorDepth = 0;
	uint8_t  texturePageSemiTransparency = 0;
	bool     textureFlipX = false;
	bool     textureFlipY = false;

	// Persistent drawing area / offset state (GP0 0xE3-0xE5)
	uint16_t drawingAreaLeft = 0;
	uint16_t drawingAreaTop = 0;
	uint16_t drawingAreaRight = 0;
	uint16_t drawingAreaBottom = 0;
	int16_t  drawingOffsetX = 0;
	int16_t  drawingOffsetY = 0;


	// Internal rendering helpers

	void drawTriangle(const Vertex& vertex0, const Vertex& vertex1, const Vertex& vertex2, bool isTextured, bool isRawTexture, bool isGouraud, uint16_t colorLookupTableX, uint16_t colorLookupTableY);
	
	
	void clipToDrawingArea(int16_t& minX, int16_t& maxX, int16_t& minY, int16_t& maxY);
	
	
	uint16_t sampleTexture(int textureX, int textureY, uint16_t colorLookupTableX, uint16_t colorLookupTableY);
	
	
	int32_t edgeFunction(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t pointX, int16_t pointY) { return (x1 - x0) * (pointY - y0) - (y1 - y0) * (pointX - x0); }
};