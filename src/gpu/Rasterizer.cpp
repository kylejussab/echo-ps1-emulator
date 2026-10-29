#include <algorithm>
#include <cstdlib>
#include <iostream>
#include "Rasterizer.h"


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
    if (isTextured) loadCLUT(colorLookupTableX, colorLookupTableY);

    Vertex offsetVertices[4];

    for (int index = 0; index < vertexCount; index++) {
        offsetVertices[index] = vertices[index];
        offsetVertices[index].x += drawingOffsetX;
        offsetVertices[index].y += drawingOffsetY;
    }

    drawTriangle(offsetVertices[0], offsetVertices[1], offsetVertices[2], isTextured, isSemiTransparent, isRawTexture, isGouraud, colorLookupTableX, colorLookupTableY);

    // Quads are two triangles: (V0, V1, V2) and (V1, V2, V3). The vertex order matters because the attribute interpolation below is anchored to the first vertex it is given.
    if (vertexCount == 4) {
        drawTriangle(offsetVertices[1], offsetVertices[2], offsetVertices[3], isTextured, isSemiTransparent, isRawTexture, isGouraud, colorLookupTableX, colorLookupTableY);
    }
}


void Rasterizer::drawRectangle(int16_t x, int16_t y, uint16_t width, uint16_t height, uint32_t flatColor, bool isTextured, bool isSemiTransparent, bool isRawTexture, uint8_t textureCoordinateU, uint8_t textureCoordinateV, uint16_t colorLookupTableX, uint16_t colorLookupTableY) {
    int16_t originX = x + drawingOffsetX;
    int16_t originY = y + drawingOffsetY;

    if (isTextured) loadCLUT(colorLookupTableX, colorLookupTableY);

    const int colorRed = flatColor & 0xFF;
    const int colorGreen = (flatColor >> 8) & 0xFF;
    const int colorBlue = (flatColor >> 16) & 0xFF;

    const uint16_t flatPacked = packColor(colorRed, colorGreen, colorBlue, 0, 0, false);

    const int startU = (textureFlipX && kFlipXForcesOddStart) ? (textureCoordinateU | 1) : textureCoordinateU;
    const int startV = (textureFlipY && kFlipYForcesOddStart) ? (textureCoordinateV | 1) : textureCoordinateV;

    // Rectangles strictly obey the hardware clipping plane. Work out which rows/columns survive it up front (instead of testing every pixel); everything below only depends on (row, column), so skipping the clipped ones changes nothing.
    const int columnFirst = std::max(0, static_cast<int>(static_cast<int16_t>(drawingAreaLeft)) - originX);
    const int columnLast = std::min(static_cast<int>(width) - 1, static_cast<int>(static_cast<int16_t>(drawingAreaRight)) - originX);
    const int rowFirst = std::max(0, static_cast<int>(static_cast<int16_t>(drawingAreaTop)) - originY);
    const int rowLast = std::min(static_cast<int>(height) - 1, static_cast<int>(static_cast<int16_t>(drawingAreaBottom)) - originY);

	drawCost += Hardware::GPU_COST_PRIMITIVE_SETUP;
    if (rowLast >= rowFirst && columnLast >= columnFirst) {
        const double perPixel = Hardware::GPU_COST_PIXEL_BASE + (isTextured ? Hardware::GPU_COST_PIXEL_TEXTURED : 0.0) + (isSemiTransparent ? Hardware::GPU_COST_PIXEL_BLEND : 0.0);
        drawCost += double(rowLast - rowFirst + 1) * (double(columnLast - columnFirst + 1) * perPixel + Hardware::GPU_COST_ROW_SETUP);
    }

    for (int row = rowFirst; row <= rowLast; row++) {
        for (int column = columnFirst; column <= columnLast; column++) {
            int16_t pixelX = originX + column;
            int16_t pixelY = originY + row;

            uint16_t color = flatPacked;

            // Untextured rectangles blend every pixel. Textured ones decide per texel.
            bool blendThisPixel = isSemiTransparent && !isTextured;

            if (isTextured) {
                int textureCoordinateX = startU + (textureFlipX ? -column : column);
                int textureCoordinateY = startV + (textureFlipY ? -row : row);

                textureCoordinateX = (textureCoordinateX & ~(textureWindowMaskX * 8)) | ((textureWindowOffsetX & textureWindowMaskX) * 8);
                textureCoordinateY = (textureCoordinateY & ~(textureWindowMaskY * 8)) | ((textureWindowOffsetY & textureWindowMaskY) * 8);

                // PS1 textures are strictly bound to 256x256 pages
                textureCoordinateX &= 0xFF;
                textureCoordinateY &= 0xFF;

                uint16_t texel = sampleTexture(textureCoordinateX, textureCoordinateY, colorLookupTableX, colorLookupTableY);

                if (texel == 0) continue;

                blendThisPixel = isSemiTransparent && (texel & 0x8000);

                if (!isRawTexture) {
                    // 5-bit texel x 8-bit color, no dither for rectangles. Texel bit 15 is kept.
                    color = packColor(modulate(texel & 0x1F, colorRed),
                                      modulate((texel >> 5) & 0x1F, colorGreen),
                                      modulate((texel >> 10) & 0x1F, colorBlue),
                                      pixelX, pixelY, false) | (texel & 0x8000);
                }
                else {
                    color = texel;
                }
            }

            if (maskPreserve && (vram.readPixel(pixelX, pixelY) & 0x8000)) {
                continue;
            }
            if (blendThisPixel) {
                color = blendSemiTransparent(vram.readPixel(pixelX, pixelY), color);
            }
            if (maskForceSet) {
                color |= 0x8000;
            }
            vram.writePixel(pixelX, pixelY, color);
        }
    }
}


