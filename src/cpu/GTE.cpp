#include "cpu/GTE.h"
#include <iostream>
#include <algorithm>
#include <cstdint>

// FLAG register (Control Register 31) bit positions — psx-spx GTE FLAG spec
static constexpr uint32_t FLAG_IR0_SAT      = 1u << 12;
static constexpr uint32_t FLAG_SY2_SAT      = 1u << 13;
static constexpr uint32_t FLAG_SX2_SAT      = 1u << 14;
static constexpr uint32_t FLAG_MAC0_NEG_OVF = 1u << 15;
static constexpr uint32_t FLAG_MAC0_POS_OVF = 1u << 16;
static constexpr uint32_t FLAG_DIVIDE_OVF   = 1u << 17;
static constexpr uint32_t FLAG_SZ3_SAT      = 1u << 18;
static constexpr uint32_t FLAG_COLOR_B_SAT  = 1u << 19;
static constexpr uint32_t FLAG_COLOR_G_SAT  = 1u << 20;
static constexpr uint32_t FLAG_COLOR_R_SAT  = 1u << 21;
static constexpr uint32_t FLAG_IR3_SAT      = 1u << 22;
static constexpr uint32_t FLAG_IR2_SAT      = 1u << 23;
static constexpr uint32_t FLAG_IR1_SAT      = 1u << 24;
static constexpr uint32_t FLAG_MAC3_NEG_OVF = 1u << 25;
static constexpr uint32_t FLAG_MAC2_NEG_OVF = 1u << 26;
static constexpr uint32_t FLAG_MAC1_NEG_OVF = 1u << 27;
static constexpr uint32_t FLAG_MAC3_POS_OVF = 1u << 28;
static constexpr uint32_t FLAG_MAC2_POS_OVF = 1u << 29;
static constexpr uint32_t FLAG_MAC1_POS_OVF = 1u << 30;
static constexpr uint32_t FLAG_ERROR        = 1u << 31;

static constexpr uint32_t FLAG_ERROR_MASK = (0xFFu << 23) | (0x3Fu << 13);

static constexpr int64_t MAC_44BIT_MAX =  ((int64_t)1 << 43) - 1;
static constexpr int64_t MAC_44BIT_MIN = -((int64_t)1 << 43);

GTE::GTE() {
    for (int i = 0; i < 32; i++) {
        dataRegisters[i] = 0;
        controlRegisters[i] = 0;
    }
}


void GTE::writeDataRegister(uint32_t index, uint32_t value) {
    dataRegisters[index] = value;
}


uint32_t GTE::readDataRegister(uint32_t index) const {
    return dataRegisters[index];
}


void GTE::writeControlRegister(uint32_t index, uint32_t value) {
    controlRegisters[index] = value;
}


uint32_t GTE::readControlRegister(uint32_t index) const {
    return controlRegisters[index];
}


int32_t GTE::clampS(int64_t val, int32_t lo, int32_t hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return (int32_t)val;
}


int32_t GTE::clampSFlag(int64_t val, int32_t lo, int32_t hi, uint32_t flagBit, uint32_t& flags) {
    if (val < lo) { flags |= flagBit; return lo; }
    if (val > hi) { flags |= flagBit; return hi; }
    return (int32_t)val;
}


uint32_t GTE::divideProjection(uint16_t h, uint16_t sz3, uint32_t& flags) {
    if (sz3 == 0) {
        flags |= FLAG_DIVIDE_OVF;
        return 0x1FFFF;
    }
    uint64_t n = ((uint64_t)h * 0x20000ull) / sz3;
    uint64_t result = (n + 1) / 2;
    if (result > 0x1FFFF) {
        flags |= FLAG_DIVIDE_OVF;
        return 0x1FFFF;
    }
    return (uint32_t)result;
}


void GTE::executeCommand(uint32_t instruction) {
    uint32_t command = instruction & 0x3F;

    switch (command) {
        case 0x06: { // NCLIP (Normal Clipping - triangle winding/backface test)
            commandNCLIP(instruction);
            break;
        }
        case 0x12: { // MVMVA (Multiply Vector by Matrix and Vector Addition)
            commandMVMVA(instruction);
            break;
        }
        case 0x13: { // NCDS (Normal Color Depth Cue, Single vector)
            commandNCDS(instruction);
            break;
        }
        case 0x2D: { // AVSZ3 (Average of 3 Z values -> OTZ)
            commandAVSZ3(instruction);
            break;
        }
        case 0x30: { // RTPT (Rotate, Translate, Perspective Transform Triple)
            commandRTPT(instruction);
            break;
        }
        default: {
            std::cout << "FATAL: Unimplemented GTE Math Command: 0x" << std::hex << command << "\n";
            exit(1);
        }
    }
}


