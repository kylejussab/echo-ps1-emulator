#pragma once

#include <cstdint>
#include "VRAM.h"
#include "../core/Constants.h"


/// @brief Handles the hardware rasterization of 2D primitives (polygons, lines, rectangles) directly into VRAM.
/// Manages the current drawing environment state, including clipping areas, texture windows, CLUT caching, and blending modes.
class Rasterizer {
public:
    /// @brief Represents a single point in 2D space with associated texture and color data.
    struct Vertex {
        int16_t  x = 0;
        int16_t  y = 0;
        uint8_t  u = 0;
        uint8_t  v = 0;
        uint32_t color = 0; // RAW 24-bit GP0 color, 0x00BBGGRR. Do NOT truncate to 15-bit before passing it in the GPU shades/modulates at 8 bits per channel and only truncates on write.
    };

	explicit Rasterizer(VRAM& vram) : vram(vram) {}


    /// @brief Sets the texture window mask and offset for texture wrapping/mirroring.
    void setTextureWindow(uint8_t maskX, uint8_t maskY, uint8_t offsetX, uint8_t offsetY);
    
    
    /// @brief Configures how the hardware mask bit (bit 15) is handled during VRAM writes.
    void setMaskSettings(bool setMask, bool preserveMask);


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
    /// @param flatColor RAW 24-bit command color 0x00BBGGRR (the base color if untextured, the tint if textured).
    /// @param isTextured True if the rectangle maps a texture.
    /// @param isRawTexture True if texture colors bypass illumination/shading.
    /// @param textureCoordinateU The base U coordinate of the texture.
    /// @param textureCoordinateV The base V coordinate of the texture.
    /// @param colorLookupTableX The CLUT X base coordinate in VRAM.
    /// @param colorLookupTableY The CLUT Y base coordinate in VRAM.
	void drawRectangle(int16_t x, int16_t y, uint16_t width, uint16_t height, uint32_t flatColor, bool isTextured, bool isSemiTransparent, bool isRawTexture, uint8_t textureCoordinateU, uint8_t textureCoordinateV, uint16_t colorLookupTableX, uint16_t colorLookupTableY);

	
	/// @brief Fills a block of VRAM with a solid color. Unlike standard drawing commands, 
    /// the drawing offset is NOT applied on real hardware for this operation.
    /// @param x Absolute VRAM X coordinate.
    /// @param y Absolute VRAM Y coordinate.
    /// @param width Width of the fill block.
    /// @param height Height of the fill block.
    /// @param color 15-bit color to fill the region with.
	void fillRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);


    /// @brief Renders a straight line between two vertices using the current hardware environment state.
    /// @param v0 The starting vertex (contains pre-offset coordinates and base color).
    /// @param v1 The ending vertex (contains pre-offset coordinates and end color).
    /// @param isSemiTransparent True if the line uses alpha blending.
    /// @param isGouraud True if the line smoothly interpolates colors between the two vertices.
    void drawLine(const Vertex& v0, const Vertex& v1, bool isSemiTransparent, bool isGouraud);


    /// @brief Invalidates the cached Color Lookup Table (CLUT) to force a reload on the next textured draw.
    void invalidateCache();


    /// @brief GP0(E1h) bit 9. Applies the 4x4 ordered dither to Gouraud / texture-modulated polygons and to ALL lines (flat and Gouraud).
	/// Rectangles, flat polygons and raw-texture draws are never dithered on hardware.
    void setDither(bool enabled) { ditherEnabled = enabled; }


    /// @brief Returns the estimated GPU clocks spent since the last call, then resets the counter.
    double takeDrawCost() { double cost = drawCost; drawCost = 0.0; return cost; }


