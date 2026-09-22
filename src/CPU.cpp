#include "CPU.h"
#include "Constants.h"
#include <iostream>

#include <cstdio>

CPU::CPU(Bus* bus) : bus(bus) {
    programCounter = Hardware::BIOS_STARTING_ADDRESS;
    nextProgramCounter = programCounter + Hardware::INSTRUCTION_SIZE;
    
    // Initialize COP0 Processor ID (PRid) to Sony's silicon version
    coprocessor0Registers[15] = 0x00000002;

    // Initialize COP0 Status Register (Set BEV bit to 1, and enable COP0)
    coprocessor0Registers[12] = 0x10400000;
}


void CPU::step() {
    bool interruptsCurrentlyEnabled = (coprocessor0Registers[12] & 0x1) != 0;

    isDelaySlot = nextIsDelaySlot;
    nextIsDelaySlot = false;

    if (bus->hasPendingInterrupts() && interruptsCurrentlyEnabled) {
        if (!isDelaySlot) {
            // Flush any pending load from the previous instruction before jumping
            if (pendingLoadRegister != 0) {
                setRegister(pendingLoadRegister, pendingLoadValue);
                pendingLoadRegister = 0;
                pendingLoadValue = 0;
            }

            triggerHardwareInterrupt();
            bus->tickHardware(1);
            instructionCount++;
            return; 
        }
    }

    uint32_t registerToUpdate = pendingLoadRegister;
    uint32_t valueToUpdate = pendingLoadValue;

    pendingLoadRegister = 0;
    pendingLoadValue = 0;

    currentProgramCounter = programCounter;

    // Cache calculation variables (staged for future I-Cache implementation)
    uint32_t physicalProgramCounter = programCounter & 0x1FFFFFFF;
    uint32_t cacheIndex = (physicalProgramCounter >> 2) & 0x3FF; 
    uint32_t cacheTag = physicalProgramCounter & 0xFFFFF000;         
    bool isUncachedRegion = (programCounter & 0xE0000000) == 0xA0000000;
    
    uint32_t instruction = bus->read32(programCounter);

    programCounter = nextProgramCounter;
    nextProgramCounter += Hardware::INSTRUCTION_SIZE;

    lastWrittenRegister = 0xFFFFFFFF;

    execute(instruction);

    if (registerToUpdate != 0 && registerToUpdate != lastWrittenRegister && registerToUpdate != pendingLoadRegister) { 
        setRegister(registerToUpdate, valueToUpdate);
    }

    bus->tickHardware(1);
    instructionCount++; 

    // if (instructionCount % 3000000 == 0) {
    //     std::cout << "[Heartbeat] CPU is executing at PC: 0x" << std::hex << std::uppercase << programCounter << std::endl;
    // }
}


bool CPU::isInstructionCacheIsolated() const {
    uint32_t statusRegister = coprocessor0Registers[12];
    // Sony's custom CPU isolates the I-Cache using ONLY Bit 16
    return (statusRegister & 0x00010000) != 0;
}


