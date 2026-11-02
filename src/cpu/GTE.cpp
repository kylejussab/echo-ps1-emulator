#include <algorithm>
#include <cstdint>
#include <iostream>
#include "cpu/GTE.h"


GTE::GTE() {
    for (int i = 0; i < 32; i++) {
        dataRegisters[i] = 0;
        controlRegisters[i] = 0;
    }
    buildReciprocalSeedTable();
}


uint32_t GTE::readDataRegister(uint32_t index) const {
    switch (index) {
        case 1: case 3: case 5: case 8: case 9: case 10: case 11: {
            return (uint32_t)(int32_t)(int16_t)dataRegisters[index];
        }
        case 7: case 16: case 17: case 18: case 19: {
            return dataRegisters[index] & 0xFFFF;
        }
        case 15: {
            return dataRegisters[14];
        }
        case 28: case 29: {
            auto c = [](int16_t v) { return (uint32_t)std::clamp((int)v >> 7, 0, 0x1F); };
            return c((int16_t)dataRegisters[9]) | (c((int16_t)dataRegisters[10]) << 5)
                | (c((int16_t)dataRegisters[11]) << 10);
        }
        default: {
            return dataRegisters[index];
        }
    }
}


void GTE::writeDataRegister(uint32_t index, uint32_t value) {
    switch (index) {
        case 15: {
            dataRegisters[12] = dataRegisters[13];
            dataRegisters[13] = dataRegisters[14];
            dataRegisters[14] = value;
            dataRegisters[15] = value;
            break;
        }
        case 28: {
            dataRegisters[9]  = (value & 0x1F) * 0x80;
            dataRegisters[10] = ((value >> 5) & 0x1F) * 0x80;
            dataRegisters[11] = ((value >> 10) & 0x1F) * 0x80;
            break;
        } 
        case 29: case 31: break; // read-only
        case 30: {
            dataRegisters[30] = value;
            // LZCR counts leading bits equal to the sign bit, so flip negative values first
            uint32_t bitsToCount = ((int32_t)value < 0) ? ~value : value;
            dataRegisters[31] = countLeadingZeros(bitsToCount);
            break;
        }
        default: dataRegisters[index] = value;
    }
}


uint32_t GTE::readControlRegister(uint32_t index) const {
    switch (index) {
        case 4: case 12: case 20: case 26: case 27: case 29: case 30: {
            return (uint32_t)(int32_t)(int16_t)controlRegisters[index];
        }
        default: {
            return controlRegisters[index];
        }
    }
}


void GTE::writeControlRegister(uint32_t index, uint32_t value) {
    if (index == 31) { 
        value &= 0x7FFFF000;

        if (value & 0x7F87E000) {
            value |= 0x80000000;
        }
    }
    controlRegisters[index] = value;
}


