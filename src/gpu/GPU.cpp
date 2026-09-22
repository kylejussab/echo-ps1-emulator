#include "GPU.h"
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <iomanip>

GPU::GPU() : rasterizer(vram) {}


uint32_t GPU::readGP0() {
    if (vramReadWordsRemaining <= 0) {
        return gpuReadLatch;
    }

    uint16_t firstPixel = vram.readPixel(readTransferXCursor, readTransferYCursor);
    readTransferXCursor++;
    if (readTransferXCursor >= readTransferX + readTransferWidth) {
        readTransferXCursor = readTransferX;
        readTransferYCursor++;
    }

    uint16_t secondPixel = vram.readPixel(readTransferXCursor, readTransferYCursor);
    readTransferXCursor++;
    if (readTransferXCursor >= readTransferX + readTransferWidth) {
        readTransferXCursor = readTransferX;
        readTransferYCursor++;
    }

    vramReadWordsRemaining--;
    if (vramReadWordsRemaining <= 0) {
        gpuStatusRegister &= ~0x08000000; // Clear Bit 27
    }

    return static_cast<uint32_t>(firstPixel) | (static_cast<uint32_t>(secondPixel) << 16);
}


uint32_t GPU::readGP1() {
    // Force Bits 26, 27, and 28 high (Idle and Ready)
    gpuStatusRegister |= 0x1C000000;
    updateDMARequestBit();
    return gpuStatusRegister;
}


void GPU::writeGP0(uint32_t value) {
    if (wordsRemaining > 0 || parametersRemaining > 0) {
        continuePendingCommand(value);
        return;
    }

    beginNewCommand(value);
}


void GPU::writeGP1(uint32_t value) {
    uint8_t commandType = value >> 24;

    switch (commandType) {
        case 0x00: { // Reset GPU
            vram.setDisplayArea(0, 0);
            wordsRemaining = 0;
            parametersRemaining = 0;
            gpuStatusRegister = Hardware::GPU_DEFAULT_STATUS;
            break;
        }
        case 0x01: { // Reset Command Buffer
            wordsRemaining = 0;
            parametersRemaining = 0;
            break;
        }
        case 0x02: { // Acknowledge Interrupt
            gpuStatusRegister &= ~0x01000000; // Clear Bit 24
            break;
        }
        case 0x03: { // Display Enable
            uint32_t displayEnable = value & 0x01;
            gpuStatusRegister &= ~0x00800000; // Clear Bit 23
            gpuStatusRegister |= (displayEnable << 23);
            break; 
        }
        case 0x04: { // DMA Direction
            DMADirection = value & 0x3;
            gpuStatusRegister &= ~0x60000000; // Clear Bits 29-30
            gpuStatusRegister |= (static_cast<uint32_t>(DMADirection) << 29);
            updateDMARequestBit();
            break;
        }
        case 0x05: { // Start of Display Area
            uint16_t x = value & 0x3FF;
            uint16_t y = (value >> 10) & 0x1FF;
            vram.setDisplayArea(x, y);
            break;
        }
        case 0x06: break;
        case 0x07: break;
        case 0x08: { // Display Mode
            uint32_t displayBits = value & 0x3F;
            gpuStatusRegister &= ~0x007E0000; // Clear Bits 17-22 
            gpuStatusRegister |= (displayBits << 17);

            bool is480Lines = (value >> 2) & 0x1;
            uint16_t height = is480Lines ? 480 : 240;
            uint16_t width = 320;

            bool horizontalResolutionOverride = (value >> 6) & 0x01;
            if (horizontalResolutionOverride) {
                width = 368;
            } 
            else {
                switch (value & 0x03) {
                    case 0: width = 256; break;
                    case 1: width = 320; break;
                    case 2: width = 512; break;
                    case 3: width = 640; break;
                }
            }
            vram.setDisplayDimensions(width, height);
            break;
        }
        case 0x10: { // Get GPU Info
            switch (value & 0x0F) {
                case 7: gpuReadLatch = 2; break; // GPU version
                case 8: gpuReadLatch = 0; break;
                default: break;                  // unimplemented queries leave the latch unchanged
            }
            break;
        }
        default: {
            std::stringstream ss;
            ss << "FATAL: Unhandled GP1 command header: 0x" << std::hex << static_cast<int>(commandType);
            throw std::runtime_error(ss.str());
        }
    }
}



