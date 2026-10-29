#pragma once

#include <cstdint>


/// @brief Geometry Transformation Engine (GTE) Coprocessor (COP2).
/// Handles 3D vector math, perspective projection, and lighting calculations.
class GTE {
public:
    GTE();

    /// @brief Reads a 32-bit value from the specified GTE data register.
    /// @param index Register index (0-31).
    /// @return The value from the register.
    uint32_t readDataRegister(uint32_t index) const;


    /// @brief Writes a 32-bit value to the specified GTE data register.
    /// @param index Register index (0-31).
    /// @param value The value to write.
    void writeDataRegister(uint32_t index, uint32_t value);
    

    /// @brief Reads a 32-bit value from the specified GTE control register.
    /// @param index Register index (0-31).
    /// @return The value from the register.
    uint32_t readControlRegister(uint32_t index) const;


    /// @brief Writes a 32-bit value to the specified GTE control register.
    /// @param index Register index (0-31).
    /// @param value The value to write.
    void writeControlRegister(uint32_t index, uint32_t value);
    

    /// @brief Executes a GTE instruction.
    /// @param instruction The 32-bit COP2 instruction word.
    void executeCommand(uint32_t instruction);

private:
    uint32_t dataRegisters[32];
    uint32_t controlRegisters[32];

    static constexpr uint32_t RECIPROCAL_SEED_TABLE_SIZE = 257;
    uint32_t reciprocalSeedTable[RECIPROCAL_SEED_TABLE_SIZE];
    
    // Constants for 44-bit Accumulator Limits
    static constexpr int64_t MAC_44BIT_MAX =  ((int64_t)1 << 43) - 1;
    static constexpr int64_t MAC_44BIT_MIN = -((int64_t)1 << 43);

    // FLAG Register (Control Register 31) Bit Masks
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
    
    static constexpr uint32_t FLAG_ERROR_MASK   = (0xFFu << 23) | (0x3Fu << 13);


    /// @brief Initializes the hardware lookup table used for fast division approximation in perspective calculations.
    void buildReciprocalSeedTable();


    // Command Implementations
    // (See executeCommand for dispatcher logic)
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


    // Internal Pipeline

    /// @brief Executes a hardware divide using the reciprocal seed table to calculate the perspective ratio (H/SZ).
    /// @param projectionPlaneDistance The focal length (H).
    /// @param screenZ The Z-depth of the transformed vertex (SZ).
    /// @param flags Reference to the instruction's running flag state.
    /// @return The truncated 32-bit perspective ratio.
    uint32_t divideProjection(uint16_t projectionPlaneDistance, uint16_t screenZ, uint32_t& flags);
    

    /// @brief Rotates, translates, and applies perspective projection to a single vertex.
    /// @param instruction The COP2 instruction word.
    /// @param vertexIndex Which of the three input vectors (V0, V1, V2) to process.
    /// @param isLastVertex True if this is the final vertex in a triple operation (triggers FIFO pushes).
    /// @param flags Reference to the instruction's running flag state.
    void stepPerspectiveTransform(uint32_t instruction, uint32_t vertexIndex, bool isLastVertex, uint32_t& flags);


    /// @brief Multiplies a vertex normal by the Light Matrix (LL) and accumulates the background/ambient light (BK).
    /// @param instruction The COP2 instruction word.
    /// @param vertexIndex Which normal vector (N0, N1, N2) to process.
    /// @param flags Reference to the instruction's running flag state.
    void stepLightVertex(uint32_t instruction, uint32_t vertexIndex, uint32_t& flags);


    /// @brief Multiplies the local light vector by the Light Color Matrix (LC) and stores the result.
    /// @param instruction The COP2 instruction word.
    /// @param flags Reference to the instruction's running flag state.
    void stepBackgroundAndLightColor(uint32_t instruction, uint32_t& flags);


    /// @brief Multiplies the active color register (RGBC) by the intermediate light vectors (IR1, IR2, IR3).
    /// @param instruction The COP2 instruction word.
    /// @param flags Reference to the instruction's running flag state.
    void stepColorTimesIR(uint32_t instruction, uint32_t& flags);


