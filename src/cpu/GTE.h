#pragma once
#include <cstdint>

class GTE {
public:
    GTE();

    void writeDataRegister(uint32_t index, uint32_t value);
    uint32_t readDataRegister(uint32_t index) const;

    void writeControlRegister(uint32_t index, uint32_t value);
    uint32_t readControlRegister(uint32_t index) const;

    void executeCommand(uint32_t instruction);

private:
    // Core Registers 
    uint32_t dataRegisters[32];
    uint32_t controlRegisters[32];


    // Reciprocal Lookup Table
    static constexpr uint32_t RECIPROCAL_SEED_TABLE_SIZE = 257;
    uint32_t reciprocalSeedTable[RECIPROCAL_SEED_TABLE_SIZE];
    void buildReciprocalSeedTable();


    // Constants for 44-bit Accumulator Limits
    static constexpr int64_t MAC_44BIT_MAX =  ((int64_t)1 << 43) - 1;
    static constexpr int64_t MAC_44BIT_MIN = -((int64_t)1 << 43);


    // Command Implementations
    void commandRTPS(uint32_t instruction);
    void commandNCLIP(uint32_t instruction);
    void commandOP(uint32_t instruction);
    void commandDPCS(uint32_t instruction);
    void commandINTPL(uint32_t instruction);
    void commandMVMVA(uint32_t instruction);
    void commandNCDS(uint32_t instruction);
    void commandCDP(uint32_t instruction);
    void commandNCDT(uint32_t instruction);
    void commandNCCS(uint32_t instruction);
    void commandCC(uint32_t instruction);
    void commandNCS(uint32_t instruction);
    void commandNCT(uint32_t instruction);
    void commandDPCT(uint32_t instruction);
    void commandAVSZ3(uint32_t instruction);
    void commandAVSZ4(uint32_t instruction);
    void commandSQR(uint32_t instruction);
    void commandDCPL(uint32_t instruction);
    void commandRTPT(uint32_t instruction);
    void commandGPF(uint32_t instruction);
    void commandGPL(uint32_t instruction);
    void commandNCCT(uint32_t instruction);    


    // Helpers
    static inline uint32_t countLeadingZeros(uint32_t value) {
        uint32_t count = 0;
        for (uint32_t mask = 0x80000000u; mask != 0 && (value & mask) == 0; mask >>= 1) {
            count++;
        }
        return count;
    }

    static inline int32_t clampSFlag(int64_t val, int32_t lo, int32_t hi, uint32_t flagBit, uint32_t& flags) {
        if (val < lo) { flags |= flagBit; return lo; }
        if (val > hi) { flags |= flagBit; return hi; }
        return (int32_t)val;
    }

    static inline void checkMac0Overflow(int64_t value, uint32_t& flags) {
        if (value > INT32_MAX) flags |= (1u << 16); // FLAG_MAC0_POS_OVF
        if (value < INT32_MIN) flags |= (1u << 15); // FLAG_MAC0_NEG_OVF
    }

    static inline void flagMac44BitOverflow(int64_t value, uint32_t positiveFlag, uint32_t negativeFlag, uint32_t& flags) {
        if (value > MAC_44BIT_MAX) flags |= positiveFlag;
        if (value < MAC_44BIT_MIN) flags |= negativeFlag;
    }

    // One addition into a 44-bit MAC accumulator (channel 0..2 = MAC1..MAC3). The hardware checks for
    // overflow after EVERY addition in a multi-term sum, not just the last one, and the partial sum
    // wraps to 44 bits, so a later term can raise the opposite overflow flag.
    static inline int64_t accumulate44(int64_t accumulator, int64_t term, int channel, uint32_t& flags) {
        const int64_t sum = accumulator + term;
        if (sum > MAC_44BIT_MAX) flags |= (1u << (30 - channel)); // MAC1/2/3 positive overflow: bits 30/29/28
        if (sum < MAC_44BIT_MIN) flags |= (1u << (27 - channel)); // MAC1/2/3 negative overflow: bits 27/26/25
        return (int64_t)((uint64_t)sum << 20) >> 20;              // sign-extend from 44 bits
    }

    static inline int shiftFromInstruction(uint32_t instruction) {
        return ((instruction >> 19) & 1) ? 12 : 0;
    }

    static inline int32_t irLowerBoundFromInstruction(uint32_t instruction) {
        return ((instruction >> 10) & 1) ? 0 : -32768;
    }

    static inline void readMatrix(const uint32_t* ctrlRegs, uint32_t firstRegister, int32_t matrix[3][3]) {
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

    static inline void multiplyColorByIntermediate(const uint32_t* regs, int64_t accumulator[3]) {
        const uint32_t rgbc = regs[6];
        const int64_t color[3] = { (int64_t)(rgbc & 0xFF), (int64_t)((rgbc >> 8) & 0xFF), (int64_t)((rgbc >> 16) & 0xFF) };
        for (int channel = 0; channel < 3; channel++) {
            const int64_t intermediate = (int16_t)(regs[9 + channel] & 0xFFFF);
            accumulator[channel] = color[channel] * intermediate * 16;
        }
    }


    uint32_t divideProjection(uint16_t projectionPlaneDistance, uint16_t screenZ, uint32_t& flags);
    

    void stepPerspectiveTransform(uint32_t instruction, uint32_t vertexIndex, bool isLastVertex, uint32_t& flags);
    void stepLightVertex(uint32_t instruction, uint32_t vertexIndex, uint32_t& flags);
    void stepBackgroundAndLightColor(uint32_t instruction, uint32_t& flags);
    void stepColorTimesIR(uint32_t instruction, uint32_t& flags);
    void stepColorTimesIRWithDepthCue(uint32_t instruction, uint32_t& flags);
    void stepInterpolateIR(uint32_t instruction, uint32_t& flags);
    void applyDepthCue(int64_t accumulator[3], int shift, uint32_t& flags) const;
    void storeAccumulators(const int64_t accumulator[3], int shift, int32_t irLowerBound, uint32_t& flags);
    void pushColorFifo(uint32_t& flags);
    void commitFlags(uint32_t flags);
};