void CPU::execute(uint32_t instruction) {
    // Isolate the 6-bit opcode
    uint32_t opcode = instruction >> 26;

    switch (opcode) {
        case 0x00: { // R-Types
            uint32_t registerFirstSource = (instruction >> 21) & 0x1F;
            uint32_t registerSecondSource = (instruction >> 16) & 0x1F;
            uint32_t registerTarget = (instruction >> 11) & 0x1F;
            uint32_t shiftAmount = (instruction >> 6) & 0x1F;
            uint32_t function = instruction & 0x3F;

            switch(function){
                case 0x00: { // SLL (Shift Left Logical)
                    setRegister(registerTarget, getRegister(registerSecondSource) << shiftAmount);
                    break; 
                }
                case 0x02: { // SRL (Shift Right Logical Zero-Extended)
                    setRegister(registerTarget, getRegister(registerSecondSource) >> shiftAmount);
                    break;
                }
                case 0x03: { // SRA (Arithmetic Shift Right Sign-Extended)
                    int32_t value = static_cast<int32_t>(getRegister(registerSecondSource));
    
                    setRegister(registerTarget, value >> shiftAmount);
                    break;
                }
                case 0x04: { // SLLV (Shift Left Logical Variable)
                    // The shift amount is stored in the bottom 5 bits of registerFirstSource
                    uint32_t shiftAmountVariable = getRegister(registerFirstSource) & 0x1F;
                    
                    setRegister(registerTarget, getRegister(registerSecondSource) << shiftAmountVariable);
                    break;
                }
                case 0x06: { // SRLV (Shift Right Logical Variable)
                    // Get the dynamic shift amount from the bottom 5 bits of registerFirstSource
                    uint32_t shiftAmountVariable = getRegister(registerFirstSource) & 0x1F;
                    
                    setRegister(registerTarget, getRegister(registerSecondSource) >> shiftAmountVariable);
                    break;
                }
                case 0x07: { // SRAV (Shift Right Arithmetic Variable)
                    // Get the dynamic shift amount from the bottom 5 bits of registerFirstSource
                    uint32_t shiftAmountVariable = getRegister(registerFirstSource) & 0x1F;
                    
                    int32_t value = static_cast<int32_t>(getRegister(registerSecondSource));
                    
                    setRegister(registerTarget, value >> shiftAmountVariable);
                    break;
                }
                case 0x08: { // JR (Jump to Address in Register)
                    nextIsDelaySlot = true;
                    nextProgramCounter = getRegister(registerFirstSource);

                    break;
                }
                case 0x09: { // JALR (Jump and Link Register)
                    nextIsDelaySlot = true;
                    uint32_t target = getRegister(registerFirstSource);
                    
                    // Fix: Save PC + 8 (instruction after the delay slot)
                    setRegister(registerTarget, programCounter + Hardware::INSTRUCTION_SIZE);
                    
                    nextProgramCounter = target;
                    break;
                }
                case 0x0C: { // SYSCALL (System Call)
                    // 0x08 is the standard MIPS hardware cause code for a Syscall
                    triggerException(0x08); 
                    break;
                }
                case 0x0D: { // BREAK (Breakpoint)
                    // 0x09 is the MIPS hardware cause code for a Breakpoint Exception (Bp)
                    triggerException(0x09); 
                    break;
                }
                case 0x10: { // MFHI (Move From HI Register)
                    setRegister(registerTarget, hi);
                    break;
                }
                case 0x11: { // MTHI (Move To HI Register)
                    hi = getRegister(registerFirstSource);
                    break;
                }
                case 0x12: { // MFLO (Move from LO Register)
                    setRegister(registerTarget, lo);
                    break;
                }
                case 0x13: { // MTLO (Move To LO Register)
                    lo = getRegister(registerFirstSource);
                    break;
                }
                case 0x18: { // MULT (Multiply Signed)
                    int64_t value1 = static_cast<int64_t>(static_cast<int32_t>(getRegister(registerFirstSource)));
                    int64_t value2 = static_cast<int64_t>(static_cast<int32_t>(getRegister(registerSecondSource)));
                    
                    int64_t result = value1 * value2;
                    
                    // Split the 64-bit signed result into two 32-bit chunks
                    lo = static_cast<uint32_t>(result & 0xFFFFFFFF);
                    hi = static_cast<uint32_t>((result >> 32) & 0xFFFFFFFF);
                    break;
                }
                case 0x19: { // MULTU (Multiply Unsigned)
                    uint64_t value1 = static_cast<uint64_t>(getRegister(registerFirstSource));
                    uint64_t value2 = static_cast<uint64_t>(getRegister(registerSecondSource));
                    
                    uint64_t result = value1 * value2;
                    
                    // Split the 64-bit result into two 32-bit chunks
                    lo = static_cast<uint32_t>(result & 0xFFFFFFFF);
                    hi = static_cast<uint32_t>(result >> 32);
                    break;
                }
                case 0x1A: { // DIV (Divide Signed)
                    int32_t numerator = static_cast<int32_t>(getRegister(registerFirstSource));
                    int32_t denominator = static_cast<int32_t>(getRegister(registerSecondSource));

                    if (denominator == 0) {
                        hi = static_cast<uint32_t>(numerator);
                        // Emulate PS1 silicon divide-by-zero behavior
                        if (numerator >= 0) {
                            lo = 0xFFFFFFFF; // -1
                        } else {
                            lo = 1;
                        }
                    } else if (static_cast<uint32_t>(numerator) == 0x80000000 && denominator == -1) {
                        // Prevent x86 host crash on INT_MIN / -1
                        hi = 0;
                        lo = 0x80000000;
                    } else {
                        hi = static_cast<uint32_t>(numerator % denominator);
                        lo = static_cast<uint32_t>(numerator / denominator);
                    }
                    break;
                }
                case 0x1B: { // DIV (Divide Unsigned)
                    uint32_t numerator = getRegister(registerFirstSource);
                    uint32_t denominator = getRegister(registerSecondSource);

                    if (denominator == 0) {
                        hi = numerator;
                        lo = 0xFFFFFFFF; 
                    } else {
                        hi = numerator % denominator;
                        lo = numerator / denominator;
                    }
                    break;
                }
                case 0x20: { // ADD (Add)
                    uint32_t first = getRegister(registerFirstSource);
                    uint32_t second = getRegister(registerSecondSource);
                    uint32_t result = first + second;

                    // Check if both operands have the same sign, but the result has a different sign (Bit 31)
                    if (~(first ^ second) & (first ^ result) & 0x80000000) {
                        triggerException(0x0C); // 0x0C is the MIPS Cause Code for Arithmetic Overflow
                    } 
                    else {
                        setRegister(registerTarget, result);
                    }
                    break;
                }
                case 0x21: { // ADDU (Add Unsigned)
                    setRegister(registerTarget, getRegister(registerFirstSource) + getRegister(registerSecondSource));
                    break;
                }
                case 0x22: { // SUB (Subtract Signed)
                    uint32_t first = getRegister(registerFirstSource);
                    uint32_t second = getRegister(registerSecondSource);
                    uint32_t result = first - second;

                    // Check if operands have different signs, AND the result sign doesn't match the first operand
                    if ((first ^ second) & (first ^ result) & 0x80000000) {
                        triggerException(0x0C); // MIPS Cause Code 0x0C for Arithmetic Overflow
                    }
                    else {
                        setRegister(registerTarget, result);
                    }
                    break;
                }
                case 0x23: { // SUBU (Subtract Unsigned)
                    setRegister(registerTarget, getRegister(registerFirstSource) - getRegister(registerSecondSource));
                    break;
                }
                case 0x24: { // AND (Bitwise AND)
                    setRegister(registerTarget, getRegister(registerFirstSource) & getRegister(registerSecondSource));
                    break;
                }
                case 0x25: { // OR (Bitwise OR)
                    setRegister(registerTarget, getRegister(registerFirstSource) | getRegister(registerSecondSource));
                    break;
                }
                case 0x26: { // XOR (Bitwise XOR)
                    setRegister(registerTarget, getRegister(registerFirstSource) ^ getRegister(registerSecondSource));
                    break;
                }
                case 0x27: { // NOR (Bitwise NOR)
                    setRegister(registerTarget, ~(getRegister(registerFirstSource) | getRegister(registerSecondSource)));
                    break;
                }
                case 0x2A: { // SLT (Set to 1 if Less Than Signed)
                    int32_t signedFirstSource = static_cast<int32_t>(getRegister(registerFirstSource));
                    int32_t signedSecondSource = static_cast<int32_t>(getRegister(registerSecondSource));

                    if (signedFirstSource < signedSecondSource) {
                        setRegister(registerTarget, 1);
                    } else {
                        setRegister(registerTarget, 0);
                    }
                    break;
                }
                case 0x2B: { // SLTU (Set to 1 if Less Than Unsigned)
                    setRegister(registerTarget, getRegister(registerFirstSource) < getRegister(registerSecondSource));
                    break;
                }
                default: {
                    std::cout << "Unimplemented R-Type function: 0x" << std::hex << function << " at PC: 0x" << (programCounter - Hardware::INSTRUCTION_SIZE) << std::endl;
                    std::cout << "\nTotal Instructions Executed: " << std::dec << instructionCount << std::endl; // Temporary tracking
                    exit(1);
                }
            }
            break;
        }
        case 0x01: { // REGIMM (Branch operations using a single register)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t subOpcode = (instruction >> 16) & 0x1F;
            
            // Extract and sign-extend the 16-bit immediate
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            // Shift left by 2 to align to 32-bit instruction boundaries
            uint32_t offset = immediate << 2;

            switch (subOpcode) {
                case 0x00: { // BLTZ (Branch if Less Than Zero)
                    nextIsDelaySlot = true;
                    if (static_cast<int32_t>(getRegister(registerSource)) < 0) {
                        nextProgramCounter = programCounter + offset;
                    }
                    break;
                }
                case 0x01: { // BGEZ (Branch if Greater Than or Equal to Zero)
                    nextIsDelaySlot = true;
                    if (static_cast<int32_t>(getRegister(registerSource)) >= 0) {
                        nextProgramCounter = programCounter + offset;
                    }
                    break;
                }
                case 0x10: { // BLTZAL (Branch if Less Than Zero And Link)
                    nextIsDelaySlot = true;
                    setRegister(31, programCounter + Hardware::INSTRUCTION_SIZE); 
                    
                    if (static_cast<int32_t>(getRegister(registerSource)) < 0) {
                        nextProgramCounter = programCounter + offset;
                    }
                    break;
                }
                case 0x11: { // BGEZAL (Branch if Greater Than or Equal to Zero And Link)
                    nextIsDelaySlot = true;
                    setRegister(31, programCounter + Hardware::INSTRUCTION_SIZE); 
                    
                    if (static_cast<int32_t>(getRegister(registerSource)) >= 0) {
                        nextProgramCounter = programCounter + offset;
                    }
                    break;
                }
                default: {
                    triggerException(0x0A);
                    exit(1);
                }
            }
            break;
        }
        case 0x02: { // J (Jump to Address)
            nextIsDelaySlot = true;
            uint32_t target = (instruction & 0x3FFFFFF) << 2;

            uint32_t programCounterRegion = programCounter & 0xF0000000;
            nextProgramCounter = programCounterRegion | target;
            
            break;
        }
        case 0x03: { // JAL (Jump and Link)
            nextIsDelaySlot = true;
            uint32_t target = (instruction & 0x3FFFFFF) << 2;

            // Fix: Save PC + 8 (instruction after the delay slot)
            setRegister(31, programCounter + Hardware::INSTRUCTION_SIZE); 

            uint32_t programCounterRegion = programCounter & 0xF0000000;
            nextProgramCounter = programCounterRegion | target;
            break;
        }
        case 0x04: { // BEQ (Branch if Equal)
            nextIsDelaySlot = true;
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            if(getRegister(registerSource) == getRegister(registerTarget)){
                nextProgramCounter = programCounter + (immediate << 2);
            }

            break;
        }
        case 0x05: { // BNE (Branch if Not Equal)
            nextIsDelaySlot = true;
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            if(getRegister(registerSource) != getRegister(registerTarget)){
                nextProgramCounter = programCounter + (immediate << 2);
            }

            break;
        }
        case 0x06: { // BLEZ (Branch if Less Than or Equal to Zero)
            nextIsDelaySlot = true;
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            if((int32_t)getRegister(registerSource) <= 0){
                nextProgramCounter = programCounter + (immediate << 2);
            }

            break;

        }
        case 0x07: { // BGTZ (Branch on Greater Than Zero)
            nextIsDelaySlot = true;
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            if((int32_t)getRegister(registerSource) > 0){
                nextProgramCounter = programCounter + (immediate << 2);
            }

            break;
        }
        case 0x08: { // ADDI (Add Immediate) // TODO: Implement signed overflow exception (Coprocessor 0)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            setRegister(registerTarget, getRegister(registerSource) + immediate);
            break;
        }
        case 0x09: { // ADDIU (Add Unsigned Immediate)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            setRegister(registerTarget, getRegister(registerSource) + immediate);

            break;
        }
        case 0x0A: { // SLTI (Set to 1 if Less Than Immediate)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
    
            int32_t signedSource = static_cast<int32_t>(getRegister(registerSource));

            if (signedSource < immediate) {
                setRegister(registerTarget, 1);
            } 
            else {
                setRegister(registerTarget, 0);
            }
            
            break;
        }
        case 0x0B: { // SLTIU (Set to 1 if Less Than Unsigned Immediate)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            if (getRegister(registerSource) < static_cast<uint32_t>(immediate)) {
                setRegister(registerTarget, 1);
            } 
            else {
                setRegister(registerTarget, 0);
            }
            
            break;
        }
        case 0x0C: { // ANDI (Bitwise AND Immediate)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            uint32_t immediate = instruction & 0xFFFF;

            setRegister(registerTarget, getRegister(registerSource) & immediate);
            break;
        }
        case 0x0D: { // ORI (Bitwise OR Immediate)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            uint32_t immediate = instruction & 0xFFFF;

            setRegister(registerTarget, getRegister(registerSource) | immediate);
            break;
        }
        case 0x0E: { // XORI (Exclusive OR Immediate)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            
            uint32_t immediate = instruction & 0xFFFF; 
            
            setRegister(registerTarget, getRegister(registerSource) ^ immediate);
            break;
        }
        case 0x0F: { // LUI (Load Upper Immediate)
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            uint32_t immediate = instruction & 0xFFFF;
            
            setRegister(registerTarget, immediate << 16);
            break;
        }
        case 0x10: { // Coprocessor 0 
            uint32_t coprocessorOopcode = (instruction >> 21) & 0x1F;

            switch (coprocessorOopcode) {
                case 0x00: { // MFC0 (Move From Coprocessor 0)
                    uint32_t registerTarget = (instruction >> 16) & 0x1F;
                    uint32_t coprocessor0Register = (instruction >> 11) & 0x1F;
                    
                    setRegister(registerTarget, coprocessor0Registers[coprocessor0Register]);
                    break;
                }
                case 0x04: { // MTC0 (Move To Coprocessor 0)
                    uint32_t cpuRegisterSource = (instruction >> 16) & 0x1F;
                    uint32_t coprocessor0RegisterTarget = (instruction >> 11) & 0x1F;

                    coprocessor0Registers[coprocessor0RegisterTarget] = getRegister(cpuRegisterSource);
                    break;
                }
                case 0x10: { // RFE (Return From Exception)
                    uint32_t statusRegister = coprocessor0Registers[12];
                    
                    uint32_t mode = statusRegister & 0x0000003F;
                    statusRegister = (statusRegister & 0xFFFFFFF0) | (mode >> 2);
                    
                    coprocessor0Registers[12] = statusRegister;
                    break;
                }
                default: {
                    std::cout << "Unimplemented COP0 instruction: " << std::hex << coprocessorOopcode << " at PC: 0x" << std::hex << (programCounter - Hardware::INSTRUCTION_SIZE) << std::endl;
                    exit(1);
                }
            }
            break;
        }
        case 0x12: { // Coprocessor 2 (Geometry Transformation Engine)
            if (instruction & 0x02000000) { // Bit 25
                break; 
            }

            // Otherwise, it's a register move operation
            uint32_t coprocessor2Opcode = (instruction >> 21) & 0x1F;

            switch (coprocessor2Opcode) {
                case 0x00: { // MFC2 (Move From Coprocessor 2 Data Register)
                    uint32_t registerTarget = (instruction >> 16) & 0x1F;
                    uint32_t coprocessor2Register = (instruction >> 11) & 0x1F;
                    
                    setRegister(registerTarget, coprocessor2DataRegisters[coprocessor2Register]);
                    break;
                }
                case 0x02: { // CFC2 (Move From Coprocessor 2 Control Register)
                    uint32_t registerTarget = (instruction >> 16) & 0x1F;
                    uint32_t coprocessor2Register = (instruction >> 11) & 0x1F;
                    
                    setRegister(registerTarget, coprocessor2ControlRegisters[coprocessor2Register]);
                    break;
                }
                case 0x04: { // MTC2 (Move To Coprocessor 2 Data Register)
                    uint32_t cpuRegisterSource = (instruction >> 16) & 0x1F;
                    uint32_t coprocessor2RegisterTarget = (instruction >> 11) & 0x1F;

                    coprocessor2DataRegisters[coprocessor2RegisterTarget] = getRegister(cpuRegisterSource);
                    break;
                }
                case 0x06: { // CTC2 (Move To Coprocessor 2 Control Register)
                    uint32_t cpuRegisterSource = (instruction >> 16) & 0x1F;
                    uint32_t coprocessor2RegisterTarget = (instruction >> 11) & 0x1F;

                    coprocessor2ControlRegisters[coprocessor2RegisterTarget] = getRegister(cpuRegisterSource);
                    break;
                }
                default: {
                    std::cout << "Unimplemented COP2 instruction: 0x" << std::hex << coprocessor2Opcode << " at PC: 0x" << std::hex << (programCounter - Hardware::INSTRUCTION_SIZE) << std::endl;
                    exit(1);
                }
                    
            }
            break;
        }
        case 0x20: { // LB (Load Byte)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            uint8_t byte = bus->read8(getRegister(registerSource) + immediate);

            pendingLoadRegister = registerTarget;
            pendingLoadValue = static_cast<uint32_t>(static_cast<int8_t>(byte));
            break;
        }
        case 0x21: { // LH (Load Halfword Signed)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;

            if (address & 1) {
                triggerException(0x04); // AdEL: Address Error (Load)
                break;
            }

            uint16_t halfword = bus->read16(getRegister(registerSource) + immediate);
            int32_t signExtendedHalfword = static_cast<int32_t>(static_cast<int16_t>(halfword));

            pendingLoadRegister = registerTarget;
            pendingLoadValue = static_cast<uint32_t>(signExtendedHalfword);
            break;
        }
        case 0x22: { // LWL (Load Word Left)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;

            uint32_t alignedAddress = address & 0xFFFFFFFC;
            uint32_t alignedWord = bus->read32(alignedAddress);
            uint32_t shift = (address & 3) * 8; 

            uint32_t currentRegValue = getRegister(registerTarget);
            if (pendingLoadRegister == registerTarget) {
                currentRegValue = pendingLoadValue;
            }

            uint32_t mask = 0x00FFFFFF >> shift;
            uint32_t newValue = (currentRegValue & mask) | (alignedWord << (24 - shift));
            
            pendingLoadRegister = registerTarget;
            pendingLoadValue = newValue;
            break;
        }
        case 0x23: { // LW (Load Word)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;

            if (address & 3) {
                triggerException(0x04); // AdEL: Address Error (Load)
                break;
            }

            pendingLoadRegister = registerTarget;
            pendingLoadValue = bus->read32(getRegister(registerSource) + immediate);

            break;
        }
        case 0x24: { // LBU (Load Byte Unsigned)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            uint8_t byte = bus->read8(getRegister(registerSource) + immediate);

            pendingLoadRegister = registerTarget;
            pendingLoadValue = byte; 
            break;
        }
        case 0x25: { // LHU (Load Halfword Unsigned)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            uint32_t address = getRegister(registerSource) + immediate;

            if (address & 1) {
                triggerException(0x04); // AdEL: Address Error (Load)
                break;
            }

            uint16_t halfword = bus->read16(getRegister(registerSource) + immediate);

            pendingLoadRegister = registerTarget;
            pendingLoadValue = halfword;
            break;
        }
        case 0x26: { // LWR (Load Word Right)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;

            uint32_t alignedAddress = address & 0xFFFFFFFC;
            uint32_t alignedWord = bus->read32(alignedAddress);
            uint32_t shift = (address & 3) * 8; 

            uint32_t currentRegValue = getRegister(registerTarget);
            if (pendingLoadRegister == registerTarget) {
                currentRegValue = pendingLoadValue;
            }

            uint32_t mask = 0xFFFFFF00 << (24 - shift);
            uint32_t newValue = (currentRegValue & mask) | (alignedWord >> shift);
            
            pendingLoadRegister = registerTarget;
            pendingLoadValue = newValue;
            break;
        }
        case 0x28: { // SB (Store Byte)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            uint8_t byte = getRegister(registerTarget) & 0xFF;

            if (isInstructionCacheIsolated()) break;

            bus->write8(getRegister(registerSource) + immediate, byte);
            break;
        }
        case 0x29: { // SH (Store Halfword)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;

            if (address & 1) {
                triggerException(0x05); // AdES: Address Error (Store)
                break;
            }

            uint16_t halfword = getRegister(registerTarget) & 0xFFFF;

            if (isInstructionCacheIsolated()) break;

            bus->write16(getRegister(registerSource) + immediate, halfword);
            break;
        }
        case 0x2A: { // SWL (Store Word Left)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;

            uint32_t alignedAddress = address & 0xFFFFFFFC;
            uint32_t alignedWord = bus->read32(alignedAddress);
            uint32_t shift = (address & 3) * 8; 

            uint32_t mask = 0xFFFFFF00 << shift;
            uint32_t newValue = (alignedWord & mask) | (getRegister(registerTarget) >> (24 - shift));
            
            if (isInstructionCacheIsolated()) break;

            bus->write32(alignedAddress, newValue);
            break;
        }
        case 0x2B: { // SW (Store Word)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;

            if (address & 3) {
                triggerException(0x05); // AdES: Address Error (Store)
                break;
            }

            if (isInstructionCacheIsolated()) {
                uint32_t physicalAddress = address & 0x1FFFFFFF;
                uint32_t cacheIndex = (physicalAddress >> 2) & 0x000003FF;
                uint32_t cacheTag = physicalAddress & 0xFFFFF000;

                iCache[cacheIndex].data = getRegister(registerTarget);
                iCache[cacheIndex].tag = cacheTag;
                iCache[cacheIndex].valid = true;
                break;
            }

            if (isInstructionCacheIsolated()) break;

            bus->write32(address, getRegister(registerTarget));
            break;
        }
        case 0x2E: { // SWR (Store Word Right)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;
            
            uint32_t alignedAddress = address & 0xFFFFFFFC;
            uint32_t alignedWord = bus->read32(alignedAddress);
            uint32_t shift = (address & 0x00000003) * 8; 

            uint32_t mask = 0x00FFFFFF >> (24 - shift);
            uint32_t newValue = (alignedWord & mask) | (getRegister(registerTarget) << shift);
            
            if (isInstructionCacheIsolated()) break;

            bus->write32(alignedAddress, newValue);
            break;
        }
        case 0x32: { // LWC2 (Load Word to Coprocessor 2)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t coprocessor2RegisterTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;

            if (address & 3) {
                triggerException(0x04); // AdEL
                break;
            }

            coprocessor2DataRegisters[coprocessor2RegisterTarget] = bus->read32(address);
            break;
        }
        case 0x3A: { // SWC2 (Store Word from Coprocessor 2)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t coprocessor2RegisterSource = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;

            if (address & 3) {
                triggerException(0x05); // AdES
                break;
            }

            bus->write32(address, coprocessor2DataRegisters[coprocessor2RegisterSource]);
            break;
        }
        default: {  // The Safety Net Crash
            uint32_t opcode = instruction >> 26;
            bool isGarbage = true;

            if (opcode <= 0x13) {
                isGarbage = false;
            } 
            else if (opcode >= 0x20 && opcode <= 0x26) {
                isGarbage = false; 
            } 
            else if (opcode >= 0x28 && opcode <= 0x2E) {
                isGarbage = false; 
            }
            else if (opcode >= 0x30 && opcode <= 0x33) {
                isGarbage = false; 
            }
            else if (opcode >= 0x38 && opcode <= 0x3B) {
                isGarbage = false;
            }

            if (isGarbage) {
                std::cout << "\n---------------------------------------ERROR!---------------------------------------" << std::endl;
                std::cout << "CPU: Executed garbage memory!" << std::endl;
                std::cout << "Unknown hardware Opcode (0x" << std::hex << opcode << ") found in instruction 0x" << instruction << " at PC: 0x" << (programCounter - Hardware::INSTRUCTION_SIZE) << std::endl;
                std::cout << "------------------------------------------------------------------------------------" << std::endl;
            } 
            else {
                std::cout << "Unimplemented valid instruction: 0x" << std::hex << instruction << " (Opcode 0x" << opcode << ") at PC: 0x" << (programCounter - Hardware::INSTRUCTION_SIZE) << std::endl;
            }

            std::cout << "\nTotal Instructions Executed: " << std::dec << instructionCount << std::endl;
            exit(1); 
        }
    }
}