void GTE::commandNCLIP(uint32_t instruction) {
    uint32_t flags = 0;

    int16_t SX0 = (int16_t)(dataRegisters[12] & 0xFFFF);
    int16_t SY0 = (int16_t)(dataRegisters[12] >> 16);
    int16_t SX1 = (int16_t)(dataRegisters[13] & 0xFFFF);
    int16_t SY1 = (int16_t)(dataRegisters[13] >> 16);
    int16_t SX2 = (int16_t)(dataRegisters[14] & 0xFFFF);
    int16_t SY2 = (int16_t)(dataRegisters[14] >> 16);

    int64_t mac0 = (int64_t)SX0 * SY1 + (int64_t)SX1 * SY2 + (int64_t)SX2 * SY0
                 - (int64_t)SX0 * SY2 - (int64_t)SX1 * SY0 - (int64_t)SX2 * SY1;

    // MAC0 is a 32-bit signed register; flag it if the cross product overflows that range.
    if (mac0 > INT32_MAX) flags |= FLAG_MAC0_POS_OVF;
    if (mac0 < INT32_MIN) flags |= FLAG_MAC0_NEG_OVF;

    dataRegisters[24] = (uint32_t)(int32_t)mac0; // MAC0

    if (flags & FLAG_ERROR_MASK) flags |= FLAG_ERROR;
    controlRegisters[31] = flags; // FLAG register
}