void GPU::continuePendingCommand(uint32_t value) {
    uint8_t commandType = currentCommand >> 24;

    if ((commandType & 0xE0) == 0x60) { // Rectangles (0x60-0x7F): textured/semi-transparent/raw-texture/size-mode
        handleRectangleWord(value);
        return;
    }

    if ((commandType & 0xF0) == 0x30) { // Gouraud-shaded triangles/quads (0x30-0x3F): same story — textured, semi-transparent, raw-texture, and quad-vs-triangle
        handleGouraudPolygonWord(value);
        return;
    }

    if ((commandType & 0xF4) == 0x24) { // Textured, flat-shaded triangles/quads (0x24-0x27 and 0x2C-0x2F)
        handleTexturedPolygonWord(value);
        return;
    }

    switch (commandType) {
        case 0x02: { // Fill Rectangle in VRAM
            handleFillRectangleWord(value);
            break;
        }
        case 0x20: case 0x21: case 0x22: case 0x23: { // Monotone Triangle
            // Nothing in here yet
        }
        case 0x28: case 0x29: case 0x2A: case 0x2B: { // Monotone Quad
            handleMonotoneQuadWord(value);
            break;
        }
        case 0xA0: { // Copy CPU to VRAM
            handleCopyCPUToVRAMWord(value);
            break;
        }
        case 0xC0: { // Copy VRAM to CPU
            handleCopyVRAMToCPUParameters(value);
            break;
        }
        default: {
            std::stringstream ss;
            ss << "FATAL: Unhandled multi-word GPU command: 0x" << std::hex << static_cast<int>(commandType) << " (Params remaining: " << std::dec << parametersRemaining << ")";
            throw std::runtime_error(ss.str());
        }
    }
}