void Rasterizer::fillRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color) {
	drawCost += 46.0 + double(height) * (width / 8.0 + 9.0);   // block fills are fast on hardware

	// The PS1 Block Fill command explicitly ignores both the Drawing Offset and the Drawing Area clip. It wraps around the 1024x512 VRAM edges.
    for (uint16_t row = 0; row < height; row++) {
        for (uint16_t column = 0; column < width; column++) {
            uint16_t pixelX = (x + column) & (Hardware::VRAM_WIDTH - 1);
            uint16_t pixelY = (y + row) & (Hardware::VRAM_HEIGHT - 1);

            if (maskPreserve && (vram.readPixel(pixelX, pixelY) & 0x8000)) {
                continue;
            }

            vram.writePixel(pixelX, pixelY, color);
        }
    }
}


void Rasterizer::drawLine(const Vertex& vertex0, const Vertex& vertex1, bool isSemiTransparent, bool isGouraud) {
	int startX = vertex0.x + drawingOffsetX;
	int startY = vertex0.y + drawingOffsetY;
	int endX = vertex1.x + drawingOffsetX;
	int endY = vertex1.y + drawingOffsetY;
	uint32_t startColor = vertex0.color;
	uint32_t endColor = vertex1.color;

	const int absoluteDeltaX = std::abs(endX - startX);
	const int absoluteDeltaY = std::abs(endY - startY);
	if (absoluteDeltaX > 1023 || absoluteDeltaY > 511) return;

	const int stepCount = std::max(absoluteDeltaX, absoluteDeltaY);

	// Hardware always walks the line left to right (colors travel with their endpoints)
	if (startX >= endX && stepCount > 0) {
		std::swap(startX, endX);
		std::swap(startY, endY);
		std::swap(startColor, endColor);
	}

	// Position steps have 32 fractional bits and round away from zero
	auto divideAwayFromZero = [stepCount](int64_t delta) -> int64_t {
		delta *= (int64_t(1) << 32);
		if (delta < 0) delta -= stepCount - 1;
		if (delta > 0) delta += stepCount - 1;
		return delta / stepCount;
	};

	int64_t stepX = 0, stepY = 0, stepRed = 0, stepGreen = 0, stepBlue = 0;
	if (stepCount > 0) {
		stepX = divideAwayFromZero(endX - startX);
		stepY = divideAwayFromZero(endY - startY);
		if (isGouraud) {
			stepRed   = (int64_t(int(endColor & 0xFF) - int(startColor & 0xFF)) * 4096) / stepCount;
			stepGreen = (int64_t(int((endColor >> 8) & 0xFF) - int((startColor >> 8) & 0xFF)) * 4096) / stepCount;
			stepBlue  = (int64_t(int((endColor >> 16) & 0xFF) - int((startColor >> 16) & 0xFF)) * 4096) / stepCount;
		}
	}

	// Start at the pixel center minus a tiny epsilon. X always gets the epsilon, Y only when it is decreasing.
	const int64_t one = int64_t(1) << 32;
	int64_t currentX = int64_t(startX) * one + (one >> 1) - 1024;
	int64_t currentY = int64_t(startY) * one + (one >> 1) - (stepY < 0 ? 1024 : 0);
	int64_t currentRed   = (int64_t(startColor & 0xFF) << 12) + 0x800;
	int64_t currentGreen = (int64_t((startColor >> 8) & 0xFF) << 12) + 0x800;
	int64_t currentBlue  = (int64_t((startColor >> 16) & 0xFF) << 12) + 0x800;

	const bool ditherThisDraw = ditherEnabled;

	drawCost += Hardware::GPU_COST_PRIMITIVE_SETUP + double(stepCount) * (Hardware::GPU_COST_PIXEL_BASE + (isSemiTransparent ? Hardware::GPU_COST_PIXEL_BLEND : 0.0));

	for (int step = 0; step <= stepCount; step++, currentX += stepX, currentY += stepY, currentRed += stepRed, currentGreen += stepGreen, currentBlue += stepBlue) {
		const int pixelX = static_cast<int>(currentX >> 32);
		const int pixelY = static_cast<int>(currentY >> 32);

		if (pixelX < static_cast<int16_t>(drawingAreaLeft) || pixelX > static_cast<int16_t>(drawingAreaRight) ||
			pixelY < static_cast<int16_t>(drawingAreaTop)  || pixelY > static_cast<int16_t>(drawingAreaBottom)) {
			continue;
		}

		uint16_t color = packColor(static_cast<int>(currentRed >> 12), static_cast<int>(currentGreen >> 12), static_cast<int>(currentBlue >> 12), pixelX, pixelY, ditherThisDraw);

		if (maskPreserve && (vram.readPixel(pixelX, pixelY) & 0x8000)) continue;
		if (isSemiTransparent) color = blendSemiTransparent(vram.readPixel(pixelX, pixelY), color);
		if (maskForceSet) color |= 0x8000;
		vram.writePixel(pixelX, pixelY, color);
	}
}