void GTE::commandNCDS(uint32_t instruction) {
    const bool sf = (instruction >> 19) & 1;
    const bool lm = (instruction >> 10) & 1;
    const int shift = sf ? 12 : 0;
    const int32_t irLo = lm ? 0 : -32768;

    uint32_t flags = 0;

    // Light Source Matrix (LLM) - Control Registers 8-12
    int16_t L11 = (int16_t)(controlRegisters[8] & 0xFFFF),  L12 = (int16_t)(controlRegisters[8] >> 16);
    int16_t L13 = (int16_t)(controlRegisters[9] & 0xFFFF),  L21 = (int16_t)(controlRegisters[9] >> 16);
    int16_t L22 = (int16_t)(controlRegisters[10] & 0xFFFF), L23 = (int16_t)(controlRegisters[10] >> 16);
    int16_t L31 = (int16_t)(controlRegisters[11] & 0xFFFF), L32 = (int16_t)(controlRegisters[11] >> 16);
    int16_t L33 = (int16_t)(controlRegisters[12] & 0xFFFF);

    // Background color vector (BK) - Control Registers 13-15
    int32_t RBK = (int32_t)controlRegisters[13];
    int32_t GBK = (int32_t)controlRegisters[14];
    int32_t BBK = (int32_t)controlRegisters[15];

    // Light Color Matrix (LCM) - Control Registers 16-20
    int16_t C11 = (int16_t)(controlRegisters[16] & 0xFFFF), C12 = (int16_t)(controlRegisters[16] >> 16);
    int16_t C13 = (int16_t)(controlRegisters[17] & 0xFFFF), C21 = (int16_t)(controlRegisters[17] >> 16);
    int16_t C22 = (int16_t)(controlRegisters[18] & 0xFFFF), C23 = (int16_t)(controlRegisters[18] >> 16);
    int16_t C31 = (int16_t)(controlRegisters[19] & 0xFFFF), C32 = (int16_t)(controlRegisters[19] >> 16);
    int16_t C33 = (int16_t)(controlRegisters[20] & 0xFFFF);

    // Far color vector (FC) - Control Registers 21-23
    int32_t RFC = (int32_t)controlRegisters[21];
    int32_t GFC = (int32_t)controlRegisters[22];
    int32_t BFC = (int32_t)controlRegisters[23];

    // Vertex V0 - Data Registers 0-1
    int16_t vx = (int16_t)(dataRegisters[0] & 0xFFFF);
    int16_t vy = (int16_t)(dataRegisters[0] >> 16);
    int16_t vz = (int16_t)(dataRegisters[1] & 0xFFFF);

    // RGBC (input primary color) - Data Register 6
    uint32_t rgbc = dataRegisters[6];
    uint32_t R = rgbc & 0xFF, G = (rgbc >> 8) & 0xFF, B = (rgbc >> 16) & 0xFF, CODE = (rgbc >> 24) & 0xFF;

    int32_t IR0 = (int32_t)dataRegisters[8];

    auto checkMacOverflow = [&](int64_t m1, int64_t m2, int64_t m3) {
        if (m1 > MAC_44BIT_MAX) flags |= FLAG_MAC1_POS_OVF;
        if (m1 < MAC_44BIT_MIN) flags |= FLAG_MAC1_NEG_OVF;
        if (m2 > MAC_44BIT_MAX) flags |= FLAG_MAC2_POS_OVF;
        if (m2 < MAC_44BIT_MIN) flags |= FLAG_MAC2_NEG_OVF;
        if (m3 > MAC_44BIT_MAX) flags |= FLAG_MAC3_POS_OVF;
        if (m3 < MAC_44BIT_MIN) flags |= FLAG_MAC3_NEG_OVF;
    };

    // Step 1: LLM * V0
    int64_t mac1 = (int64_t)L11 * vx + (int64_t)L12 * vy + (int64_t)L13 * vz;
    int64_t mac2 = (int64_t)L21 * vx + (int64_t)L22 * vy + (int64_t)L23 * vz;
    int64_t mac3 = (int64_t)L31 * vx + (int64_t)L32 * vy + (int64_t)L33 * vz;
    checkMacOverflow(mac1, mac2, mac3);

    int32_t MAC1 = (int32_t)(mac1 >> shift), MAC2 = (int32_t)(mac2 >> shift), MAC3 = (int32_t)(mac3 >> shift);
    int32_t IR1 = clampSFlag(MAC1, irLo, 32767, FLAG_IR1_SAT, flags);
    int32_t IR2 = clampSFlag(MAC2, irLo, 32767, FLAG_IR2_SAT, flags);
    int32_t IR3 = clampSFlag(MAC3, irLo, 32767, FLAG_IR3_SAT, flags);

    // Step 2: BK*4096 + LCM * IR
    int64_t mac1b = (int64_t)RBK * 4096 + (int64_t)C11 * IR1 + (int64_t)C12 * IR2 + (int64_t)C13 * IR3;
    int64_t mac2b = (int64_t)GBK * 4096 + (int64_t)C21 * IR1 + (int64_t)C22 * IR2 + (int64_t)C23 * IR3;
    int64_t mac3b = (int64_t)BBK * 4096 + (int64_t)C31 * IR1 + (int64_t)C32 * IR2 + (int64_t)C33 * IR3;
    checkMacOverflow(mac1b, mac2b, mac3b);

    MAC1 = (int32_t)(mac1b >> shift); MAC2 = (int32_t)(mac2b >> shift); MAC3 = (int32_t)(mac3b >> shift);
    IR1 = clampSFlag(MAC1, irLo, 32767, FLAG_IR1_SAT, flags);
    IR2 = clampSFlag(MAC2, irLo, 32767, FLAG_IR2_SAT, flags);
    IR3 = clampSFlag(MAC3, irLo, 32767, FLAG_IR3_SAT, flags);

    // Step 3: [R*IR1, G*IR2, B*IR3] << 4  (uses the CLAMPED IR values)
    int64_t colorMac1 = ((int64_t)R * IR1) << 4;
    int64_t colorMac2 = ((int64_t)G * IR2) << 4;
    int64_t colorMac3 = ((int64_t)B * IR3) << 4;

    // Step 4: depth cue, done in two stages the way the hardware does it:
	// IR = ((FC << 12) - MAC) >> shift (saturated, ignoring lm), then MAC = IR * IR0 + MAC
	int32_t depthCueIR1 = clampSFlag((((int64_t)RFC << 12) - colorMac1) >> shift, -32768, 32767, FLAG_IR1_SAT, flags);
	int32_t depthCueIR2 = clampSFlag((((int64_t)GFC << 12) - colorMac2) >> shift, -32768, 32767, FLAG_IR2_SAT, flags);
	int32_t depthCueIR3 = clampSFlag((((int64_t)BFC << 12) - colorMac3) >> shift, -32768, 32767, FLAG_IR3_SAT, flags);

	int64_t depthMac1 = (int64_t)depthCueIR1 * IR0 + colorMac1;
	int64_t depthMac2 = (int64_t)depthCueIR2 * IR0 + colorMac2;
	int64_t depthMac3 = (int64_t)depthCueIR3 * IR0 + colorMac3;
	checkMacOverflow(depthMac1, depthMac2, depthMac3);

    // Step 5: SAR shift
    MAC1 = (int32_t)(depthMac1 >> shift); MAC2 = (int32_t)(depthMac2 >> shift); MAC3 = (int32_t)(depthMac3 >> shift);
    IR1 = clampSFlag(MAC1, irLo, 32767, FLAG_IR1_SAT, flags);
    IR2 = clampSFlag(MAC2, irLo, 32767, FLAG_IR2_SAT, flags);
    IR3 = clampSFlag(MAC3, irLo, 32767, FLAG_IR3_SAT, flags);

    dataRegisters[9]  = (uint32_t)IR1;
    dataRegisters[10] = (uint32_t)IR2;
    dataRegisters[11] = (uint32_t)IR3;
    dataRegisters[25] = (uint32_t)MAC1;
    dataRegisters[26] = (uint32_t)MAC2;
    dataRegisters[27] = (uint32_t)MAC3;

    // Color FIFO push: [MAC1/16, MAC2/16, MAC3/16, CODE], saturated to 0..255
    uint32_t outR = (uint32_t)clampSFlag(MAC1 >> 4, 0, 255, FLAG_COLOR_R_SAT, flags);
    uint32_t outG = (uint32_t)clampSFlag(MAC2 >> 4, 0, 255, FLAG_COLOR_G_SAT, flags);
    uint32_t outB = (uint32_t)clampSFlag(MAC3 >> 4, 0, 255, FLAG_COLOR_B_SAT, flags);

    dataRegisters[20] = dataRegisters[21];
    dataRegisters[21] = dataRegisters[22];
    dataRegisters[22] = (CODE << 24) | (outB << 16) | (outG << 8) | outR;

    if (flags & FLAG_ERROR_MASK) flags |= FLAG_ERROR;
    controlRegisters[31] = flags;
}