void GPU::beginNewCommand(uint32_t value) {
    currentCommand = value;
    uint8_t commandType = value >> 24;

    if ((commandType & 0xE0) == 0x60) { // Rectangles (0x60-0x7F)
		bool textured = (commandType >> 2) & 0x01;
		uint8_t sizeMode = (commandType >> 3) & 0x03;

		rectangleTextured = textured;
		rectangleSemiTransparent = (commandType >> 1) & 0x01;
		rectangleRawTexture = commandType & 0x01;
		rectangleSizeMode = sizeMode;

		switch (sizeMode) {
			case 1: rectangleWidth = 1;  rectangleHeight = 1;  break;
			case 2: rectangleWidth = 8;  rectangleHeight = 8;  break;
			case 3: rectangleWidth = 16; rectangleHeight = 16; break;
			default: break; // 0 = variable size, width/height come from a parameter word
		}

		rectangleParametersExpected = 1 + (textured ? 1 : 0) + (sizeMode == 0 ? 1 : 0);
		parametersRemaining = rectangleParametersExpected;
		wordsRemaining = 0;
		return;
	}

    if ((commandType & 0xF0) == 0x30) { // Gouraud-shaded triangle/quad (0x30-0x3F)
        bool isQuad = (commandType >> 3) & 0x01;
        primitiveIsGouraud = true;
        primitiveIsTextured = (commandType >> 2) & 0x01;
        primitiveIsSemiTransparent = (commandType >> 1) & 0x01;
        primitiveIsRawTexture = commandType & 0x01;
        
        primitiveVertexCount = isQuad ? 4 : 3;
        primitiveCurrentVertexIndex = 0;

        uint8_t red = value & 0xFF;
        uint8_t green = (value >> 8) & 0xFF;
        uint8_t blue = (value >> 16) & 0xFF;
        primitiveVertices[0].color = ((red >> 3) & 0x1F) | (((green >> 3) & 0x1F) << 5) | (((blue >> 3) & 0x1F) << 10);

        gouraudPolygonWordSequence.clear();
        for (int vertexIndex = 0; vertexIndex < primitiveVertexCount; vertexIndex++) {
            if (vertexIndex > 0) gouraudPolygonWordSequence.push_back(GouraudPolygonWordRole::Color);
            gouraudPolygonWordSequence.push_back(GouraudPolygonWordRole::Position);
            if (primitiveIsTextured) gouraudPolygonWordSequence.push_back(GouraudPolygonWordRole::Texture);
        }
        gouraudPolygonWordCursor = 0;

        wordsRemaining = static_cast<int>(gouraudPolygonWordSequence.size());
        parametersRemaining = 0;
        return;
    }

    if ((commandType & 0xF4) == 0x24) { // Textured triangle (0x24-0x27) / quad (0x2C-0x2F)
        bool isQuad = (commandType >> 3) & 0x01;
        primitiveIsGouraud = false;
        primitiveIsTextured = true;
        primitiveIsRawTexture = commandType & 0x01;
        primitiveIsSemiTransparent = (commandType >> 1) & 0x01;
        primitiveVertexCount = isQuad ? 4 : 3;

        wordsRemaining = primitiveVertexCount * 2;
        parametersRemaining = 0;
        return;
    }

    switch (commandType) {
        case 0x00: { // NOP
            parametersRemaining = 0;
            wordsRemaining = 0;
            break;
        }
        case 0x01: { // Clear Cache
            parametersRemaining = 0;
            wordsRemaining = 0;
            break;
        }
        case 0x02: { // Fill Rectangle in Video Ram
            parametersRemaining = 2;
            wordsRemaining = 0;
            break;
        }
        case 0x1F: { // Interrupt Request (IRQ1)
            gpuStatusRegister |= 0x01000000; // Set Bit 24 high
            interruptRequestFired = true; 
            parametersRemaining = 0;
            wordsRemaining = 0;
            break;
        }
        case 0x20: case 0x21: case 0x22: case 0x23: { // Monotone Triangle
            primitiveVertexCount = 3;
            primitiveIsGouraud = false;
            primitiveIsTextured = false;
            parametersRemaining = 0;
            wordsRemaining = 3;
            break;
        }
        case 0x28: case 0x29: case 0x2A: case 0x2B: { // Monotone Quad
            primitiveVertexCount = 4;
            primitiveIsGouraud = false;
            primitiveIsTextured = false;
            parametersRemaining = 0;
            wordsRemaining = 4;
            break;
        }
        case 0x60: case 0x68: case 0x70: case 0x78: {  // Rectangles
            parametersRemaining = 1;
            wordsRemaining = 0;
            break;
        }
        case 0x80: { // Copy VRAM to VRAM
            parametersRemaining = 3;
            wordsRemaining = 0;
            break;
        }
        case 0xA0: { // Copy CPU to VRAM
            parametersRemaining = 2;
            wordsRemaining = 0;
            break;
        }
        case 0xC0: { // Copy VRAM to CPU
            parametersRemaining = 2;
            wordsRemaining = 0;
            break;
        }
        case 0xE1: { // Draw Mode setting (texture page)
			uint16_t baseX = (value & 0x0F) * 64;
			uint16_t baseY = ((value >> 4) & 0x01) * 256;
			uint8_t semiTransparency = (value >> 5) & 0x03;
			uint8_t colorDepth = (value >> 7) & 0x03;
			bool flipX = (value >> 12) & 0x01;
			bool flipY = (value >> 13) & 0x01;

			rasterizer.setTexturePage(baseX, baseY, colorDepth, semiTransparency);
			rasterizer.setTextureFlip(flipX, flipY);

			parametersRemaining = 0;
			wordsRemaining = 0;
			break;
		}
		case 0xE2: { // Texture window — stored nowhere yet, safe to no-op for now
            parametersRemaining = 0;
            wordsRemaining = 0;
            break;
        }
        case 0xE3: { // Drawing area top-left
			uint16_t left = value & 0x3FF;
            uint16_t top = (value >> 10) & 0x1FF;
            rasterizer.setDrawingAreaTopLeft(left, top);
            parametersRemaining = 0;
            wordsRemaining = 0;
            break;
		}
		case 0xE4: { // Drawing area bottom-right
			uint16_t right = value & 0x3FF;
            uint16_t bottom = (value >> 10) & 0x1FF;
            rasterizer.setDrawingAreaBottomRight(right, bottom);
            parametersRemaining = 0;
            wordsRemaining = 0;
            break;
		}
		case 0xE5: { // Drawing offset
			int16_t offsetX = value & 0x7FF;
			if (offsetX & 0x400) offsetX |= 0xF800; // sign-extend 11-bit
			int16_t offsetY = (value >> 11) & 0x7FF;
			if (offsetY & 0x400) offsetY |= 0xF800;

			rasterizer.setDrawingOffset(offsetX, offsetY);
			parametersRemaining = 0;
			wordsRemaining = 0;
			break;
		}
		case 0xE6: { // Mask bit setting, not implemented yet, safe to no-op for now
			parametersRemaining = 0;
			wordsRemaining = 0;
			break;
		}
        default: {
            std::stringstream ss;
            ss << "FATAL: Unhandled GP0 command: 0x" << std::hex << static_cast<int>(commandType);
            throw std::runtime_error(ss.str());
        }
    }
}