void Rasterizer::invalidateCache() {
	cachedClutTag = 0xFFFFFFFF;
	cachedClut4BitValid = false;
	cachedClut8BitValid = false;
}



//Privates
uint16_t Rasterizer::packColor(int red, int green, int blue, int pixelX, int pixelY, bool dither) {
    if (dither) {
        const int offset = kDitherTable[pixelY & 3][pixelX & 3];
        red += offset;
        green += offset;
        blue += offset;
    }

    red = std::clamp(red, 0, 255);
    green = std::clamp(green, 0, 255);
    blue = std::clamp(blue, 0, 255);
    return static_cast<uint16_t>((red >> 3) | ((green >> 3) << 5) | ((blue >> 3) << 10));
}


bool Rasterizer::isTopLeftEdge(int16_t startX, int16_t startY, int16_t endX, int16_t endY) const {
	int deltaX = endX - startX;
	int deltaY = endY - startY;
	return (deltaY == 0 && deltaX > 0) || deltaY < 0;
}


void Rasterizer::loadCLUT(uint16_t clutX, uint16_t clutY) {
    if (texturePageColorDepth == 2) return; // 15-bit textures bypass CLUT completely

    uint32_t currentTag = (static_cast<uint32_t>(clutY) << 16) | clutX;

    if (currentTag != cachedClutTag) {
        cachedClutTag = currentTag;
        cachedClut4BitValid = false;
        cachedClut8BitValid = false;
    }

    if (texturePageColorDepth == 0) { // 4-bit
        if (!cachedClut4BitValid) {
            for (int i = 0; i < 16; i++) {
                activeCLUT[i] = vram.readPixel((clutX + i) & 0x3FF, clutY & 0x1FF);
            }
            cachedClut4BitValid = true;
        }
    }
    else if (texturePageColorDepth == 1) { // 8-bit
        if (!cachedClut8BitValid) {
            // CRITICAL: Fetch ALL 256 colors from 0, overwriting the first 16 if they existed.
            for (int i = 0; i < 256; i++) {
                activeCLUT[i] = vram.readPixel((clutX + i) & 0x3FF, clutY & 0x1FF);
            }
            cachedClut4BitValid = true;
            cachedClut8BitValid = true;
        }
    }
}