void GTE::commandAVSZ3(uint32_t instruction) {
    uint32_t flags = 0;

    int16_t ZSF3 = (int16_t)(controlRegisters[29] & 0xFFFF);

    uint16_t SZ1 = (uint16_t)(dataRegisters[17] & 0xFFFF);
    uint16_t SZ2 = (uint16_t)(dataRegisters[18] & 0xFFFF);
    uint16_t SZ3 = (uint16_t)(dataRegisters[19] & 0xFFFF);

    int64_t mac0 = (int64_t)ZSF3 * ((int64_t)SZ1 + SZ2 + SZ3);

    if (mac0 > INT32_MAX) flags |= FLAG_MAC0_POS_OVF;
    if (mac0 < INT32_MIN) flags |= FLAG_MAC0_NEG_OVF;

    dataRegisters[24] = (uint32_t)(int32_t)mac0; // MAC0

    int32_t otz = clampSFlag(mac0 >> 12, 0, 0xFFFF, FLAG_SZ3_SAT, flags);
    dataRegisters[7] = (uint32_t)otz; // OTZ

    if (flags & FLAG_ERROR_MASK) flags |= FLAG_ERROR;
    controlRegisters[31] = flags; // FLAG register
}


void GTE::commandRTPT(uint32_t instruction) {
    const bool sf = (instruction >> 19) & 1;
    const bool lm = (instruction >> 10) & 1;
    const int shift = sf ? 12 : 0;

    uint32_t flags = 0; // accumulates across all 3 vertices, committed once at the end

    // 1. Rotation matrix (Control Registers 0-4)
    int16_t RT11 = (int16_t)(controlRegisters[0] & 0xFFFF);
    int16_t RT12 = (int16_t)(controlRegisters[0] >> 16);
    int16_t RT13 = (int16_t)(controlRegisters[1] & 0xFFFF);
    int16_t RT21 = (int16_t)(controlRegisters[1] >> 16);
    int16_t RT22 = (int16_t)(controlRegisters[2] & 0xFFFF);
    int16_t RT23 = (int16_t)(controlRegisters[2] >> 16);
    int16_t RT31 = (int16_t)(controlRegisters[3] & 0xFFFF);
    int16_t RT32 = (int16_t)(controlRegisters[3] >> 16);
    int16_t RT33 = (int16_t)(controlRegisters[4] & 0xFFFF);

    // 2. Translation vector (Control Registers 5-7)
    int32_t TRX = (int32_t)controlRegisters[5];
    int32_t TRY = (int32_t)controlRegisters[6];
    int32_t TRZ = (int32_t)controlRegisters[7];

    // Perspective projection parameters
    int32_t OFX = (int32_t)controlRegisters[24];
    int32_t OFY = (int32_t)controlRegisters[25];
    uint16_t H  = (uint16_t)(controlRegisters[26] & 0xFFFF);
    int16_t DQA = (int16_t)(controlRegisters[27] & 0xFFFF);
    int32_t DQB = (int32_t)controlRegisters[28];

    // 3. The 3 input vertices (Data Registers 0-5)
    int16_t vx[3], vy[3], vz[3];
    vx[0] = (int16_t)(dataRegisters[0] & 0xFFFF);
    vy[0] = (int16_t)(dataRegisters[0] >> 16);
    vz[0] = (int16_t)(dataRegisters[1] & 0xFFFF);
    vx[1] = (int16_t)(dataRegisters[2] & 0xFFFF);
    vy[1] = (int16_t)(dataRegisters[2] >> 16);
    vz[1] = (int16_t)(dataRegisters[3] & 0xFFFF);
    vx[2] = (int16_t)(dataRegisters[4] & 0xFFFF);
    vy[2] = (int16_t)(dataRegisters[4] >> 16);
    vz[2] = (int16_t)(dataRegisters[5] & 0xFFFF);

    int32_t mac0Final = 0;

    for (int i = 0; i < 3; i++) {
        // 4. Multiply vertex by rotation matrix, 5. add translation
        int64_t mac1 = (int64_t)TRX * 4096 + (int64_t)RT11 * vx[i] + (int64_t)RT12 * vy[i] + (int64_t)RT13 * vz[i];
        int64_t mac2 = (int64_t)TRY * 4096 + (int64_t)RT21 * vx[i] + (int64_t)RT22 * vy[i] + (int64_t)RT23 * vz[i];
        int64_t mac3 = (int64_t)TRZ * 4096 + (int64_t)RT31 * vx[i] + (int64_t)RT32 * vy[i] + (int64_t)RT33 * vz[i];

        int32_t MAC1 = (int32_t)(mac1 >> shift);
        int32_t MAC2 = (int32_t)(mac2 >> shift);
        int32_t MAC3 = (int32_t)(mac3 >> shift);

        int32_t IR1 = clampSFlag(MAC1, lm ? 0 : -32768, 32767, FLAG_IR1_SAT, flags);
        int32_t IR2 = clampSFlag(MAC2, lm ? 0 : -32768, 32767, FLAG_IR2_SAT, flags);
        int32_t IR3 = clampSFlag(MAC3, lm ? 0 : -32768, 32767, FLAG_IR3_SAT, flags);

        // Z always uses a fixed 12-bit fractional shift regardless of sf
        uint32_t SZ3 = (uint32_t)clampSFlag((int32_t)(mac3 >> 12), 0, 0xFFFF, FLAG_SZ3_SAT, flags);

        // Push into the SZ FIFO (registers 16-19)
        dataRegisters[16] = dataRegisters[17];
        dataRegisters[17] = dataRegisters[18];
        dataRegisters[18] = dataRegisters[19];
        dataRegisters[19] = SZ3;

        // 6. Perspective projection: divide by depth via shared helper (correct rounding + FLAG_DIVIDE_OVF)
        uint32_t n = divideProjection(H, (uint16_t)SZ3, flags);

        int64_t mac0X = (int64_t)n * IR1 + OFX;
        int64_t mac0Y = (int64_t)n * IR2 + OFY;
        int64_t mac0Z = (int64_t)n * DQA + DQB;

        int32_t SX  = clampSFlag(mac0X >> 16, -0x400, 0x3FF, FLAG_SX2_SAT, flags);
        int32_t SY  = clampSFlag(mac0Y >> 16, -0x400, 0x3FF, FLAG_SY2_SAT, flags);
        int32_t IR0 = clampSFlag(mac0Z >> 12, 0, 0x1000, FLAG_IR0_SAT, flags);

        // 7. Push into the SXY FIFO (registers 12-14, mirrored to 15)
        dataRegisters[12] = dataRegisters[13];
        dataRegisters[13] = dataRegisters[14];
        dataRegisters[14] = ((uint32_t)(uint16_t)SY << 16) | (uint16_t)(uint32_t)SX;
        dataRegisters[15] = dataRegisters[14]; // SXYP mirrors SXY2

        dataRegisters[8]  = (uint32_t)IR0;
        dataRegisters[9]  = (uint32_t)IR1;
        dataRegisters[10] = (uint32_t)IR2;
        dataRegisters[11] = (uint32_t)IR3;
        dataRegisters[25] = (uint32_t)MAC1;
        dataRegisters[26] = (uint32_t)MAC2;
        dataRegisters[27] = (uint32_t)MAC3;

        mac0Final = (int32_t)mac0Z;
    }

    dataRegisters[24] = (uint32_t)mac0Final;

    if (flags & FLAG_ERROR_MASK) flags |= FLAG_ERROR;
    controlRegisters[31] = flags; // FLAG register
}