void GPU::handleFillRectangleWord(uint32_t value) {
    if (parametersRemaining == 2) {
        transferX = value & 0xFFFF;
        transferY = (value >> 16) & 0xFFFF;
        parametersRemaining--;
        return;
    }

    if (parametersRemaining == 1) {
        transferWidth = value & 0xFFFF;
        transferHeight = (value >> 16) & 0xFFFF;

        uint8_t red = currentCommand & 0xFF;
        uint8_t green = (currentCommand >> 8) & 0xFF;
        uint8_t blue = (currentCommand >> 16) & 0xFF;
        uint16_t fillColor = ((red >> 3) & 0x1F) | (((green >> 3) & 0x1F) << 5) | (((blue >> 3) & 0x1F) << 10);

        rasterizer.fillRectangle(transferX, transferY, transferWidth, transferHeight, fillColor);

        parametersRemaining--;
        return;
    }
}


void GPU::handleMonotoneQuadWord(uint32_t value) {
    int vertexIndex = primitiveVertexCount - wordsRemaining; 

    int16_t rawX = value & 0xFFFF;
    if (rawX & 0x400) rawX |= 0xF800; 

    int16_t rawY = (value >> 16) & 0xFFFF;
    if (rawY & 0x400) rawY |= 0xF800; 

    primitiveVertices[vertexIndex].x = rawX;
    primitiveVertices[vertexIndex].y = rawY;

    uint8_t red   = currentCommand & 0xFF;
    uint8_t green = (currentCommand >> 8) & 0xFF;
    uint8_t blue  = (currentCommand >> 16) & 0xFF;
    primitiveVertices[vertexIndex].color = ((red >> 3) & 0x1F) | (((green >> 3) & 0x1F) << 5) | (((blue >> 3) & 0x1F) << 10);

    wordsRemaining--;

    if (wordsRemaining == 0) {
        rasterizer.drawPolygon(primitiveVertices, primitiveVertexCount, primitiveIsTextured, primitiveIsSemiTransparent, primitiveIsRawTexture, primitiveIsGouraud, primitiveColorLookupTableX, primitiveColorLookupTableY);
    }
}


void GPU::handleCopyCPUToVRAMWord(uint32_t value) {
    if (parametersRemaining == 2) {
        transferX = value & 0xFFFF;
        transferY = (value >> 16) & 0xFFFF;
        transferXCursor = transferX;
        transferYCursor = transferY;
        parametersRemaining--;
        return;
    }

    if (parametersRemaining == 1) {
        transferWidth = value & 0xFFFF;
        transferHeight = (value >> 16) & 0xFFFF;
        uint32_t totalPixels = static_cast<uint32_t>(transferWidth) * static_cast<uint32_t>(transferHeight);
        wordsRemaining = (totalPixels + 1) / 2;
        parametersRemaining--;
        return;
    }

    uint16_t firstPixel = value & 0xFFFF;
    uint16_t secondPixel = (value >> 16) & 0xFFFF;

    vram.writePixel(transferXCursor, transferYCursor, firstPixel);
    transferXCursor++;
    if (transferXCursor >= transferX + transferWidth) {
        transferXCursor = transferX;
        transferYCursor++;
    }

    vram.writePixel(transferXCursor, transferYCursor, secondPixel);
    transferXCursor++;
    if (transferXCursor >= transferX + transferWidth) {
        transferXCursor = transferX;
        transferYCursor++;
    }

    wordsRemaining--;
}