// Register 0 is hardwired to 0 in physical silicon
void CPU::setRegister(uint32_t index, uint32_t value) {
    if (index == 0) return;

    registers[index] = value;
    lastWrittenRegister = index;
}


uint32_t CPU::getRegister(uint32_t index) const {
    if (index == 0) return 0;
    return registers[index];
}


void CPU::triggerException(uint32_t cause) {
    if (isDelaySlot) {
        // The exception happened in a delay slot. Save the branch's address.
        coprocessor0Registers[14] = currentProgramCounter - Hardware::INSTRUCTION_SIZE; 
        
        // Clear old cause (bits 2-6) and BD bit, set new cause, set BD bit
        coprocessor0Registers[13] = (coprocessor0Registers[13] & ~0x8000007C) | (cause << 2) | 0x80000000; 
    } 
    else {
        // Normal execution. Save the current instruction's address.
        coprocessor0Registers[14] = currentProgramCounter; 
        
        // Clear old cause and BD bit, set new cause
        coprocessor0Registers[13] = (coprocessor0Registers[13] & ~0x8000007C) | (cause << 2); 
    }

    // Clear pipeline state so the exception handler boots cleanly
    isDelaySlot = false;
    nextIsDelaySlot = false;

    // Shift the Status Register (COP0 Reg 12)
    uint32_t status = coprocessor0Registers[12];
    coprocessor0Registers[12] = (status & 0xFFFFFFC0) | ((status << 2) & 0x0000003F);

    // Hijack the Program Counter based on the BEV bit (Bit 22)
    if (status & 0x00400000) {
        programCounter = 0x1FC00180;
    } else {
        programCounter = 0x80000080;
    }
    
    nextProgramCounter = programCounter + Hardware::INSTRUCTION_SIZE;
}


void CPU::triggerHardwareInterrupt() {
    // Normal execution: Save the current instruction's address
    coprocessor0Registers[14] = programCounter; 
    
    // Cause code 0x00, and leave BD bit as 0
    coprocessor0Registers[13] = (0x00 << 2); 

    // Shift the Status Register (COP0 Reg 12)
    uint32_t status = coprocessor0Registers[12];
    coprocessor0Registers[12] = (status & 0xFFFFFFC0) | ((status << 2) & 0x0000003F);

    if (status & 0x00400000) {
        programCounter = 0x1FC00180;
    } 
    else {
        programCounter = 0x80000080;
    }

    nextProgramCounter = programCounter + Hardware::INSTRUCTION_SIZE;
}