private:
	VRAM& vram;


    uint8_t textureWindowMaskX = 0;
    uint8_t textureWindowMaskY = 0;
    uint8_t textureWindowOffsetX = 0;
    uint8_t textureWindowOffsetY = 0;

    bool maskForceSet = false;
    bool maskPreserve = false;
    bool ditherEnabled = false;

    uint16_t activeCLUT[256] = {0};
    uint32_t cachedClutTag = 0xFFFFFFFF;
    bool cachedClut4BitValid = false;
    bool cachedClut8BitValid = false;


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

    double drawCost = 0.0;

    // Texture flip quirk. Evidence from texture-flip.exe: X-flipped rectangles match hardware when sampling starts at (U | 1), but Y-flipped ones get WORSE with (V | 1). So only X gets the odd-start treatment.
    static constexpr bool kFlipXForcesOddStart = true;
    static constexpr bool kFlipYForcesOddStart = false;

    // PS1 4x4 ordered dither, indexed [y & 3][x & 3] with VRAM coordinates, applied to 8-bit channels.
    static constexpr int8_t kDitherTable[4][4] = {
        {-4,  0, -3,  1},
        { 2, -2,  3, -1},
        {-3,  1, -4,  0},
        { 3, -1,  2, -2},
    };


	// Helpers

    /// @brief Converts an 8-bit-per-channel shaded color to the 15-bit value written to VRAM.
    /// @param red The 8-bit red color channel (0-255).
    /// @param green The 8-bit green color channel (0-255).
    /// @param blue The 8-bit blue color channel (0-255).
    /// @param pixelX The absolute X coordinate of the pixel in VRAM (used for dither matrix alignment).
    /// @param pixelY The absolute Y coordinate of the pixel in VRAM (used for dither matrix alignment).
    /// @param dither True to apply the PS1 4x4 hardware dither pattern before color truncation.
    /// @return The packed 15-bit color (0BBBBBGGGGGRRRRR).
    static uint16_t packColor(int red, int green, int blue, int pixelX, int pixelY, bool dither);


    /// @brief Determines if a triangle edge qualifies as a "top" or "left" edge for the top-left fill rule.
    /// Assumes the triangle's winding was normalized so its area is positive.
    /// @param startX X coordinate of the edge's start vertex.
    /// @param startY Y coordinate of the edge's start vertex.
    /// @param endX X coordinate of the edge's end vertex.
    /// @param endY Y coordinate of the edge's end vertex.
    /// @return True if the edge is a top or left edge, preventing double-drawing of shared edges.
    bool isTopLeftEdge(int16_t startX, int16_t startY, int16_t endX, int16_t endY) const;


    /// @brief Loads the specified Color Lookup Table (CLUT) from VRAM into the local cache.
    /// @param clutX The X coordinate of the CLUT in VRAM.
    /// @param clutY The Y coordinate of the CLUT in VRAM.
    void loadCLUT(uint16_t clutX, uint16_t clutY);


    /// @brief Rasterizes a single triangle, handling texture mapping and gouraud shading if requested.
    /// @param vertex0 The first vertex of the triangle.
    /// @param vertex1 The second vertex of the triangle.
    /// @param vertex2 The third vertex of the triangle.
    /// @param isTextured True if the triangle maps a texture.
    /// @param isSemiTransparent True if the triangle uses alpha blending.
    /// @param isRawTexture True if texture colors bypass illumination/shading.
    /// @param isGouraud True if vertex colors are smoothly interpolated across the face.
    /// @param colorLookupTableX The CLUT X base coordinate in VRAM.
    /// @param colorLookupTableY The CLUT Y base coordinate in VRAM.
	void drawTriangle(const Vertex& vertex0, const Vertex& vertex1, const Vertex& vertex2, bool isTextured, bool isSemiTransparent, bool isRawTexture, bool isGouraud, uint16_t colorLookupTableX, uint16_t colorLookupTableY);
	
	
    /// @brief Blends a calculated foreground color with the existing VRAM background pixel.
    /// @param backgroundColor The existing 15-bit pixel color in VRAM.
    /// @param foregroundColor The incoming 15-bit pixel color to blend.
    /// @return The final blended 15-bit color to write to VRAM.
    uint16_t blendSemiTransparent(uint16_t backgroundColor, uint16_t foregroundColor) const;


    /// @brief Restricts rasterization boundaries to the currently active hardware drawing area.
    /// @param minX Reference to the minimum X bounding coordinate to clip.
    /// @param maxX Reference to the maximum X bounding coordinate to clip.
    /// @param minY Reference to the minimum Y bounding coordinate to clip.
    /// @param maxY Reference to the maximum Y bounding coordinate to clip.
	void clipToDrawingArea(int16_t& minX, int16_t& maxX, int16_t& minY, int16_t& maxY);
	
	
    /// @brief Fetches and decodes a texel from the active texture page and cached CLUT.
    /// @param textureX The X coordinate within the texture page.
    /// @param textureY The Y coordinate within the texture page.
    /// @param colorLookupTableX The CLUT X base coordinate in VRAM.
    /// @param colorLookupTableY The CLUT Y base coordinate in VRAM.
    /// @return The decoded 15-bit color of the sampled texel.
	uint16_t sampleTexture(int textureX, int textureY, uint16_t colorLookupTableX, uint16_t colorLookupTableY);
	

    /// @brief 5-bit texel channel * 8-bit color channel.
    static inline int modulate(int texel5, int color8) { return (texel5 * color8) >> 4; }
    
	
    /// @brief Evaluates which side of a line segment a point falls on (used for barycentric rasterization).
    /// @param x0 X coordinate of the line segment's start.
    /// @param y0 Y coordinate of the line segment's start.
    /// @param x1 X coordinate of the line segment's end.
    /// @param y1 Y coordinate of the line segment's end.
    /// @param pointX X coordinate of the point to test.
    /// @param pointY Y coordinate of the point to test.
    /// @return A positive value if the point is on one side, negative if on the other, and 0 if exactly on the line.
	int32_t edgeFunction(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t pointX, int16_t pointY) { return (x1 - x0) * (pointY - y0) - (y1 - y0) * (pointX - x0); }
};