void Rasterizer::drawTriangle(const Vertex& vertex0, const Vertex& vertex1, const Vertex& vertex2, bool isTextured, bool isSemiTransparent, bool isRawTexture, bool isGouraud, uint16_t colorLookupTableX, uint16_t colorLookupTableY) {
	// Signed area in the ORIGINAL vertex order. It is the denominator of the attribute gradients below.
	const int32_t denominator = edgeFunction(vertex0.x, vertex0.y, vertex1.x, vertex1.y, vertex2.x, vertex2.y);
	if (denominator == 0) return; // Degenerate triangle

	// Real hardware silently drops any triangle with an edge wider than 1023 or taller than 511
	auto tooLarge = [](const Vertex& a, const Vertex& b) {
		return std::abs(a.x - b.x) > 1023 || std::abs(a.y - b.y) > 511;
	};
	if (tooLarge(vertex0, vertex1) || tooLarge(vertex1, vertex2) || tooLarge(vertex2, vertex0)) return;

	// Winding-normalized copies, used only for the edge (coverage) test
	Vertex first = vertex0;
	Vertex second = vertex1;
	Vertex third = vertex2;
	if (denominator < 0) {
		std::swap(second, third);
	}

	int16_t minX = std::min({first.x, second.x, third.x});
	int16_t maxX = std::max({first.x, second.x, third.x});
	int16_t minY = std::min({first.y, second.y, third.y});
	int16_t maxY = std::max({first.y, second.y, third.y});

	minX = std::max((int16_t)0, minX);
	maxX = std::min((int16_t)(Hardware::VRAM_WIDTH - 1), maxX);
	minY = std::max((int16_t)0, minY);
	maxY = std::min((int16_t)(Hardware::VRAM_HEIGHT - 1), maxY);

	clipToDrawingArea(minX, maxX, minY, maxY);

	// The GPU does not do per-pixel barycentric weights. It derives a per-attribute gradient (d/dx, d/dy)
	// in fixed point (12 fractional bits), anchors it at the first vertex with a +0.5 rounding bias,
	// and steps from there. The truncation in these gradients is what real UV/color interpolation looks like.
	struct Plane { int32_t dx = 0; int32_t dy = 0; };

	auto makePlane = [&](int c0, int c1, int c2) -> Plane {
		const int64_t numeratorX = int64_t(c1 - c0) * (vertex2.y - vertex0.y) - int64_t(c2 - c0) * (vertex1.y - vertex0.y);
		const int64_t numeratorY = int64_t(c2 - c0) * (vertex1.x - vertex0.x) - int64_t(c1 - c0) * (vertex2.x - vertex0.x);
		Plane plane;
		plane.dx = static_cast<int32_t>(numeratorX * 4096 / denominator);
		plane.dy = static_cast<int32_t>(numeratorY * 4096 / denominator);
		return plane;
	};

	// Anchor = the first vertex as given (hardware does not sort by Y for attribute interpolation)
	const Vertex* anchor = &vertex0;

	Plane planeRed, planeGreen, planeBlue, planeU, planeV;
	if (isGouraud) {
		planeRed   = makePlane(vertex0.color & 0xFF, vertex1.color & 0xFF, vertex2.color & 0xFF);
		planeGreen = makePlane((vertex0.color >> 8) & 0xFF, (vertex1.color >> 8) & 0xFF, (vertex2.color >> 8) & 0xFF);
		planeBlue  = makePlane((vertex0.color >> 16) & 0xFF, (vertex1.color >> 16) & 0xFF, (vertex2.color >> 16) & 0xFF);
	}
	if (isTextured) {
		planeU = makePlane(vertex0.u, vertex1.u, vertex2.u);
		planeV = makePlane(vertex0.v, vertex1.v, vertex2.v);
	}

	const bool isEdge0TopLeft = isTopLeftEdge(second.x, second.y, third.x, third.y);
	const bool isEdge1TopLeft = isTopLeftEdge(third.x, third.y, first.x, first.y);
	const bool isEdge2TopLeft = isTopLeftEdge(first.x, first.y, second.x, second.y);

	// Dither applies to Gouraud polygons and to texture-modulated polygons (not raw textures, not flat untextured)
	const bool ditherThisDraw = ditherEnabled && (isGouraud || (isTextured && !isRawTexture));

	if (minX > maxX || minY > maxY) return;

	// A pixel is inside when all three edge functions are >= bias, where bias is 0 for a top/left edge (a pixel exactly on
	// it belongs to the triangle) and 1 otherwise (it must be strictly inside). Each edge function is linear, so along one
	// scanline the covered pixels form a single span that can be solved for exactly with integer division. This visits
	// the same pixels as testing every pixel of the bounding box, without testing the ones outside the triangle.
	struct Edge {
		int32_t valueAtRowStart;  // edge function at (minX, current row)
		int32_t stepPerPixel;     // change per +1 in x
		int32_t stepPerRow;       // change per +1 in y
		int32_t bias;
	};
	auto makeEdge = [&](const Vertex& a, const Vertex& b, bool topLeft) -> Edge {
		Edge edge;
		edge.valueAtRowStart = edgeFunction(a.x, a.y, b.x, b.y, minX, minY);
		edge.stepPerPixel = -(b.y - a.y);
		edge.stepPerRow = b.x - a.x;
		edge.bias = topLeft ? 0 : 1;
		return edge;
	};
	Edge edges[3] = {
		makeEdge(second, third, isEdge0TopLeft),
		makeEdge(third, first, isEdge1TopLeft),
		makeEdge(first, second, isEdge2TopLeft),
	};

	// Attribute value at pixel (x, y), as the original per-pixel evaluate() computed it, kept as a running sum
	// along the row: value(x + 1) = value(x) + plane.dx exactly (all integer, no rounding involved).
	auto attributeAtRowSpanStart = [&](const Plane& plane, int anchorValue, int startX, int pixelY) -> int64_t {
		return (int64_t(anchorValue) << 12) + 0x800
			+ int64_t(plane.dx) * (startX - anchor->x)
			+ int64_t(plane.dy) * (pixelY - anchor->y);
	};

	const int anchorRed   = vertex0.color & 0xFF;
	const int anchorGreen = (vertex0.color >> 8) & 0xFF;
	const int anchorBlue  = (vertex0.color >> 16) & 0xFF;

	drawCost += Hardware::GPU_COST_PRIMITIVE_SETUP;
	const double perPixelCost = Hardware::GPU_COST_PIXEL_BASE + (isTextured ? Hardware::GPU_COST_PIXEL_TEXTURED : 0.0) + (isSemiTransparent ? Hardware::GPU_COST_PIXEL_BLEND : 0.0);


	for (int pixelY = minY; pixelY <= maxY; pixelY++) {
		// Solve for the span [spanFirst, spanLast] of x in [minX, maxX] covered on this row
		int spanFirst = minX;
		int spanLast = maxX;
		for (int edgeIndex = 0; edgeIndex < 3 && spanFirst <= spanLast; edgeIndex++) {
			const Edge& edge = edges[edgeIndex];
			const int64_t slack = int64_t(edge.valueAtRowStart) - edge.bias; // need slack + stepPerPixel * k >= 0, k = x - minX
			if (edge.stepPerPixel == 0) {
				if (slack < 0) spanFirst = maxX + 1; // whole row outside
			}
			else if (edge.stepPerPixel > 0) {
				if (slack < 0) { // first k that makes it non-negative
					const int64_t firstK = (-slack + edge.stepPerPixel - 1) / edge.stepPerPixel;
					spanFirst = std::max<int64_t>(spanFirst, minX + firstK);
				}
			}
			else {
				if (slack < 0) spanFirst = maxX + 1; // already negative and getting more negative
				else spanLast = std::min<int64_t>(spanLast, minX + slack / (-int64_t(edge.stepPerPixel)));
			}
		}
		for (Edge& edge : edges) edge.valueAtRowStart += edge.stepPerRow; // advance to the next row

		if (spanFirst > spanLast) continue;

		drawCost += double(spanLast - spanFirst + 1) * perPixelCost + Hardware::GPU_COST_ROW_SETUP;


		int64_t valueRed = 0, valueGreen = 0, valueBlue = 0, valueU = 0, valueV = 0;
		if (isGouraud) {
			valueRed   = attributeAtRowSpanStart(planeRed,   anchorRed,   spanFirst, pixelY);
			valueGreen = attributeAtRowSpanStart(planeGreen, anchorGreen, spanFirst, pixelY);
			valueBlue  = attributeAtRowSpanStart(planeBlue,  anchorBlue,  spanFirst, pixelY);
		}
		if (isTextured) {
			valueU = attributeAtRowSpanStart(planeU, anchor->u, spanFirst, pixelY);
			valueV = attributeAtRowSpanStart(planeV, anchor->v, spanFirst, pixelY);
		}

		for (int pixelX = spanFirst; pixelX <= spanLast; pixelX++,
		     valueRed += planeRed.dx, valueGreen += planeGreen.dx, valueBlue += planeBlue.dx, valueU += planeU.dx, valueV += planeV.dx) {

			int shadeRed = anchorRed;
			int shadeGreen = anchorGreen;
			int shadeBlue = anchorBlue;

			if (isGouraud) {
				shadeRed   = std::clamp(static_cast<int>(valueRed >> 12),   0, 255);
				shadeGreen = std::clamp(static_cast<int>(valueGreen >> 12), 0, 255);
				shadeBlue  = std::clamp(static_cast<int>(valueBlue >> 12),  0, 255);
			}

			uint16_t finalColor;

			bool blendThisPixel = isSemiTransparent && !isTextured;

			if (isTextured) {
				int textureCoordinateX = static_cast<int>(valueU >> 12);
				int textureCoordinateY = static_cast<int>(valueV >> 12);

				textureCoordinateX = (textureCoordinateX & ~(textureWindowMaskX * 8)) | ((textureWindowOffsetX & textureWindowMaskX) * 8);
				textureCoordinateY = (textureCoordinateY & ~(textureWindowMaskY * 8)) | ((textureWindowOffsetY & textureWindowMaskY) * 8);

				textureCoordinateX &= 0xFF;
				textureCoordinateY &= 0xFF;

				uint16_t texel = sampleTexture(textureCoordinateX, textureCoordinateY, colorLookupTableX, colorLookupTableY);
				if (texel == 0) continue; // 0x0000 is fully transparent in PS1 textures

				blendThisPixel = isSemiTransparent && (texel & 0x8000);

				if (!isRawTexture) {
					finalColor = packColor(modulate(texel & 0x1F, shadeRed),
					                       modulate((texel >> 5) & 0x1F, shadeGreen),
					                       modulate((texel >> 10) & 0x1F, shadeBlue),
					                       pixelX, pixelY, ditherThisDraw) | (texel & 0x8000);
				}
				else {
					finalColor = texel;
				}
			}
			else {
				finalColor = packColor(shadeRed, shadeGreen, shadeBlue, pixelX, pixelY, ditherThisDraw);
			}

			if (maskPreserve && (vram.readPixel(pixelX, pixelY) & 0x8000)) {
				continue;
			}
			if (blendThisPixel) {
				finalColor = blendSemiTransparent(vram.readPixel(pixelX, pixelY), finalColor);
			}
			if (maskForceSet) {
				finalColor |= 0x8000;
			}
			vram.writePixel(pixelX, pixelY, finalColor);
		}
	}
}


