#include "Rasterizer.h"
#include <algorithm>
#include <iostream>

void Rasterizer::setTextureWindow(uint8_t maskX, uint8_t maskY, uint8_t offsetX, uint8_t offsetY) {
    textureWindowMaskX = maskX;
    textureWindowMaskY = maskY;
    textureWindowOffsetX = offsetX;
    textureWindowOffsetY = offsetY;
}

void Rasterizer::setMaskSettings(bool setMask, bool preserveMask) {
    maskForceSet = setMask;
    maskPreserve = preserveMask;
}

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


void Rasterizer::setDrawingAreaTopLeft(uint16_t left, uint16_t top) {
    drawingAreaLeft = left;
    drawingAreaTop = top;
}


void Rasterizer::setDrawingAreaBottomRight(uint16_t right, uint16_t bottom) {
    drawingAreaRight = right;
    drawingAreaBottom = bottom;
}


void Rasterizer::setDrawingOffset(int16_t offsetX, int16_t offsetY) {
    drawingOffsetX = offsetX;
    drawingOffsetY = offsetY;
}


void Rasterizer::drawPolygon(const Vertex* vertices, int vertexCount, bool isTextured, bool isSemiTransparent, bool isRawTexture, bool isGouraud, uint16_t colorLookupTableX, uint16_t colorLookupTableY) {
    // TODO: Wire up semi-transparency blending
    if (isSemiTransparent) {
        std::cout << "FATAL: Rasterizer missing Semi-Transparency (Polygons)\n";
        exit(1);
    }
    
    // TODO: Wire up raw-texture sampling (bypassing color modulation)
    if (isRawTexture) {
        std::cout << "FATAL: Rasterizer missing Raw Texture (Polygons)\n";
        exit(1);
    }

    // Apply the persistent drawing offset to all vertices once before rasterization
    Vertex offsetVertices[4];
    for (int index = 0; index < vertexCount; index++) {
        offsetVertices[index] = vertices[index];
        offsetVertices[index].x += drawingOffsetX;
        offsetVertices[index].y += drawingOffsetY;
    }

    drawTriangle(offsetVertices[0], offsetVertices[1], offsetVertices[2], isTextured, isRawTexture, isGouraud, colorLookupTableX, colorLookupTableY);

    // Quads are drawn as two triangles with a V1, V3, V2 winding, matching PS1 hardware.
    if (vertexCount == 4) {
        drawTriangle(offsetVertices[1], offsetVertices[3], offsetVertices[2], isTextured, isRawTexture, isGouraud, colorLookupTableX, colorLookupTableY);
    }
}


void Rasterizer::drawRectangle(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t flatColor, bool isTextured, bool isRawTexture, uint8_t textureCoordinateU, uint8_t textureCoordinateV, uint16_t colorLookupTableX, uint16_t colorLookupTableY) {
    // TODO: Wire up raw-texture sampling
    if (isRawTexture) {
        std::cout << "FATAL: Rasterizer missing Raw Texture (Rectangles)\n";
        exit(1);
    }

    int16_t originX = x + drawingOffsetX;
    int16_t originY = y + drawingOffsetY;

    for (uint16_t row = 0; row < height; row++) {
        for (uint16_t column = 0; column < width; column++) {
            int16_t pixelX = originX + column;
            int16_t pixelY = originY + row;

            // Rectangles strictly obey the hardware clipping plane
            if (pixelX < static_cast<int16_t>(drawingAreaLeft) || pixelX > static_cast<int16_t>(drawingAreaRight) ||
                pixelY < static_cast<int16_t>(drawingAreaTop)  || pixelY > static_cast<int16_t>(drawingAreaBottom)) {
                continue;
            }

            uint16_t color = flatColor;

            if (isTextured) {
                int textureCoordinateX = textureCoordinateU + (textureFlipX ? -column : column);
                int textureCoordinateY = textureCoordinateV + (textureFlipY ? -row : row);
                
                // Apply Hardware Texture Window Bitmasking
                textureCoordinateX = (textureCoordinateX & ~(textureWindowMaskX * 8)) | ((textureWindowOffsetX & textureWindowMaskX) * 8);
                textureCoordinateY = (textureCoordinateY & ~(textureWindowMaskY * 8)) | ((textureWindowOffsetY & textureWindowMaskY) * 8);
                
                // PS1 textures are strictly bound to 256x256 pages
                textureCoordinateX &= 0xFF;
                textureCoordinateY &= 0xFF;
                
                uint16_t texel = sampleTexture(textureCoordinateX, textureCoordinateY, colorLookupTableX, colorLookupTableY);

                if (texel == 0) continue; 

                if (!isRawTexture) {
                    // Extract 5-bit RGB channels from the texture
                    int texR = texel & 0x1F;
                    int texG = (texel >> 5) & 0x1F;
                    int texB = (texel >> 10) & 0x1F;

                    // Extract 5-bit RGB channels from the flat rectangle color
                    int colR = flatColor & 0x1F;
                    int colG = (flatColor >> 5) & 0x1F;
                    int colB = (flatColor >> 10) & 0x1F;

                    // Multiply and shift
                    int outR = std::min(31, (texR * colR) >> 4);
                    int outG = std::min(31, (texG * colG) >> 4);
                    int outB = std::min(31, (texB * colB) >> 4);

                    color = outR | (outG << 5) | (outB << 10);
                } 
                else {
                    color = texel;
                }
            }

            if (maskPreserve && (vram.readPixel(pixelX, pixelY) & 0x8000)) {
                continue;
            }
            if (maskForceSet) {
                color |= 0x8000;
            }
            vram.writePixel(pixelX, pixelY, color);
        }
    }
}