void GTE::executeCommand(uint32_t instruction) {
    uint32_t command = instruction & 0x3F;

    switch (command) {
        case 0x00: { // Not a real GTE command. ps1-tests cpu/cop executes it and expects no exception.
            break;
        }
        case 0x01: { // RTPS (Rotate, Translate, Perspective Transform Single)
            commandRTPS(instruction);
            break;
        }
        case 0x06: { // NCLIP (Normal Clipping - triangle winding/backface test)
            commandNCLIP(instruction);
            break;
        }
        case 0x0C: { // OP (Outer Product of rotation matrix diagonal and IR vector)
			commandOP(instruction);
			break;
		}
        case 0x10: { // DPCS (Depth Cueing Single)
            commandDPCS(instruction);
            break;
        }
        case 0x11: { // INTPL (Interpolation of IR toward the far color)
            commandINTPL(instruction); 
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
        case 0x14: { // CDP (Color Depth Cue)
            commandCDP(instruction); 
            break; 
        }
        case 0x16: { // NCDT (Normal Color Depth cue, Triple)
            commandNCDT(instruction); 
            break; 
        }
        case 0x1B: { // NCCS (Normal Color Color, Single)
            commandNCCS(instruction); 
            break; 
        }
        case 0x1C: { // CC (Color Color)
            commandCC(instruction); 
            break; 
        }
        case 0x1E: { // NCS (Normal Color, Single)
            commandNCS(instruction); 
            break; 
        }
        case 0x20: { // NCT (Normal Color, Triple)
            commandNCT(instruction); 
            break; 
        }
        case 0x2A: { // DPCT (Depth Cueing Triple)
            commandDPCT(instruction);
            break;
        }
        case 0x2D: { // AVSZ3 (Average of 3 Z values -> OTZ)
            commandAVSZ3(instruction);
            break;
        }
        case 0x2E: { // AVSZ4 (Average of 4 Z values -> OTZ)
            commandAVSZ4(instruction);
            break;
        }
        case 0x28: { // SQR (Square of IR vector)
            commandSQR(instruction);
            break;
        }
        case 0x29: { // DCPL (Depth Cue Color Light)
            commandDCPL(instruction); 
            break; 
        }
        case 0x30: { // RTPT (Rotate, Translate, Perspective Transform Triple)
            commandRTPT(instruction);
            break;
        }
        case 0x3D: { // GPF (General Purpose Interpolation)
            commandGPF(instruction);
            break;
        }
        case 0x3E: { // GPL (General Purpose Interpolation with base)
            commandGPL(instruction);
            break;
        }
        case 0x3F: { // NCCT (Normal Color Color, Triple)
            commandNCCT(instruction); 
            break; 
        }
        default: {
            std::cout << "FATAL: Unimplemented GTE Math Command: 0x" << std::hex << command << "\n";
            exit(1);
        }
    }
}



//Privates
void GTE::buildReciprocalSeedTable() {
    for (uint32_t tableIndex = 0; tableIndex < RECIPROCAL_SEED_TABLE_SIZE; tableIndex++) {
        int32_t seed = (0x40000 / (int32_t)(tableIndex + 0x100) + 1) / 2 - 0x101;
        reciprocalSeedTable[tableIndex] = (seed < 0) ? 0 : (uint32_t)seed;
    }
}


void GTE::commandRTPS(uint32_t instruction) {
    uint32_t flags = 0;
    stepPerspectiveTransform(instruction, 0, true, flags);
    commitFlags(flags);
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


void GTE::commandOP(uint32_t instruction) {
	const bool shiftFraction = (instruction >> 19) & 1;
	const bool limitNegative = (instruction >> 10) & 1;
	const int shift = shiftFraction ? 12 : 0;
	const int32_t intermediateLowerBound = limitNegative ? 0 : -32768;

	uint32_t flags = 0;

	// The "D" vector is the diagonal of the rotation matrix: RT11, RT22, RT33
	int16_t rotationDiagonal1 = (int16_t)(controlRegisters[0] & 0xFFFF);
	int16_t rotationDiagonal2 = (int16_t)(controlRegisters[2] & 0xFFFF);
	int16_t rotationDiagonal3 = (int16_t)(controlRegisters[4] & 0xFFFF);

	// Current IR1-IR3 (Data Registers 9-11)
	int16_t intermediate1 = (int16_t)(dataRegisters[9] & 0xFFFF);
	int16_t intermediate2 = (int16_t)(dataRegisters[10] & 0xFFFF);
	int16_t intermediate3 = (int16_t)(dataRegisters[11] & 0xFFFF);

	// Cross product: IR x D
	int64_t accumulator1 = (int64_t)intermediate3 * rotationDiagonal2 - (int64_t)intermediate2 * rotationDiagonal3;
	int64_t accumulator2 = (int64_t)intermediate1 * rotationDiagonal3 - (int64_t)intermediate3 * rotationDiagonal1;
	int64_t accumulator3 = (int64_t)intermediate2 * rotationDiagonal1 - (int64_t)intermediate1 * rotationDiagonal2;

	int32_t multiplyAccumulator1 = (int32_t)(accumulator1 >> shift);
	int32_t multiplyAccumulator2 = (int32_t)(accumulator2 >> shift);
	int32_t multiplyAccumulator3 = (int32_t)(accumulator3 >> shift);

	int32_t newIntermediate1 = clampSFlag(multiplyAccumulator1, intermediateLowerBound, 32767, FLAG_IR1_SAT, flags);
	int32_t newIntermediate2 = clampSFlag(multiplyAccumulator2, intermediateLowerBound, 32767, FLAG_IR2_SAT, flags);
	int32_t newIntermediate3 = clampSFlag(multiplyAccumulator3, intermediateLowerBound, 32767, FLAG_IR3_SAT, flags);

	dataRegisters[9]  = (uint32_t)newIntermediate1;
	dataRegisters[10] = (uint32_t)newIntermediate2;
	dataRegisters[11] = (uint32_t)newIntermediate3;
	dataRegisters[25] = (uint32_t)multiplyAccumulator1;
	dataRegisters[26] = (uint32_t)multiplyAccumulator2;
	dataRegisters[27] = (uint32_t)multiplyAccumulator3;

	if (flags & FLAG_ERROR_MASK) flags |= FLAG_ERROR;
	controlRegisters[31] = flags;
}


void GTE::commandDPCS(uint32_t instruction) {
    const int shift = shiftFromInstruction(instruction);
    uint32_t flags = 0;

    // Start from the primary color scaled to MAC format (color << 16), then blend toward the far color
    const uint32_t rgbc = dataRegisters[6];

    int64_t accumulator[3] = {
        (int64_t)(rgbc & 0xFF) << 16,
        (int64_t)((rgbc >> 8) & 0xFF) << 16,
        (int64_t)((rgbc >> 16) & 0xFF) << 16
    };

    applyDepthCue(accumulator, shift, flags);
    storeAccumulators(accumulator, shift, irLowerBoundFromInstruction(instruction), flags);
    pushColorFifo(flags);
    commitFlags(flags);
}


void GTE::commandINTPL(uint32_t instruction) {
    uint32_t flags = 0;
    stepInterpolateIR(instruction, flags);
    pushColorFifo(flags);
    commitFlags(flags);
}


void GTE::commandMVMVA(uint32_t instruction) {
    uint32_t flags = 0;

    const uint32_t mx = (instruction >> 17) & 3;
    const uint32_t v  = (instruction >> 15) & 3;
    const uint32_t cv = (instruction >> 13) & 3;
    const int shift = shiftFromInstruction(instruction);

    // 1. Matrix selection (mx)
    int32_t m[3][3];
    switch (mx) {
        case 0: readMatrix(controlRegisters, 0,  m); break; // RT  (rotation)
        case 1: readMatrix(controlRegisters, 8,  m); break; // LLM (light)
        case 2: readMatrix(controlRegisters, 16, m); break; // LCM (light color)
        default: {
            // mx == 3 is not a real matrix. The hardware reads a "garbage" matrix assembled from
            // unrelated registers: R (from RGBC) << 4, IR0, RT13 and RT22.
            const int32_t red  = (int32_t)(dataRegisters[6] & 0xFF) << 4;
            const int32_t ir0  = (int16_t)(dataRegisters[8] & 0xFFFF);
            const int32_t rt13 = (int16_t)(controlRegisters[1] & 0xFFFF);
            const int32_t rt22 = (int16_t)(controlRegisters[2] & 0xFFFF);
            m[0][0] = -red; m[0][1] = red;  m[0][2] = ir0;
            m[1][0] = rt13; m[1][1] = rt13; m[1][2] = rt13;
            m[2][0] = rt22; m[2][1] = rt22; m[2][2] = rt22;
            break;
        }
    }

    // Input vector selection (v)
    int32_t vec[3];
    if (v == 3) { // IR1..IR3
        vec[0] = (int16_t)(dataRegisters[9]  & 0xFFFF);
        vec[1] = (int16_t)(dataRegisters[10] & 0xFFFF);
        vec[2] = (int16_t)(dataRegisters[11] & 0xFFFF);
    } 
    else { // V0..V2 live in data registers (2v, 2v+1)
        vec[0] = (int16_t)(dataRegisters[v * 2] & 0xFFFF);
        vec[1] = (int16_t)(dataRegisters[v * 2] >> 16);
        vec[2] = (int16_t)(dataRegisters[v * 2 + 1] & 0xFFFF);
    }

    // Translation vector selection (cv): TR, BK, FC, or none
    static constexpr uint32_t translationBase[3] = { 5, 13, 21 };
    static constexpr uint32_t irFlags[3]  = { FLAG_IR1_SAT, FLAG_IR2_SAT, FLAG_IR3_SAT };

    // Matrix multiply + translation (overflow checked after every addition)
    int64_t accumulator[3];
    for (int row = 0; row < 3; row++) {
        const int64_t translation = (cv == 3) ? 0 : (int64_t)(int32_t)controlRegisters[translationBase[cv] + row] * 4096;

        if (cv == 2) {
            // Hardware bug with the far color vector: the first partial sum (FC<<12 + M[row][0]*V.x)
            // still raises the MAC overflow and IR saturation flags (IR flag ignores lm), but its
            // value is thrown away. The result is only M[row][1]*V.y + M[row][2]*V.z.
            const int64_t discarded = accumulate44(translation, (int64_t)m[row][0] * vec[0], row, flags);
            const int32_t discardedShifted = (int32_t)(discarded >> shift);
            if (discardedShifted < -32768 || discardedShifted > 32767) flags |= irFlags[row];

            int64_t sum = (int64_t)m[row][1] * vec[1];
            sum = accumulate44(sum, (int64_t)m[row][2] * vec[2], row, flags);
            accumulator[row] = sum;
        } else {
            int64_t sum = translation;
            sum = accumulate44(sum, (int64_t)m[row][0] * vec[0], row, flags);
            sum = accumulate44(sum, (int64_t)m[row][1] * vec[1], row, flags);
            sum = accumulate44(sum, (int64_t)m[row][2] * vec[2], row, flags);
            accumulator[row] = sum;
        }
    }

    storeAccumulators(accumulator, shift, irLowerBoundFromInstruction(instruction), flags);
    commitFlags(flags);
}


void GTE::commandNCDS(uint32_t instruction) {
    uint32_t flags = 0;
    stepLightVertex(instruction, 0, flags);
    stepBackgroundAndLightColor(instruction, flags);
    stepColorTimesIRWithDepthCue(instruction, flags);
    pushColorFifo(flags);
    commitFlags(flags);
}


void GTE::commandCDP(uint32_t instruction) {
    uint32_t flags = 0;
    stepBackgroundAndLightColor(instruction, flags);
    stepColorTimesIRWithDepthCue(instruction, flags);
    pushColorFifo(flags);
    commitFlags(flags);
}


void GTE::commandNCDT(uint32_t instruction) {
    uint32_t flags = 0;
    for (uint32_t vertexIndex = 0; vertexIndex < 3; vertexIndex++) {
        stepLightVertex(instruction, vertexIndex, flags);
        stepBackgroundAndLightColor(instruction, flags);
        stepColorTimesIRWithDepthCue(instruction, flags);
        pushColorFifo(flags);
    }
    commitFlags(flags);
}


void GTE::commandNCCS(uint32_t instruction) {
    uint32_t flags = 0;
    stepLightVertex(instruction, 0, flags);
    stepBackgroundAndLightColor(instruction, flags);
    stepColorTimesIR(instruction, flags);
    pushColorFifo(flags);
    commitFlags(flags);
}


void GTE::commandCC(uint32_t instruction) {
    uint32_t flags = 0;
    stepBackgroundAndLightColor(instruction, flags);
    stepColorTimesIR(instruction, flags);
    pushColorFifo(flags);
    commitFlags(flags);
}


void GTE::commandNCS(uint32_t instruction) {
    uint32_t flags = 0;
    stepLightVertex(instruction, 0, flags);
    stepBackgroundAndLightColor(instruction, flags);
    pushColorFifo(flags);
    commitFlags(flags);
}


void GTE::commandNCT(uint32_t instruction) {
    uint32_t flags = 0;
    for (uint32_t vertexIndex = 0; vertexIndex < 3; vertexIndex++) {
        stepLightVertex(instruction, vertexIndex, flags);
        stepBackgroundAndLightColor(instruction, flags);
        pushColorFifo(flags);
    }
    commitFlags(flags);
}


void GTE::commandDPCT(uint32_t instruction) {
    const int shift = shiftFromInstruction(instruction);
    uint32_t flags = 0; // accumulates across all 3 color FIFO pushes

    for (int i = 0; i < 3; i++) {
        // DPCT always reads the OLDEST FIFO entry (RGB0). The push at the end of each pass shifts the next entry into RGB0, so three passes walk the whole FIFO.
        // The pushed CODE byte comes from RGBC (data register 6), not from the FIFO entry.
        const uint32_t rgb0 = dataRegisters[20];
        int64_t accumulator[3] = {
            (int64_t)(rgb0 & 0xFF) << 16,
            (int64_t)((rgb0 >> 8) & 0xFF) << 16,
            (int64_t)((rgb0 >> 16) & 0xFF) << 16
        };

        applyDepthCue(accumulator, shift, flags);
        storeAccumulators(accumulator, shift, irLowerBoundFromInstruction(instruction), flags);
        pushColorFifo(flags);
    }

    commitFlags(flags);
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


void GTE::commandAVSZ4(uint32_t instruction) {
    uint32_t flags = 0;

    int16_t ZSF4 = (int16_t)(controlRegisters[30] & 0xFFFF);

    uint16_t SZ0 = (uint16_t)(dataRegisters[16] & 0xFFFF);
    uint16_t SZ1 = (uint16_t)(dataRegisters[17] & 0xFFFF);
    uint16_t SZ2 = (uint16_t)(dataRegisters[18] & 0xFFFF);
    uint16_t SZ3 = (uint16_t)(dataRegisters[19] & 0xFFFF);

    int64_t mac0 = (int64_t)ZSF4 * ((int64_t)SZ0 + SZ1 + SZ2 + SZ3);

    if (mac0 > INT32_MAX) flags |= FLAG_MAC0_POS_OVF;
    if (mac0 < INT32_MIN) flags |= FLAG_MAC0_NEG_OVF;

    dataRegisters[24] = (uint32_t)(int32_t)mac0; // MAC0

    int32_t otz = clampSFlag(mac0 >> 12, 0, 0xFFFF, FLAG_SZ3_SAT, flags);
    dataRegisters[7] = (uint32_t)otz; // OTZ

    if (flags & FLAG_ERROR_MASK) flags |= FLAG_ERROR;
    controlRegisters[31] = flags; // FLAG register
}


void GTE::commandSQR(uint32_t instruction) {
    const bool sf = (instruction >> 19) & 1;
    const bool lm = (instruction >> 10) & 1;
    const int shift = sf ? 12 : 0;
    const int32_t irLo = lm ? 0 : -32768;

    uint32_t flags = 0;

    int16_t ir1 = (int16_t)(dataRegisters[9] & 0xFFFF);
    int16_t ir2 = (int16_t)(dataRegisters[10] & 0xFFFF);
    int16_t ir3 = (int16_t)(dataRegisters[11] & 0xFFFF);

    int64_t mac1 = (int64_t)ir1 * ir1;
    int64_t mac2 = (int64_t)ir2 * ir2;
    int64_t mac3 = (int64_t)ir3 * ir3;

    int32_t MAC1 = (int32_t)(mac1 >> shift);
    int32_t MAC2 = (int32_t)(mac2 >> shift);
    int32_t MAC3 = (int32_t)(mac3 >> shift);

    int32_t IR1 = clampSFlag(MAC1, irLo, 32767, FLAG_IR1_SAT, flags);
    int32_t IR2 = clampSFlag(MAC2, irLo, 32767, FLAG_IR2_SAT, flags);
    int32_t IR3 = clampSFlag(MAC3, irLo, 32767, FLAG_IR3_SAT, flags);

    dataRegisters[9]  = (uint32_t)IR1;
    dataRegisters[10] = (uint32_t)IR2;
    dataRegisters[11] = (uint32_t)IR3;
    dataRegisters[25] = (uint32_t)MAC1;
    dataRegisters[26] = (uint32_t)MAC2;
    dataRegisters[27] = (uint32_t)MAC3;

    if (flags & FLAG_ERROR_MASK) flags |= FLAG_ERROR;
    controlRegisters[31] = flags;
}


void GTE::commandDCPL(uint32_t instruction) {
    uint32_t flags = 0;
    stepColorTimesIRWithDepthCue(instruction, flags);
    pushColorFifo(flags);
    commitFlags(flags);
}


void GTE::commandRTPT(uint32_t instruction) {
    uint32_t flags = 0; // accumulates across all 3 vertices, committed once at the end

    for (uint32_t vertexIndex = 0; vertexIndex < 3; vertexIndex++) {
        stepPerspectiveTransform(instruction, vertexIndex, vertexIndex == 2, flags);
    }
    commitFlags(flags);
}


void GTE::commandGPF(uint32_t instruction) {
    const bool sf = (instruction >> 19) & 1;
    const bool lm = (instruction >> 10) & 1;
    const int shift = sf ? 12 : 0;
    const int32_t irLo = lm ? 0 : -32768;

    uint32_t flags = 0;

    int16_t IR0 = (int16_t)(dataRegisters[8] & 0xFFFF);
    int16_t IR1 = (int16_t)(dataRegisters[9] & 0xFFFF);
    int16_t IR2 = (int16_t)(dataRegisters[10] & 0xFFFF);
    int16_t IR3 = (int16_t)(dataRegisters[11] & 0xFFFF);

    // GPF: MAC = IR0 * IR (unlike GPL which adds existing MAC << shift)
    int64_t mac1 = (int64_t)IR0 * IR1;
    int64_t mac2 = (int64_t)IR0 * IR2;
    int64_t mac3 = (int64_t)IR0 * IR3;

    flagMac44BitOverflow(mac1, FLAG_MAC1_POS_OVF, FLAG_MAC1_NEG_OVF, flags);
    flagMac44BitOverflow(mac2, FLAG_MAC2_POS_OVF, FLAG_MAC2_NEG_OVF, flags);
    flagMac44BitOverflow(mac3, FLAG_MAC3_POS_OVF, FLAG_MAC3_NEG_OVF, flags);

    int32_t MAC1 = (int32_t)(mac1 >> shift);
    int32_t MAC2 = (int32_t)(mac2 >> shift);
    int32_t MAC3 = (int32_t)(mac3 >> shift);

    int32_t outIR1 = clampSFlag(MAC1, irLo, 32767, FLAG_IR1_SAT, flags);
    int32_t outIR2 = clampSFlag(MAC2, irLo, 32767, FLAG_IR2_SAT, flags);
    int32_t outIR3 = clampSFlag(MAC3, irLo, 32767, FLAG_IR3_SAT, flags);

    dataRegisters[9]  = (uint32_t)outIR1;
    dataRegisters[10] = (uint32_t)outIR2;
    dataRegisters[11] = (uint32_t)outIR3;
    dataRegisters[25] = (uint32_t)MAC1;
    dataRegisters[26] = (uint32_t)MAC2;
    dataRegisters[27] = (uint32_t)MAC3;

    pushColorFifo(flags);

    if (flags & FLAG_ERROR_MASK) flags |= FLAG_ERROR;
    controlRegisters[31] = flags;
}


void GTE::commandGPL(uint32_t instruction) {
    const bool sf = (instruction >> 19) & 1;
    const bool lm = (instruction >> 10) & 1;
    const int shift = sf ? 12 : 0;
    const int32_t irLo = lm ? 0 : -32768;

    uint32_t flags = 0;

    int32_t MAC1_in = (int32_t)dataRegisters[25];
    int32_t MAC2_in = (int32_t)dataRegisters[26];
    int32_t MAC3_in = (int32_t)dataRegisters[27];

    int16_t IR0 = (int16_t)(dataRegisters[8] & 0xFFFF);
    int16_t IR1 = (int16_t)(dataRegisters[9] & 0xFFFF);
    int16_t IR2 = (int16_t)(dataRegisters[10] & 0xFFFF);
    int16_t IR3 = (int16_t)(dataRegisters[11] & 0xFFFF);

    // MAC = (MAC << shift) + (IR0 * IR)
    int64_t mac1 = ((int64_t)MAC1_in << shift) + ((int64_t)IR0 * IR1);
    int64_t mac2 = ((int64_t)MAC2_in << shift) + ((int64_t)IR0 * IR2);
    int64_t mac3 = ((int64_t)MAC3_in << shift) + ((int64_t)IR0 * IR3);

    flagMac44BitOverflow(mac1, FLAG_MAC1_POS_OVF, FLAG_MAC1_NEG_OVF, flags);
    flagMac44BitOverflow(mac2, FLAG_MAC2_POS_OVF, FLAG_MAC2_NEG_OVF, flags);
    flagMac44BitOverflow(mac3, FLAG_MAC3_POS_OVF, FLAG_MAC3_NEG_OVF, flags);

    int32_t MAC1 = (int32_t)(mac1 >> shift);
    int32_t MAC2 = (int32_t)(mac2 >> shift);
    int32_t MAC3 = (int32_t)(mac3 >> shift);

    int32_t outIR1 = clampSFlag(MAC1, irLo, 32767, FLAG_IR1_SAT, flags);
    int32_t outIR2 = clampSFlag(MAC2, irLo, 32767, FLAG_IR2_SAT, flags);
    int32_t outIR3 = clampSFlag(MAC3, irLo, 32767, FLAG_IR3_SAT, flags);

    dataRegisters[9]  = (uint32_t)outIR1;
    dataRegisters[10] = (uint32_t)outIR2;
    dataRegisters[11] = (uint32_t)outIR3;
    dataRegisters[25] = (uint32_t)MAC1;
    dataRegisters[26] = (uint32_t)MAC2;
    dataRegisters[27] = (uint32_t)MAC3;

    pushColorFifo(flags);

    if (flags & FLAG_ERROR_MASK) flags |= FLAG_ERROR;
    controlRegisters[31] = flags;
}


void GTE::commandNCCT(uint32_t instruction) {
    uint32_t flags = 0;
    for (uint32_t vertexIndex = 0; vertexIndex < 3; vertexIndex++) {
        stepLightVertex(instruction, vertexIndex, flags);
        stepBackgroundAndLightColor(instruction, flags);
        stepColorTimesIR(instruction, flags);
        pushColorFifo(flags);
    }
    commitFlags(flags);
}



//Privates:: Internal Pipeline
uint32_t GTE::divideProjection(uint16_t projectionPlaneDistance, uint16_t screenZ, uint32_t& flags) {
    // If the result would be 2.0 or more (or screenZ is 0), the hardware saturates and flags it.
    if (projectionPlaneDistance >= (uint32_t)screenZ * 2) {
        flags |= FLAG_DIVIDE_OVF;
        return 0x1FFFF;
    }

    // Normalize: shift screenZ up until its top bit is set (so it lies in 0x8000..0xFFFF),
    // and shift the numerator by the same amount so the ratio is unchanged.
    uint32_t shiftAmount = countLeadingZeros(screenZ) - 16;
    uint64_t numerator = (uint64_t)projectionPlaneDistance << shiftAmount;
    uint32_t denominator = (uint32_t)screenZ << shiftAmount;

    // First guess at 1/denominator, chosen from the top bits of the denominator.
    uint32_t seed = reciprocalSeedTable[(denominator - 0x7FC0) >> 7] + 0x101;

    // Two Newton-Raphson refinement passes sharpen the guess into the reciprocal.
    uint32_t reciprocal = (uint32_t)((0x2000080ull - (uint64_t)denominator * seed) >> 8);
    reciprocal = (uint32_t)((0x0000080ull + (uint64_t)reciprocal * seed) >> 8);

    // Multiply by the numerator (+0x8000 rounds to nearest) and cap at 17 bits.
    uint64_t result = ((numerator * reciprocal) + 0x8000) >> 16;
    if (result > 0x1FFFF) {
        result = 0x1FFFF;
    }
    return (uint32_t)result;
}


void GTE::stepPerspectiveTransform(uint32_t instruction, uint32_t vertexIndex, bool isLastVertex, uint32_t& flags) {
    const int shift = shiftFromInstruction(instruction);
    const int32_t irLo = irLowerBoundFromInstruction(instruction);

    int32_t rotation[3][3];
    readMatrix(controlRegisters, 0, rotation);

    // Perspective projection parameters
    const int32_t  OFX = (int32_t)controlRegisters[24];
    const int32_t  OFY = (int32_t)controlRegisters[25];
    const uint16_t H   = (uint16_t)(controlRegisters[26] & 0xFFFF);
    const int16_t  DQA = (int16_t)(controlRegisters[27] & 0xFFFF);
    const int32_t  DQB = (int32_t)controlRegisters[28];

    // Vertex n lives in data registers (2n, 2n+1)
    const int32_t vx = (int16_t)(dataRegisters[vertexIndex * 2] & 0xFFFF);
    const int32_t vy = (int16_t)(dataRegisters[vertexIndex * 2] >> 16);
    const int32_t vz = (int16_t)(dataRegisters[vertexIndex * 2 + 1] & 0xFFFF);

    // Rotate + translate
    int64_t accumulator[3];
    for (int row = 0; row < 3; row++) {
        int64_t sum = (int64_t)(int32_t)controlRegisters[5 + row] * 4096;
        sum = accumulate44(sum, (int64_t)rotation[row][0] * vx, row, flags);
        sum = accumulate44(sum, (int64_t)rotation[row][1] * vy, row, flags);
        sum = accumulate44(sum, (int64_t)rotation[row][2] * vz, row, flags);
        accumulator[row] = sum;
    }

    const int32_t MAC1 = (int32_t)(accumulator[0] >> shift);
    const int32_t MAC2 = (int32_t)(accumulator[1] >> shift);
    const int32_t MAC3 = (int32_t)(accumulator[2] >> shift);

    const int32_t IR1 = clampSFlag(MAC1, irLo, 32767, FLAG_IR1_SAT, flags);
    const int32_t IR2 = clampSFlag(MAC2, irLo, 32767, FLAG_IR2_SAT, flags);

    // Hardware quirk: IR3's *value* saturates from MAC3 as usual (honouring lm), but its *flag* is raised from (accumulator >> 12) against the full signed 16-bit range, ignoring sf and lm.
    // That 12-bit-shifted value is also what feeds SZ3.
    const int32_t z = (int32_t)(accumulator[2] >> 12);
    const int32_t IR3 = std::clamp(MAC3, irLo, 32767);
    if (z < -32768 || z > 32767) flags |= FLAG_IR3_SAT;

    const uint32_t SZ3 = (uint32_t)clampSFlag(z, 0, 0xFFFF, FLAG_SZ3_SAT, flags);

    // Push into the SZ FIFO (registers 16-19)
    dataRegisters[16] = dataRegisters[17];
    dataRegisters[17] = dataRegisters[18];
    dataRegisters[18] = dataRegisters[19];
    dataRegisters[19] = SZ3;

    const uint32_t n = divideProjection(H, (uint16_t)SZ3, flags);

    const int64_t mac0X = (int64_t)n * IR1 + OFX;
    const int64_t mac0Y = (int64_t)n * IR2 + OFY;

    checkMac0Overflow(mac0X, flags);
    checkMac0Overflow(mac0Y, flags);

    const int32_t SX  = clampSFlag(mac0X >> 16, -0x400, 0x3FF, FLAG_SX2_SAT, flags);
    const int32_t SY  = clampSFlag(mac0Y >> 16, -0x400, 0x3FF, FLAG_SY2_SAT, flags);

    // Depth cue (IR0/MAC0): earlier RTPT vertices leave no trace in registers or flags
    if (isLastVertex) {
        const int64_t mac0Z = (int64_t)n * DQA + DQB;
        checkMac0Overflow(mac0Z, flags);
        const int32_t IR0 = clampSFlag(mac0Z >> 12, 0, 0x1000, FLAG_IR0_SAT, flags);
        dataRegisters[8]  = (uint32_t)IR0;
        dataRegisters[24] = (uint32_t)(int32_t)mac0Z; // MAC0
    }

    // Push into the SXY FIFO (registers 12-14, mirrored to 15)
    dataRegisters[12] = dataRegisters[13];
    dataRegisters[13] = dataRegisters[14];
    dataRegisters[14] = ((uint32_t)(uint16_t)SY << 16) | (uint16_t)(uint32_t)SX;
    dataRegisters[15] = dataRegisters[14];

    dataRegisters[9]  = (uint32_t)IR1;
    dataRegisters[10] = (uint32_t)IR2;
    dataRegisters[11] = (uint32_t)IR3;
    dataRegisters[25] = (uint32_t)MAC1;
    dataRegisters[26] = (uint32_t)MAC2;
    dataRegisters[27] = (uint32_t)MAC3;
}


void GTE::stepLightVertex(uint32_t instruction, uint32_t vertexIndex, uint32_t& flags) {
    int32_t lightMatrix[3][3];
    readMatrix(controlRegisters, 8, lightMatrix);

    // Vertex n lives in data registers (2n, 2n+1): X and Y packed in the first, Z in the second.
    const int32_t vertexX = (int16_t)(dataRegisters[vertexIndex * 2] & 0xFFFF);
    const int32_t vertexY = (int16_t)(dataRegisters[vertexIndex * 2] >> 16);
    const int32_t vertexZ = (int16_t)(dataRegisters[vertexIndex * 2 + 1] & 0xFFFF);

    int64_t accumulator[3];
    for (int row = 0; row < 3; row++) {
        accumulator[row] = (int64_t)lightMatrix[row][0] * vertexX
                         + (int64_t)lightMatrix[row][1] * vertexY
                         + (int64_t)lightMatrix[row][2] * vertexZ;
    }
    storeAccumulators(accumulator, shiftFromInstruction(instruction), irLowerBoundFromInstruction(instruction), flags);
}


void GTE::stepBackgroundAndLightColor(uint32_t instruction, uint32_t& flags) {
    int32_t lightColorMatrix[3][3];
    readMatrix(controlRegisters, 16, lightColorMatrix);

    const int32_t intermediate[3] = {
        (int16_t)(dataRegisters[9]  & 0xFFFF),
        (int16_t)(dataRegisters[10] & 0xFFFF),
        (int16_t)(dataRegisters[11] & 0xFFFF)
    };

    int64_t accumulator[3];
    for (int row = 0; row < 3; row++) {
        const int64_t backgroundColor = (int32_t)controlRegisters[13 + row];
        int64_t sum = backgroundColor * 4096;
        sum = accumulate44(sum, (int64_t)lightColorMatrix[row][0] * intermediate[0], row, flags);
        sum = accumulate44(sum, (int64_t)lightColorMatrix[row][1] * intermediate[1], row, flags);
        sum = accumulate44(sum, (int64_t)lightColorMatrix[row][2] * intermediate[2], row, flags);
        accumulator[row] = sum;
    }
    storeAccumulators(accumulator, shiftFromInstruction(instruction), irLowerBoundFromInstruction(instruction), flags);
}


void GTE::stepColorTimesIR(uint32_t instruction, uint32_t& flags) {
    int64_t accumulator[3];
    multiplyColorByIntermediate(dataRegisters, accumulator);
    storeAccumulators(accumulator, shiftFromInstruction(instruction), irLowerBoundFromInstruction(instruction), flags);
}


void GTE::stepColorTimesIRWithDepthCue(uint32_t instruction, uint32_t& flags) {
    const int shift = shiftFromInstruction(instruction);

    int64_t accumulator[3];
    multiplyColorByIntermediate(dataRegisters, accumulator);
    applyDepthCue(accumulator, shift, flags);
    storeAccumulators(accumulator, shift, irLowerBoundFromInstruction(instruction), flags);
}


void GTE::stepInterpolateIR(uint32_t instruction, uint32_t& flags) {
    const int shift = shiftFromInstruction(instruction);

    int64_t accumulator[3];
    for (int channel = 0; channel < 3; channel++) {
        const int64_t intermediate = (int16_t)(dataRegisters[9 + channel] & 0xFFFF);
        accumulator[channel] = intermediate * 4096;
    }
    applyDepthCue(accumulator, shift, flags);
    storeAccumulators(accumulator, shift, irLowerBoundFromInstruction(instruction), flags);
}


void GTE::applyDepthCue(int64_t accumulator[3], int shift, uint32_t& flags) const {
    const int64_t farColor[3] = {
        (int32_t)controlRegisters[21],
        (int32_t)controlRegisters[22],
        (int32_t)controlRegisters[23]
    };

    const uint32_t positiveOverflowFlags[3] = { FLAG_MAC1_POS_OVF, FLAG_MAC2_POS_OVF, FLAG_MAC3_POS_OVF };
    const uint32_t negativeOverflowFlags[3] = { FLAG_MAC1_NEG_OVF, FLAG_MAC2_NEG_OVF, FLAG_MAC3_NEG_OVF };
    const uint32_t irSaturationFlags[3]     = { FLAG_IR1_SAT, FLAG_IR2_SAT, FLAG_IR3_SAT };

    const int32_t interpolationFactor = (int16_t)(dataRegisters[8] & 0xFFFF); // IR0

    for (int channel = 0; channel < 3; channel++) {
        const int64_t difference = farColor[channel] * 4096 - accumulator[channel];
        flagMac44BitOverflow(difference, positiveOverflowFlags[channel], negativeOverflowFlags[channel], flags);

        const int32_t saturatedDifference =
            clampSFlag((int32_t)(difference >> shift), -32768, 32767, irSaturationFlags[channel], flags);

        accumulator[channel] = (int64_t)saturatedDifference * interpolationFactor + accumulator[channel];
    }
}


void GTE::storeAccumulators(const int64_t accumulator[3], int shift, int32_t irLowerBound, uint32_t& flags) {
    flagMac44BitOverflow(accumulator[0], FLAG_MAC1_POS_OVF, FLAG_MAC1_NEG_OVF, flags);
    flagMac44BitOverflow(accumulator[1], FLAG_MAC2_POS_OVF, FLAG_MAC2_NEG_OVF, flags);
    flagMac44BitOverflow(accumulator[2], FLAG_MAC3_POS_OVF, FLAG_MAC3_NEG_OVF, flags);

    const int32_t mac1 = (int32_t)(accumulator[0] >> shift);
    const int32_t mac2 = (int32_t)(accumulator[1] >> shift);
    const int32_t mac3 = (int32_t)(accumulator[2] >> shift);

    dataRegisters[25] = (uint32_t)mac1;
    dataRegisters[26] = (uint32_t)mac2;
    dataRegisters[27] = (uint32_t)mac3;

    dataRegisters[9]  = (uint32_t)clampSFlag(mac1, irLowerBound, 32767, FLAG_IR1_SAT, flags);
    dataRegisters[10] = (uint32_t)clampSFlag(mac2, irLowerBound, 32767, FLAG_IR2_SAT, flags);
    dataRegisters[11] = (uint32_t)clampSFlag(mac3, irLowerBound, 32767, FLAG_IR3_SAT, flags);
}


void GTE::pushColorFifo(uint32_t& flags) {
    const int32_t mac1 = (int32_t)dataRegisters[25];
    const int32_t mac2 = (int32_t)dataRegisters[26];
    const int32_t mac3 = (int32_t)dataRegisters[27];
    const uint32_t code = (dataRegisters[6] >> 24) & 0xFF;

    const uint32_t red   = (uint32_t)clampSFlag(mac1 >> 4, 0, 255, FLAG_COLOR_R_SAT, flags);
    const uint32_t green = (uint32_t)clampSFlag(mac2 >> 4, 0, 255, FLAG_COLOR_G_SAT, flags);
    const uint32_t blue  = (uint32_t)clampSFlag(mac3 >> 4, 0, 255, FLAG_COLOR_B_SAT, flags);

    dataRegisters[20] = dataRegisters[21];
    dataRegisters[21] = dataRegisters[22];
    dataRegisters[22] = (code << 24) | (blue << 16) | (green << 8) | red;
}


void GTE::commitFlags(uint32_t flags) {
    if (flags & FLAG_ERROR_MASK) flags |= FLAG_ERROR;
    controlRegisters[31] = flags;
}


//Privates: Helpers
uint32_t GTE::countLeadingZeros(uint32_t value) {
    uint32_t count = 0;

    for (uint32_t mask = 0x80000000u; mask != 0 && (value & mask) == 0; mask >>= 1) {
        count++;
    }

    return count;
}


int32_t GTE::clampSFlag(int64_t val, int32_t lo, int32_t hi, uint32_t flagBit, uint32_t& flags) {
    if (val < lo) { flags |= flagBit; return lo; }
    if (val > hi) { flags |= flagBit; return hi; }
    return (int32_t)val;
}


void GTE::checkMac0Overflow(int64_t value, uint32_t& flags) {
    if (value > INT32_MAX) flags |= (1u << 16); // FLAG_MAC0_POS_OVF
    if (value < INT32_MIN) flags |= (1u << 15); // FLAG_MAC0_NEG_OVF
}


void GTE::flagMac44BitOverflow(int64_t value, uint32_t positiveFlag, uint32_t negativeFlag, uint32_t& flags) {
    if (value > MAC_44BIT_MAX) flags |= positiveFlag;
    if (value < MAC_44BIT_MIN) flags |= negativeFlag;
}


int64_t GTE::accumulate44(int64_t accumulator, int64_t term, int channel, uint32_t& flags) {
    const int64_t sum = accumulator + term;
    if (sum > MAC_44BIT_MAX) flags |= (1u << (30 - channel)); // MAC1/2/3 positive overflow: bits 30/29/28
    if (sum < MAC_44BIT_MIN) flags |= (1u << (27 - channel)); // MAC1/2/3 negative overflow: bits 27/26/25
    return (int64_t)((uint64_t)sum << 20) >> 20; // sign-extend from 44 bits
}


void GTE::readMatrix(const uint32_t* ctrlRegs, uint32_t firstRegister, int32_t matrix[3][3]) {
    matrix[0][0] = (int16_t)(ctrlRegs[firstRegister] & 0xFFFF);
    matrix[0][1] = (int16_t)(ctrlRegs[firstRegister] >> 16);
    matrix[0][2] = (int16_t)(ctrlRegs[firstRegister + 1] & 0xFFFF);
    matrix[1][0] = (int16_t)(ctrlRegs[firstRegister + 1] >> 16);
    matrix[1][1] = (int16_t)(ctrlRegs[firstRegister + 2] & 0xFFFF);
    matrix[1][2] = (int16_t)(ctrlRegs[firstRegister + 2] >> 16);
    matrix[2][0] = (int16_t)(ctrlRegs[firstRegister + 3] & 0xFFFF);
    matrix[2][1] = (int16_t)(ctrlRegs[firstRegister + 3] >> 16);
    matrix[2][2] = (int16_t)(ctrlRegs[firstRegister + 4] & 0xFFFF);
}


void GTE::multiplyColorByIntermediate(const uint32_t* regs, int64_t accumulator[3]) {
    const uint32_t rgbc = regs[6];
    const int64_t color[3] = { (int64_t)(rgbc & 0xFF), (int64_t)((rgbc >> 8) & 0xFF), (int64_t)((rgbc >> 16) & 0xFF) };

    for (int channel = 0; channel < 3; channel++) {
        const int64_t intermediate = (int16_t)(regs[9 + channel] & 0xFFFF);
        accumulator[channel] = color[channel] * intermediate * 16;
    }
}

