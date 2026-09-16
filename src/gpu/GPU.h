#pragma once
#include <cstdint>
#include <vector>
#include <iostream>
#include "../Constants.h"
#include "VRAM.h"
#include "Rasterizer.h"

using namespace std;

class GPU {
public:
    GPU();
    
    uint32_t readGP0();
    uint32_t readGP1();
    void writeGP0(uint32_t value);
    void writeGP1(uint32_t value);

    // Delegated to VRAM member
    const uint16_t* getVRAMRawPointer() const { return vram.getRawData(); }
    uint16_t getDisplayAreaX() const { return vram.getDisplayAreaX(); }
    uint16_t getDisplayAreaY() const { return vram.getDisplayAreaY(); }
    uint16_t getDisplayWidth() const { return vram.getDisplayWidth(); }
    uint16_t getDisplayHeight() const { return vram.getDisplayHeight(); }

private:
    // =========================================================
    // CORE HARDWARE STATE
    // =========================================================
    VRAM vram;
    Rasterizer rasterizer;
    uint32_t gpuStatusRegister = Hardware::GPU_DEFAULT_STATUS;
    uint8_t  DMADirection = 0;


    // =========================================================
    // GP0: COMMAND PARSER & FIFO
    // =========================================================
    uint32_t currentCommand = 0;
    int wordsRemaining = 0;
    int parametersRemaining = 0;

    // =========================================================
    // GP0: PRIMITIVE BUILDER STATE
    // =========================================================
    // CPU to VRAM (0xA0) / VRAM to CPU (0xC0)
    uint16_t transferX = 0, transferY = 0;
    uint16_t transferWidth = 0, transferHeight = 0;
    int transferXCursor = 0, transferYCursor = 0;
    
    uint16_t readTransferX = 0, readTransferY = 0;
    uint16_t readTransferWidth = 0, readTransferHeight = 0;
    int readTransferXCursor = 0, readTransferYCursor = 0;
    int vramReadWordsRemaining = 0;

    // Rectangles (0x60-0x7F)
    bool rectangleTextured = false;
    bool rectangleSemiTransparent = false;
    bool rectangleRawTexture = false;
    uint8_t rectangleSizeMode = 0; 
    int rectangleParametersExpected = 0;
    int16_t rectangleX = 0, rectangleY = 0;
    uint16_t rectangleWidth = 0, rectangleHeight = 0;
    uint8_t rectangleTextureCoordinateU = 0, rectangleTextureCoordinateV = 0;
    uint16_t rectangleColorLookupTableX = 0, rectangleColorLookupTableY = 0;

    // --- Unified Polygon State ---
    Vertex primitiveVertices[4];
    int primitiveVertexCount = 0;
    int primitiveCurrentVertexIndex = 0;

    // Shared Primitive Attributes
    bool primitiveIsTextured = false;
    bool primitiveIsSemiTransparent = false;
    bool primitiveIsRawTexture = false;
    bool primitiveIsGouraud = false;

    // Shared Color Lookup Table (CLUT) 
    uint16_t primitiveColorLookupTableX = 0;
    uint16_t primitiveColorLookupTableY = 0;

    // Specifically for Gouraud parsing order
    enum class GouraudPolygonWordRole { Color, Position, Texture };
    vector<GouraudPolygonWordRole> gouraudPolygonWordSequence;
    int gouraudPolygonWordCursor = 0;


    // =========================================================
    // INTERNAL HANDLERS & DISPATCHERS
    // =========================================================
    void updateDMARequestBit();
    void handleFillRectangleWord(uint32_t value);
    void handleMonotoneQuadWord(uint32_t value);
    void handleCopyCPUToVRAMWord(uint32_t value);
    void handleCopyVRAMToCPUParameters(uint32_t value);
    void handleRectangleWord(uint32_t value);
    void handleTexturedPolygonWord(uint32_t value);
    void handleGouraudPolygonWord(uint32_t value);
    void finishRectangleSetup();
};