uint16_t Rasterizer::blendSemiTransparent(uint16_t backgroundColor, uint16_t foregroundColor) const {
	int blendedChannels[3];

	for (int channel = 0; channel < 3; channel++) {
		int shift = channel * 5;
		int background = (backgroundColor >> shift) & 0x1F;
		int foreground = (foregroundColor >> shift) & 0x1F;
		int result = 0;

		switch (texturePageSemiTransparency) {
			case 0: result = (background + foreground) >> 1; break; // B/2 + F/2
			case 1: result = background + foreground; break;        // B + F
			case 2: result = background - foreground; break;        // B - F
			case 3: result = background + (foreground >> 2); break; // B + F/4
		}

		blendedChannels[channel] = std::clamp(result, 0, 31);
	}

	return blendedChannels[0] | (blendedChannels[1] << 5) | (blendedChannels[2] << 10);
}


void Rasterizer::clipToDrawingArea(int16_t& minX, int16_t& maxX, int16_t& minY, int16_t& maxY) {
    minX = std::max(minX, (int16_t)std::max(0, (int)drawingAreaLeft));
    maxX = std::min(maxX, (int16_t)std::min((int)Hardware::VRAM_WIDTH - 1, (int)drawingAreaRight));
    minY = std::max(minY, (int16_t)std::max(0, (int)drawingAreaTop));
    maxY = std::min(maxY, (int16_t)std::min((int)Hardware::VRAM_HEIGHT - 1, (int)drawingAreaBottom));
}