void GPU::handleCopyVRAMToCPUParameters(uint32_t value) {
	if (parametersRemaining == 2) {
		readTransferX = value & 0xFFFF;
		readTransferY = (value >> 16) & 0xFFFF;
		readTransferXCursor = readTransferX;
		readTransferYCursor = readTransferY;
		parametersRemaining--;
		return;
	}

	if (parametersRemaining == 1) {
		readTransferWidth = value & 0xFFFF;
		readTransferHeight = (value >> 16) & 0xFFFF;

		uint32_t totalPixels = static_cast<uint32_t>(readTransferWidth) * static_cast<uint32_t>(readTransferHeight);
		vramReadWordsRemaining = (totalPixels + 1) / 2;

		gpuStatusRegister |= 0x08000000; // Set Bit 27 (Ready to send VRAM to CPU)
		parametersRemaining--;
		return;
	}
}


void GPU::handleRectangleWord(uint32_t value) {
    int parameterWordIndex = rectangleParametersExpected - parametersRemaining;

    if (parameterWordIndex == 0) {
        rectangleX = static_cast<int16_t>(value & 0xFFFF);
        rectangleY = static_cast<int16_t>((value >> 16) & 0xFFFF);

        parametersRemaining--;
        if (parametersRemaining == 0) finishRectangleSetup();
        return;
    }

    if (rectangleTextured && parameterWordIndex == 1) {
        rectangleTextureCoordinateU = value & 0xFF;
        rectangleTextureCoordinateV = (value >> 8) & 0xFF;
        rectangleColorLookupTableX = ((value >> 16) & 0x3F) * 16;
        rectangleColorLookupTableY = (value >> 22) & 0x1FF;

        parametersRemaining--;
        if (parametersRemaining == 0) finishRectangleSetup();
        return;
    }

    rectangleWidth = value & 0x3FF;
    rectangleHeight = (value >> 16) & 0x3FF;

    parametersRemaining--;
    finishRectangleSetup();
}


void GPU::handleTexturedPolygonWord(uint32_t value) {
    int totalWords = primitiveVertexCount * 2;
    int wordIndex = totalWords - wordsRemaining;
    int vertexIndex = wordIndex / 2;
    bool isTextureCoordinateWord = (wordIndex % 2) == 1;

    if (!isTextureCoordinateWord) {
        int16_t rawX = value & 0xFFFF;
        if (rawX & 0x400) rawX |= 0xF800;
        
        int16_t rawY = (value >> 16) & 0xFFFF;
        if (rawY & 0x400) rawY |= 0xF800;

        primitiveVertices[vertexIndex].x = rawX;
        primitiveVertices[vertexIndex].y = rawY;
        
        uint8_t red   = currentCommand & 0xFF;
        uint8_t green = (currentCommand >> 8) & 0xFF;
        uint8_t blue  = (currentCommand >> 16) & 0xFF;
        primitiveVertices[vertexIndex].color = ((red >> 3) & 0x1F) | (((green >> 3) & 0x1F) << 5) | (((blue >> 3) & 0x1F) << 10);
    } 
    else {
        primitiveVertices[vertexIndex].u = value & 0xFF;
        primitiveVertices[vertexIndex].v = (value >> 8) & 0xFF;

        if (vertexIndex == 0) {
            primitiveColorLookupTableX = ((value >> 16) & 0x3F) * 16;
            primitiveColorLookupTableY = (value >> 22) & 0x1FF;
        } 
        else if (vertexIndex == 1) {
            uint16_t texturePageValue = (value >> 16) & 0xFFFF;
            uint16_t baseX = (texturePageValue & 0x0F) * 64;
            uint16_t baseY = ((texturePageValue >> 4) & 0x01) * 256;
            uint8_t semiTransparency = (texturePageValue >> 5) & 0x03;
            uint8_t colorDepth = (texturePageValue >> 7) & 0x03;

            rasterizer.setTexturePage(baseX, baseY, colorDepth, semiTransparency);
        }
    }

    wordsRemaining--;

    if (wordsRemaining == 0) {
        rasterizer.drawPolygon(primitiveVertices, primitiveVertexCount, primitiveIsTextured, primitiveIsSemiTransparent, primitiveIsRawTexture, primitiveIsGouraud, primitiveColorLookupTableX, primitiveColorLookupTableY);
    }
}