    /// @brief Multiplies the active color register by the intermediate vectors and applies depth cueing (fog).
    /// @param instruction The COP2 instruction word.
    /// @param flags Reference to the instruction's running flag state.
    void stepColorTimesIRWithDepthCue(uint32_t instruction, uint32_t& flags);


    /// @brief Interpolates the intermediate vectors using the active color register.
    /// @param instruction The COP2 instruction word.
    /// @param flags Reference to the instruction's running flag state.
    void stepInterpolateIR(uint32_t instruction, uint32_t& flags);


    /// @brief Applies depth cueing (fog) to the provided accumulators based on the Z depth.
    /// @param accumulator The 3-channel 64-bit MAC accumulators.
    /// @param shift The hardware shift multiplier (0 or 12).
    /// @param flags Reference to the instruction's running flag state.
    void applyDepthCue(int64_t accumulator[3], int shift, uint32_t& flags) const;


    /// @brief Clamps and stores the 64-bit accumulators into the 16-bit Intermediate Registers (IR).
    /// @param accumulator The 3-channel 64-bit MAC accumulators.
    /// @param shift The hardware shift multiplier (0 or 12).
    /// @param irLowerBound The clamp floor for the IR registers (0 or -32768).
    /// @param flags Reference to the instruction's running flag state.
    void storeAccumulators(const int64_t accumulator[3], int shift, int32_t irLowerBound, uint32_t& flags);


    /// @brief Pushes the clamped MAC/IR results into the RGB color FIFO.
    /// @param flags Reference to the instruction's running flag state.
    void pushColorFifo(uint32_t& flags);


    /// @brief Merges the local instruction flags into the global GTE Status Register (Register 31).
    /// @param flags The accumulated flags from the just-completed instruction.
    void commitFlags(uint32_t flags);;


    // Helpers

    /// @brief Counts the number of leading zero bits in a 32-bit value.
    static uint32_t countLeadingZeros(uint32_t value);


    /// @brief Clamps a 64-bit value to a 32-bit range, setting a specific flag bit on overflow/underflow.
    static int32_t clampSFlag(int64_t val, int32_t lo, int32_t hi, uint32_t flagBit, uint32_t& flags);


    /// @brief Checks a 64-bit MAC0 value against 32-bit boundaries and sets standard overflow/underflow flags.
    static void checkMac0Overflow(int64_t value, uint32_t& flags);


    /// @brief Flags if a 64-bit intermediate sum exceeds the 44-bit hardware accumulator boundaries.
    static void flagMac44BitOverflow(int64_t value, uint32_t positiveFlag, uint32_t negativeFlag, uint32_t& flags);


    /// @brief Adds a term into a 44-bit MAC accumulator (channel 0..2 = MAC1..MAC3), setting flags and sign-extending the wrapped partial sum.
    // The hardware checks for overflow after EVERY addition in a multi-term sum, not just the last one, and the partial sum wraps to 44 bits, so a later term can raise the opposite overflow flag.
    static int64_t accumulate44(int64_t accumulator, int64_t term, int channel, uint32_t& flags);


    /// @brief Reads a 3x3 rotational/light matrix directly from GTE control registers into a localized array.
    static void readMatrix(const uint32_t* ctrlRegs, uint32_t firstRegister, int32_t matrix[3][3]);


    /// @brief Multiplies the active color register (RGBC) by the intermediate IR values, loading the result into the accumulators.
    static void multiplyColorByIntermediate(const uint32_t* regs, int64_t accumulator[3]);


    /// @brief Extracts the hardware shift multiplier (0 or 12 bits) from the instruction word (bit 19).
    static int shiftFromInstruction(uint32_t instruction) { return ((instruction >> 19) & 1) ? 12 : 0; }


    /// @brief Extracts the IR lower bound clamp (0 or -32768) from the instruction word (bit 10).
    static int32_t irLowerBoundFromInstruction(uint32_t instruction) { return ((instruction >> 10) & 1) ? 0 : -32768; }
};