uint16_t Rasterizer::sampleTexture(int textureX, int textureY, uint16_t colorLookupTableX, uint16_t colorLookupTableY) {
    // The texture page sits at an arbitrary VRAM X, so a 8-bit / 15-bit texture can run off the right
    // edge of VRAM. Hardware wraps around to the left side rather than clamping or reading out of bounds.
    switch (texturePageColorDepth) {
        case 0: { // 4-bit CLUT
            int videoRamX = (texturePageBaseX + (textureX / 4)) & 0x3FF;
            int videoRamY = (texturePageBaseY + textureY) & 0x1FF;

            uint16_t pixelDataWord = vram.readPixel(videoRamX, videoRamY);
            int bitShift = (textureX % 4) * 4;
            uint8_t colorLookupTableIndex = (pixelDataWord >> bitShift) & 0xF;

            return activeCLUT[colorLookupTableIndex];
        }
        case 1: { // 8-bit CLUT
            int videoRamX = (texturePageBaseX + (textureX / 2)) & 0x3FF;
            int videoRamY = (texturePageBaseY + textureY) & 0x1FF;

            uint16_t pixelDataWord = vram.readPixel(videoRamX, videoRamY);
            int bitShift = (textureX % 2) * 8;
            uint8_t colorLookupTableIndex = (pixelDataWord >> bitShift) & 0xFF;

            return activeCLUT[colorLookupTableIndex];
        }
        default: { // 15-bit Direct (1 pixel per 16-bit word, no CLUT)
            int videoRamX = (texturePageBaseX + textureX) & 0x3FF;
            int videoRamY = (texturePageBaseY + textureY) & 0x1FF;

            return vram.readPixel(videoRamX, videoRamY);
        }
    }
}

