#include "CPU.h"
#include "Constants.h"
#include <iostream>

using namespace std;

// Initialize the CPU with the Bus and set the starting Program Counter to the BIOS
CPU::CPU(Bus* bus) : bus(bus) {
    programCounter = Hardware::BIOS_STARTING_ADDRESS;
    nextProgramCounter = programCounter + Hardware::INSTRUCTION_SIZE;
    
    for (int i = 0; i < 32; i++) {
        registers[i] = 0;
    }

    // Explicitly zero-out the instruction cache to prevent garbage execution
    for (int i = 0; i < 1024; i++) {
        iCache[i].tag = 0;
        iCache[i].data = 0;
        iCache[i].valid = false;
    }

    // Initialize COP0 Processor ID (PRid) to Sony's silicon version
    coprocessor0Registers[15] = 0x00000002;

    // Initialize COP0 Status Register (Set BEV bit to 1, and enable COP0)
    coprocessor0Registers[12] = 0x10400000;
}

// Fetch the 32-bit word, advance the clocks, and execute
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
            return; 
        }
    }

    uint32_t registerToUpdate = pendingLoadRegister;
    uint32_t valueToUpdate = pendingLoadValue;

    pendingLoadRegister = 0;
    pendingLoadValue = 0;

    currentProgramCounter = programCounter;

    uint32_t physicalProgramCounter = programCounter & 0x1FFFFFFF;
    uint32_t cacheIndex = (physicalProgramCounter >> 2) & 0x3FF; 
    uint32_t cacheTag = physicalProgramCounter & ~0xFFF;         
    
    // KSEG1 (0xA0000000 to 0xBFFFFFFF) bypasses the cache
    bool isUncachedRegion = (programCounter & 0xE0000000) == 0xA0000000;
    
    uint32_t instruction = bus->read32(programCounter);

    programCounter = nextProgramCounter;
    nextProgramCounter += 4;

    lastWrittenRegister = 0xFFFFFFFF;

    execute(instruction);

    if (registerToUpdate != 0 && registerToUpdate != lastWrittenRegister) { 
        setRegister(registerToUpdate, valueToUpdate);
    }

    bus->tickTimers(1);
    instructionCount++; 
}

bool CPU::isInstructionCacheIsolated() const {
    uint32_t statusRegister = coprocessor0Registers[12];
    // Sony's custom CPU isolates the I-Cache using ONLY Bit 16
    return (statusRegister & (1 << 16)) != 0;
}

void CPU::requestInterrupt() {
    interruptPending = true;
}