void Rasterizer::fillRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color) {
    // The PS1 Block Fill command explicitly ignores both the Drawing Offset and the Drawing Area clip.
    // It is only constrained by the absolute physical limits of the 1024x512 VRAM.
    for (uint16_t row = 0; row < height; row++) {
        for (uint16_t column = 0; column < width; column++) {
            uint16_t pixelX = x + column;
            uint16_t pixelY = y + row;

            if (pixelX < Hardware::VRAM_WIDTH && pixelY < Hardware::VRAM_HEIGHT) {
                if (maskPreserve && (vram.readPixel(pixelX, pixelY) & 0x8000)) {
                    continue; // Do not draw over masked pixels
                }
                vram.writePixel(pixelX, pixelY, color);
            }
        }
    }
}


// Top-left fill rule: a pixel exactly on an edge belongs to the triangle only if that edge is a top or left edge. Assumes the triangle's winding was normalized so its area is positive.
static bool isTopLeftEdge(int16_t startX, int16_t startY, int16_t endX, int16_t endY) {
	int deltaX = endX - startX;
	int deltaY = endY - startY;
	return (deltaY == 0 && deltaX > 0) || deltaY < 0;
}


void Rasterizer::drawTriangle(const Vertex& vertex0, const Vertex& vertex1, const Vertex& vertex2, bool isTextured, bool isRawTexture, bool isGouraud, uint16_t colorLookupTableX, uint16_t colorLookupTableY) {
	Vertex first = vertex0;
	Vertex second = vertex1;
	Vertex third = vertex2;

	int32_t areaTotal = edgeFunction(first.x, first.y, second.x, second.y, third.x, third.y);
	if (areaTotal == 0) return; // Degenerate triangle

	// Normalize the winding so the area is positive, which lets one top-left test work for both orientations
	if (areaTotal < 0) {
		std::swap(second, third);
		areaTotal = -areaTotal;
	}

	// Find the bounding box of the triangle
	int16_t minX = std::min({first.x, second.x, third.x});
	int16_t maxX = std::max({first.x, second.x, third.x});
	int16_t minY = std::min({first.y, second.y, third.y});
	int16_t maxY = std::max({first.y, second.y, third.y});

	// Constrain the bounding box to absolute VRAM limits
	minX = std::max((int16_t)0, minX);
	maxX = std::min((int16_t)(Hardware::VRAM_WIDTH - 1), maxX);
	minY = std::max((int16_t)0, minY);
	maxY = std::min((int16_t)(Hardware::VRAM_HEIGHT - 1), maxY);

	// Further constrain to the active drawing area
	clipToDrawingArea(minX, maxX, minY, maxY);

	bool isEdge0TopLeft = isTopLeftEdge(second.x, second.y, third.x, third.y);
	bool isEdge1TopLeft = isTopLeftEdge(third.x, third.y, first.x, first.y);
	bool isEdge2TopLeft = isTopLeftEdge(first.x, first.y, second.x, second.y);

	for (int16_t pixelY = minY; pixelY <= maxY; pixelY++) {
		for (int16_t pixelX = minX; pixelX <= maxX; pixelX++) {
			int32_t edge0 = edgeFunction(second.x, second.y, third.x, third.y, pixelX, pixelY);
			int32_t edge1 = edgeFunction(third.x, third.y, first.x, first.y, pixelX, pixelY);
			int32_t edge2 = edgeFunction(first.x, first.y, second.x, second.y, pixelX, pixelY);

			bool inside = (edge0 > 0 || (edge0 == 0 && isEdge0TopLeft))
				&& (edge1 > 0 || (edge1 == 0 && isEdge1TopLeft))
				&& (edge2 > 0 || (edge2 == 0 && isEdge2TopLeft));
			if (!inside) continue;

			uint16_t finalColor = first.color;

			if (isGouraud) {
				auto interpolateChannel = [&](int shift) {
					int64_t numerator = static_cast<int64_t>(edge0) * ((first.color >> shift) & 0x1F)
						+ static_cast<int64_t>(edge1) * ((second.color >> shift) & 0x1F)
						+ static_cast<int64_t>(edge2) * ((third.color >> shift) & 0x1F);
					return static_cast<int>(numerator / areaTotal) & 0x1F;
				};
				// Recombine 5-bit color channels
				finalColor = interpolateChannel(0) | (interpolateChannel(5) << 5) | (interpolateChannel(10) << 10);
			}

			if (isTextured) {
				int64_t numeratorU = static_cast<int64_t>(edge0) * first.u + static_cast<int64_t>(edge1) * second.u + static_cast<int64_t>(edge2) * third.u;
				int64_t numeratorV = static_cast<int64_t>(edge0) * first.v + static_cast<int64_t>(edge1) * second.v + static_cast<int64_t>(edge2) * third.v;
				int textureCoordinateX = static_cast<int>(numeratorU / areaTotal);
				int textureCoordinateY = static_cast<int>(numeratorV / areaTotal);

				// Apply Hardware Texture Window Bitmasking
				textureCoordinateX = (textureCoordinateX & ~(textureWindowMaskX * 8)) | ((textureWindowOffsetX & textureWindowMaskX) * 8);
				textureCoordinateY = (textureCoordinateY & ~(textureWindowMaskY * 8)) | ((textureWindowOffsetY & textureWindowMaskY) * 8);

				// PS1 textures are strictly bound to 256x256 pages
				textureCoordinateX &= 0xFF;
				textureCoordinateY &= 0xFF;

				uint16_t texel = sampleTexture(textureCoordinateX, textureCoordinateY, colorLookupTableX, colorLookupTableY);
				if (texel == 0) continue; // 0x0000 is fully transparent in PS1 textures

				if (!isRawTexture) {
					// Extract 5-bit RGB channels from the texture
					int textureRed = texel & 0x1F;
					int textureGreen = (texel >> 5) & 0x1F;
					int textureBlue = (texel >> 10) & 0x1F;

					// Extract 5-bit RGB channels from the interpolated vertex color
					int colorRed = finalColor & 0x1F;
					int colorGreen = (finalColor >> 5) & 0x1F;
					int colorBlue = (finalColor >> 10) & 0x1F;

					// Multiply and shift (PS1 treats vertex color 16 as a 1.0 multiplier)
					int outputRed = std::min(31, (textureRed * colorRed) >> 4);
					int outputGreen = std::min(31, (textureGreen * colorGreen) >> 4);
					int outputBlue = std::min(31, (textureBlue * colorBlue) >> 4);

					finalColor = outputRed | (outputGreen << 5) | (outputBlue << 10);
				}
				else {
					finalColor = texel;
				}
			}

			if (maskPreserve && (vram.readPixel(pixelX, pixelY) & 0x8000)) {
				continue;
			}
			if (maskForceSet) {
				finalColor |= 0x8000;
			}
			vram.writePixel(pixelX, pixelY, finalColor);
		}
	}
}