void GPU::handleGouraudPolygonWord(uint32_t value) {
    GouraudPolygonWordRole role = gouraudPolygonWordSequence[gouraudPolygonWordCursor];
    gouraudPolygonWordCursor++;

    switch (role) {
        case GouraudPolygonWordRole::Color: {
            primitiveCurrentVertexIndex++;
            uint8_t red   = value & 0xFF;
            uint8_t green = (value >> 8) & 0xFF;
            uint8_t blue  = (value >> 16) & 0xFF;
            primitiveVertices[primitiveCurrentVertexIndex].color = 
                ((red >> 3) & 0x1F) | (((green >> 3) & 0x1F) << 5) | (((blue >> 3) & 0x1F) << 10);
            break;
        }
        case GouraudPolygonWordRole::Position: {
            int16_t rawX = value & 0xFFFF;
            if (rawX & 0x400) rawX |= 0xF800;
            
            int16_t rawY = (value >> 16) & 0xFFFF;
            if (rawY & 0x400) rawY |= 0xF800;

            primitiveVertices[primitiveCurrentVertexIndex].x = rawX;
            primitiveVertices[primitiveCurrentVertexIndex].y = rawY;
            break;
        }
        case GouraudPolygonWordRole::Texture: {
            primitiveVertices[primitiveCurrentVertexIndex].u = value & 0xFF;
            primitiveVertices[primitiveCurrentVertexIndex].v = (value >> 8) & 0xFF;

            if (primitiveCurrentVertexIndex == 0) {
                primitiveColorLookupTableX = ((value >> 16) & 0x3F) * 16;
                primitiveColorLookupTableY = (value >> 22) & 0x1FF;
            } 
            else if (primitiveCurrentVertexIndex == 1) {
                uint16_t texturePageValue = (value >> 16) & 0xFFFF;
                uint16_t baseX = (texturePageValue & 0x0F) * 64;
                uint16_t baseY = ((texturePageValue >> 4) & 0x01) * 256;
                uint8_t semiTransparency = (texturePageValue >> 5) & 0x03;
                uint8_t colorDepth = (texturePageValue >> 7) & 0x03;

                rasterizer.setTexturePage(baseX, baseY, colorDepth, semiTransparency);
            }
            break;
        }
    }

    wordsRemaining--;

    if (wordsRemaining == 0) {
        rasterizer.drawPolygon(primitiveVertices, primitiveVertexCount, primitiveIsTextured, primitiveIsSemiTransparent, primitiveIsRawTexture, primitiveIsGouraud, primitiveColorLookupTableX, primitiveColorLookupTableY);
    }
}


void GPU::finishRectangleSetup() {
    uint8_t red   = currentCommand & 0xFF;
    uint8_t green = (currentCommand >> 8) & 0xFF;
    uint8_t blue  = (currentCommand >> 16) & 0xFF;
    uint16_t flatColor = ((red >> 3) & 0x1F) | (((green >> 3) & 0x1F) << 5) | (((blue >> 3) & 0x1F) << 10);

    rasterizer.drawRectangle(rectangleX, rectangleY, rectangleWidth, rectangleHeight, flatColor, rectangleTextured, rectangleRawTexture, rectangleTextureCoordinateU, rectangleTextureCoordinateV, rectangleColorLookupTableX, rectangleColorLookupTableY);
}


void GPU::updateDMARequestBit() {
    bool bit;
    switch (DMADirection) {
        case 0: bit = false; break;
        case 1: bit = true; break;
        case 2: bit = (gpuStatusRegister >> 28) & 0x01; break;
        case 3: bit = (gpuStatusRegister >> 27) & 0x01; break;
        default: bit = false;
    }
    gpuStatusRegister = bit ? (gpuStatusRegister | 0x02000000) : (gpuStatusRegister & ~0x02000000);
}