void CPU::triggerHardwareInterrupt() {
    // Normal execution: Save the current instruction's address
    coprocessor0Registers[14] = programCounter; 
    
    // Cause code 0x00, and leave BD bit as 0
    coprocessor0Registers[13] = (0x00 << 2); 

    // Shift the Status Register (COP0 Reg 12)
    uint32_t status = coprocessor0Registers[12];
    coprocessor0Registers[12] = (status & ~0x3F) | ((status << 2) & 0x3F);

    if (status & (1 << 22)) {
        programCounter = 0x1FC00180;
    } else {
        programCounter = 0x80000080;
    }

    nextProgramCounter = programCounter + 4;
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
                    setRegister(registerTarget, programCounter + 4);
                    
                    nextProgramCounter = target;
                    break;
                }
                case 0x0C: { // SYSCALL (System Call)
                    // 0x08 is the standard MIPS hardware cause code for a Syscall
                    triggerException(0x08); 
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
                    setRegister(registerTarget, getRegister(registerFirstSource) + getRegister(registerSecondSource));
                    break;
                }
                case 0x21: { // ADDU (Add Unsigned)
                    setRegister(registerTarget, getRegister(registerFirstSource) + getRegister(registerSecondSource));
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
                    cout << "Unimplemented R-Type function: 0x" << hex << function << " at PC: 0x" << (programCounter - 4) << endl;
                    cout << "\nTotal Instructions Executed: " << dec << instructionCount << endl; // Temporary tracking
                    exit(1);
                }
            }
            break;
        }
        case 0x01: { // REGIMM (Branch operations using a single register)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t subOpcode = (instruction >> 16) & 0x1F; //
            
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
                default: {
                    cout << "Unimplemented REGIMM sub-opcode: 0x" << hex << subOpcode << " at PC: 0x" << (programCounter - 4) << endl;
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
            setRegister(31, programCounter + 4); 

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
            uint32_t immediate = instruction & 0xFFFF; // Autofills left with 0s 

            setRegister(registerTarget, getRegister(registerSource) & immediate);
            break;
        }
        case 0x0D: { // ORI (Bitwise OR Immediate)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            uint32_t immediate = instruction & 0xFFFF; // Autofills left with 0s 

            setRegister(registerTarget, getRegister(registerSource) | immediate);
            break;
        }
        case 0x0F: { // LUI (Load Upper Immediate)
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            uint32_t immediate = instruction & 0xFFFF;
            
            // LUI shifts the immediate value into the upper 16 bits of the register
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

                    // Copy data from the standard CPU register to the COP0 register
                    coprocessor0Registers[coprocessor0RegisterTarget] = getRegister(cpuRegisterSource);
                    break;
                }
                case 0x10: { // RFE (Return From Exception)
                    // The Status Register is COP0 Register 12
                    uint32_t statusRegister = coprocessor0Registers[12];
                    
                    // RFE shifts the interrupt bits (bits 2-5) back to the right by 2
                    uint32_t mode = statusRegister & 0x3F;
                    statusRegister = (statusRegister & ~0xF) | (mode >> 2);
                    
                    coprocessor0Registers[12] = statusRegister;
                    break;
                }
                default:
                    cout << "Unimplemented COP0 instruction: " << hex << coprocessorOopcode 
                         << " at PC: 0x" << hex << (programCounter - 4) << endl;
                    exit(1);
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

            uint32_t alignedAddress = address & ~3;
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

            uint32_t alignedAddress = address & ~3;
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

            uint32_t alignedAddress = address & ~3;
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

            // Only hijack the write if the BIOS specifically targets the I-Cache
            if (isInstructionCacheIsolated()) {
                uint32_t physicalAddress = address & 0x1FFFFFFF;
                uint32_t cacheIndex = (physicalAddress >> 2) & 0x3FF;
                uint32_t cacheTag = physicalAddress & ~0xFFF;

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
            
            uint32_t alignedAddress = address & ~3;
            uint32_t alignedWord = bus->read32(alignedAddress);
            uint32_t shift = (address & 3) * 8; 

            uint32_t mask = 0x00FFFFFF >> (24 - shift);
            uint32_t newValue = (alignedWord & mask) | (getRegister(registerTarget) << shift);
            
            if (isInstructionCacheIsolated()) break;

            bus->write32(alignedAddress, newValue);
            break;
        }
        default: {  // The Safety Net Crash
            uint32_t opcode = instruction >> 26;
            bool isGarbage = true;

            // WHITELIST: These are the only valid primary opcodes on the PS1
            if (opcode <= 0x13) {
                isGarbage = false; // 0x00-0x13: ALU, Branches, Jumps, and Coprocessor operations
            } 
            else if (opcode >= 0x20 && opcode <= 0x26) {
                isGarbage = false; // 0x20-0x26: Memory Loads (LB, LH, LW, etc.)
            } 
            else if (opcode >= 0x28 && opcode <= 0x2E) {
                isGarbage = false; // 0x28-0x2E: Memory Stores (SB, SH, SW, etc.)
            }
            else if (opcode >= 0x30 && opcode <= 0x33) {
                isGarbage = false; // 0x30-0x33: Coprocessor Loads
            }
            else if (opcode >= 0x38 && opcode <= 0x3B) {
                isGarbage = false; // 0x38-0x3B: Coprocessor Stores
            }

            if (isGarbage) {
                cout << "\n---------------------------------------ERROR!---------------------------------------" << endl;
                cout << "CPU: Executed garbage memory!" << endl;
                cout << "Unknown hardware Opcode (0x" << hex << opcode << ") found in instruction 0x" << instruction << " at PC: 0x" << (programCounter - 4) << endl;
                cout << "------------------------------------------------------------------------------------" << endl;
            } 
            else {
                cout << "Unimplemented valid instruction: 0x" << hex << instruction << " (Opcode 0x" << opcode << ") at PC: 0x" << (programCounter - 4) << endl;
            }

            cout << "\nTotal Instructions Executed: " << dec << instructionCount << endl;
            exit(1); 
        }
    }
}


void CPU::triggerException(uint32_t cause) {
    if (isDelaySlot) {
        // The exception happened in a delay slot. Save the branch's address.
        coprocessor0Registers[14] = currentProgramCounter - 4; 
        
        // Set the Cause code and flag the BD (Branch Delay) bit (Bit 31)
        coprocessor0Registers[13] = (cause << 2) | (1 << 31); 
    } else {
        // Normal execution. Save the current instruction's address.
        coprocessor0Registers[14] = currentProgramCounter; 
        
        // Set the Cause code, leave BD bit as 0
        coprocessor0Registers[13] = (cause << 2); 
    }

    // Clear pipeline state so the exception handler boots cleanly
    isDelaySlot = false;
    nextIsDelaySlot = false;

    // Shift the Status Register (COP0 Reg 12)
    uint32_t status = coprocessor0Registers[12];
    coprocessor0Registers[12] = (status & ~0x3F) | ((status << 2) & 0x3F);

    // Hijack the Program Counter based on the BEV bit (Bit 22)
    if (status & (1 << 22)) {
        programCounter = 0x1FC00180;
    } else {
        programCounter = 0x80000080;
    }
    
    nextProgramCounter = programCounter + 4;
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