void Rasterizer::clipToDrawingArea(int16_t& minX, int16_t& maxX, int16_t& minY, int16_t& maxY) {
    minX = std::max(minX, (int16_t)std::max(0, (int)drawingAreaLeft));
    maxX = std::min(maxX, (int16_t)std::min((int)Hardware::VRAM_WIDTH - 1, (int)drawingAreaRight));
    minY = std::max(minY, (int16_t)std::max(0, (int)drawingAreaTop));
    maxY = std::min(maxY, (int16_t)std::min((int)Hardware::VRAM_HEIGHT - 1, (int)drawingAreaBottom));
}


uint16_t Rasterizer::sampleTexture(int textureX, int textureY, uint16_t colorLookupTableX, uint16_t colorLookupTableY) {
    switch (texturePageColorDepth) {
        case 0: { // 4-bit CLUT (4 pixels packed into 1 16-bit word)
            int videoRamX = texturePageBaseX + (textureX / 4);
            int videoRamY = texturePageBaseY + textureY;

            uint16_t pixelDataWord = vram.readPixel(videoRamX, videoRamY);
            int bitShift = (textureX % 4) * 4;
            uint8_t colorLookupTableIndex = (pixelDataWord >> bitShift) & 0xF;

            return vram.readPixel(colorLookupTableX + colorLookupTableIndex, colorLookupTableY);
        }
        case 1: { // 8-bit CLUT (2 pixels packed into 1 16-bit word)
            int videoRamX = texturePageBaseX + (textureX / 2);
            int videoRamY = texturePageBaseY + textureY;

            uint16_t pixelDataWord = vram.readPixel(videoRamX, videoRamY);
            int bitShift = (textureX % 2) * 8;
            uint8_t colorLookupTableIndex = (pixelDataWord >> bitShift) & 0xFF;

            return vram.readPixel(colorLookupTableX + colorLookupTableIndex, colorLookupTableY);
        }
        default: { // 15-bit Direct (1 pixel per 16-bit word, no CLUT)
            int videoRamX = texturePageBaseX + textureX;
            int videoRamY = texturePageBaseY + textureY;

            return vram.readPixel(videoRamX, videoRamY);
        }
    }
}