void GTE::commandMVMVA(uint32_t instruction) {
    uint32_t flags = 0;

    // Extract instruction configuration
    const bool sf = (instruction >> 19) & 1;
    const uint32_t mx = (instruction >> 17) & 3;
    const uint32_t v  = (instruction >> 15) & 3;
    const uint32_t cv = (instruction >> 13) & 3;
    const bool lm = (instruction >> 10) & 1;
    
    const int shift = sf ? 12 : 0;
    const int32_t irLo = lm ? 0 : -32768;

    // --- 1. Matrix Selection (mx) ---
    int16_t m11, m12, m13, m21, m22, m23, m31, m32, m33;
    if (mx == 0) { // RT (Rotation Matrix)
        m11 = (int16_t)(controlRegisters[0] & 0xFFFF); m12 = (int16_t)(controlRegisters[0] >> 16);
        m13 = (int16_t)(controlRegisters[1] & 0xFFFF); m21 = (int16_t)(controlRegisters[1] >> 16);
        m22 = (int16_t)(controlRegisters[2] & 0xFFFF); m23 = (int16_t)(controlRegisters[2] >> 16);
        m31 = (int16_t)(controlRegisters[3] & 0xFFFF); m32 = (int16_t)(controlRegisters[3] >> 16);
        m33 = (int16_t)(controlRegisters[4] & 0xFFFF);
    } 
    else if (mx == 1) { // LLM (Light Source Matrix)
        m11 = (int16_t)(controlRegisters[8] & 0xFFFF); m12 = (int16_t)(controlRegisters[8] >> 16);
        m13 = (int16_t)(controlRegisters[9] & 0xFFFF); m21 = (int16_t)(controlRegisters[9] >> 16);
        m22 = (int16_t)(controlRegisters[10] & 0xFFFF); m23 = (int16_t)(controlRegisters[10] >> 16);
        m31 = (int16_t)(controlRegisters[11] & 0xFFFF); m32 = (int16_t)(controlRegisters[11] >> 16);
        m33 = (int16_t)(controlRegisters[12] & 0xFFFF);
    } 
    else { // 2 = LCM (Light Color Matrix), 3 is undefined/reserved but hardware defaults to LCM
        m11 = (int16_t)(controlRegisters[16] & 0xFFFF); m12 = (int16_t)(controlRegisters[16] >> 16);
        m13 = (int16_t)(controlRegisters[17] & 0xFFFF); m21 = (int16_t)(controlRegisters[17] >> 16);
        m22 = (int16_t)(controlRegisters[18] & 0xFFFF); m23 = (int16_t)(controlRegisters[18] >> 16);
        m31 = (int16_t)(controlRegisters[19] & 0xFFFF); m32 = (int16_t)(controlRegisters[19] >> 16);
        m33 = (int16_t)(controlRegisters[20] & 0xFFFF);
    }

    // --- 2. Input Vector Selection (v) ---
    int32_t vx, vy, vz;
    if (v == 0) { // V0
        vx = (int16_t)(dataRegisters[0] & 0xFFFF);
        vy = (int16_t)(dataRegisters[0] >> 16);
        vz = (int16_t)(dataRegisters[1] & 0xFFFF);
    } 
    else if (v == 1) { // V1
        vx = (int16_t)(dataRegisters[2] & 0xFFFF);
        vy = (int16_t)(dataRegisters[2] >> 16);
        vz = (int16_t)(dataRegisters[3] & 0xFFFF);
    } 
    else if (v == 2) { // V2
        vx = (int16_t)(dataRegisters[4] & 0xFFFF);
        vy = (int16_t)(dataRegisters[4] >> 16);
        vz = (int16_t)(dataRegisters[5] & 0xFFFF);
    } 
    else { // 3 = IR (Intermediate Result registers)
        vx = (int16_t)(dataRegisters[9] & 0xFFFF);  // IR1
        vy = (int16_t)(dataRegisters[10] & 0xFFFF); // IR2
        vz = (int16_t)(dataRegisters[11] & 0xFFFF); // IR3
    }

    // --- 3. Translation Control Vector Selection (cv) ---
    int64_t tx = 0, ty = 0, tz = 0;
    if (cv == 0) { // TR
        tx = (int32_t)controlRegisters[5];
        ty = (int32_t)controlRegisters[6];
        tz = (int32_t)controlRegisters[7];
    } 
    else if (cv == 1) { // BK
        tx = (int32_t)controlRegisters[13];
        ty = (int32_t)controlRegisters[14];
        tz = (int32_t)controlRegisters[15];
    } 
    else if (cv == 2) { // FC
        tx = (int32_t)controlRegisters[21];
        ty = (int32_t)controlRegisters[22];
        tz = (int32_t)controlRegisters[23];
    } 
    // cv == 3 means "None", translation remains 0

    // Hardware pushes the translation registers straight into the high bits 
    // of the 44-bit accumulator prior to multiplication
    tx <<= 12; 
    ty <<= 12; 
    tz <<= 12;

    // --- 4. Matrix Multiplication & Accumulation ---
    int64_t mac1 = tx + ((int64_t)m11 * vx) + ((int64_t)m12 * vy) + ((int64_t)m13 * vz);
    int64_t mac2 = ty + ((int64_t)m21 * vx) + ((int64_t)m22 * vy) + ((int64_t)m23 * vz);
    int64_t mac3 = tz + ((int64_t)m31 * vx) + ((int64_t)m32 * vy) + ((int64_t)m33 * vz);

    // Hardware 44-bit overflow assertions
    if (mac1 > MAC_44BIT_MAX) flags |= FLAG_MAC1_POS_OVF;
    if (mac1 < MAC_44BIT_MIN) flags |= FLAG_MAC1_NEG_OVF;
    if (mac2 > MAC_44BIT_MAX) flags |= FLAG_MAC2_POS_OVF;
    if (mac2 < MAC_44BIT_MIN) flags |= FLAG_MAC2_NEG_OVF;
    if (mac3 > MAC_44BIT_MAX) flags |= FLAG_MAC3_POS_OVF;
    if (mac3 < MAC_44BIT_MIN) flags |= FLAG_MAC3_NEG_OVF;

    // --- 5. Downshift into 32-bit Multiplier Accumulators ---
    int32_t MAC1 = (int32_t)(mac1 >> shift);
    int32_t MAC2 = (int32_t)(mac2 >> shift);
    int32_t MAC3 = (int32_t)(mac3 >> shift);

    // --- 6. Saturate into 16-bit Intermediate Results ---
    int32_t IR1 = clampSFlag(MAC1, irLo, 32767, FLAG_IR1_SAT, flags);
    int32_t IR2 = clampSFlag(MAC2, irLo, 32767, FLAG_IR2_SAT, flags);
    int32_t IR3 = clampSFlag(MAC3, irLo, 32767, FLAG_IR3_SAT, flags);

    // Commit hardware registers
    dataRegisters[9]  = (uint32_t)IR1;
    dataRegisters[10] = (uint32_t)IR2;
    dataRegisters[11] = (uint32_t)IR3;
    dataRegisters[25] = (uint32_t)MAC1;
    dataRegisters[26] = (uint32_t)MAC2;
    dataRegisters[27] = (uint32_t)MAC3;

    if (flags & FLAG_ERROR_MASK) flags |= FLAG_ERROR;
    controlRegisters[31] = flags;
}



