#include <iostream>
#include "CPU.h"
#include "../core/Constants.h"


CPU::CPU(Bus* bus) : bus(bus) {
    programCounter = Hardware::BIOS_STARTING_ADDRESS;
    nextProgramCounter = programCounter + Hardware::INSTRUCTION_SIZE;
    
    // Initialize COP0 Processor ID (PRid) to Sony's silicon version
    coprocessor0Registers[15] = 0x00000002;

    // Initialize COP0 Status Register (Set BEV bit to 1, and enable COP0)
    coprocessor0Registers[12] = 0x10400000;
}


void CPU::step() {
    bool globalInterruptsEnabled = (coprocessor0Registers[12] & 0x1) != 0;

    /// @todo (bug): CAUSE.IP2 (bit 10) should continuously mirror (I_STAT & I_MASK). Also, software interrupts (CAUSE bits 8-9 with SR.IM0/IM1) never raise an exception here.

    bool hardwareInterruptsUnmasked = (coprocessor0Registers[12] & 0x400) != 0;

    isDelaySlot = nextIsDelaySlot;
    nextIsDelaySlot = false;

    /// @todo (bug): exception priority is not modelled. psx-spx order: data AdEL/AdES/DBE > Ovf > Int > Sys/Bp/RI/CpU > fetch errors. The IRQ is checked before the instruction runs, so it always wins.

    if (bus->hasPendingInterrupts() && globalInterruptsEnabled && hardwareInterruptsUnmasked) {
        /// @todo (bug): IRQs are never taken inside a branch delay slot. psx-spx says they are: CAUSE.BD=1 and EPC points back at the branch, which re-executes on return (this is also why JALR with rs==rd is dangerous).

        if (!isDelaySlot) {
            uint32_t fetchAddress = programCounter & 0x1FFFFFFF;
            bool peekable = fetchAddress < 0x00800000 || (fetchAddress >= Hardware::BIOS_ROM_START && fetchAddress < Hardware::BIOS_ROM_END);
            
            /// @todo psx-spx: an IRQ on a GTE command is taken, the command still executes, and EPC points AT the GTE command (the BIOS handler then skips it with epc+4). Here the IRQ is deferred past the command instead, so EPC never points at it.
            
            bool nextIsGteCommand = peekable && ((bus->read32(programCounter) >> 25) == 0x25);


            if (!nextIsGteCommand) {
                // Flush any pending load from the previous instruction before jumping
                if (pendingLoadRegister != 0) {
                    setRegister(pendingLoadRegister, pendingLoadValue);
                    pendingLoadRegister = 0;
                    pendingLoadValue = 0;
                }

                triggerHardwareInterrupt();
                bus->advanceClock(1);
                return; 
            }
        }
    }

    uint32_t registerToUpdate = pendingLoadRegister;
    uint32_t valueToUpdate = pendingLoadValue;

    inFlightLoadRegister = registerToUpdate;
    inFlightLoadValue = valueToUpdate;

    pendingLoadRegister = 0;
    pendingLoadValue = 0;

    currentProgramCounter = programCounter;

    /// @todo (bug): if (programCounter & 3), raise AdEL (ExcCode 0x04) with EPC = the bad address. JR/JALR to an unaligned target is never checked (psx-spx: AdEL on the next fetch).
    
    // Bus Error on Instruction fetch (ExcCode 6). Hardware-verified by ps1-tests cpu/code-in-io for exactly these three places:
    // scratchpad, the interrupt registers and MDEC. Fetching from the SPU registers, DMA0 and the DMA control register does NOT
    // fault on real hardware (the registers read back whatever was written and the CPU just executes it).
    /// @todo the test only samples a few ports, so the rule for the other I/O registers (timers, GPU, CD-ROM, SIO) is unknown.
    uint32_t fetchAddress = programCounter & 0x1FFFFFFF;
    bool busErrorOnFetch = (fetchAddress >= 0x1F800000 && fetchAddress < 0x1F800400)   // scratchpad
                        || (fetchAddress >= 0x1F801070 && fetchAddress < 0x1F801078)   // I_STAT / I_MASK
                        || (fetchAddress >= 0x1F801820 && fetchAddress < 0x1F801828);  // MDEC
    if (busErrorOnFetch) {
        triggerException(0x06); // IBE
        if (registerToUpdate != 0 && registerToUpdate != pendingLoadRegister) {
            setRegister(registerToUpdate, valueToUpdate); // the load that was in flight still completes
        }
        bus->advanceClock(1);
        return;
    }

    /// @todo (bug): instruction fetch bypasses iCache entirely. psx-spx (memorymap.md): the i-cache is direct-mapped, 256 lines x 4 words indexed by address bits [11:4], tag = physical address[31:12] | per-word valid[3:0], active only for KUSEG/KSEG0 (KSEG1 bypasses). Stores to RAM never touch it, so stale code keeps executing until an explicit flush (Formula One 2001 relies on this). A miss fills from the accessed word to the end of the line (IBLKSZ=0 limits a miss at word 0 to 2 words); a tag match with a clear valid bit refills the whole line. DMA writes (DMA.cpp goes through Bus::write32) must not invalidate it either, so keep the cache inside the CPU rather than hooking Bus::write32.

    uint32_t instruction = bus->read32(programCounter);

    // Extra cycles to simulate time it would take to read from ROM
    if (fetchAddress >= Hardware::BIOS_ROM_START && fetchAddress < Hardware::BIOS_ROM_END) {
        bus->advanceClock(Hardware::BIOS_ROM_FETCH_CYCLES - 1);
    }

    programCounter = nextProgramCounter;
    nextProgramCounter += Hardware::INSTRUCTION_SIZE;

    lastWrittenRegister = 0xFFFFFFFF;

    execute(instruction);

    if (registerToUpdate != 0 && registerToUpdate != lastWrittenRegister && registerToUpdate != pendingLoadRegister) { 
        setRegister(registerToUpdate, valueToUpdate);
    }

    bus->advanceClock(1);
}


uint32_t CPU::getRegister(uint32_t index) const {
    if (index == 0) return 0;
    return registers[index];
}


void CPU::setRegister(uint32_t index, uint32_t value) {
    if (index == 0) return;
    registers[index] = value;
    lastWrittenRegister = index;
}


void CPU::jumpTo(uint32_t address) {
    programCounter = address;
    nextProgramCounter = address + Hardware::INSTRUCTION_SIZE;
    pendingLoadRegister = 0;
    pendingLoadValue = 0;
    nextIsDelaySlot = false;
    isDelaySlot = false;
}



//Privates
bool CPU::isInstructionCacheIsolated() const {
    /// @todo (bug): the isolated-cache model is partial. psx-spx: with SR.IsC=1 ALL loads and stores target the cache (loads here still read the bus; SB/SH/SWL/SWR are dropped). BCC (FFFE0130h) picks the target: TAG+IS1 writes the tag, tag = (data & 0Fh) | (write offset & FFFFF000h), so the low 4 data bits are the valid bits (this is how the BIOS flushes); IS1 alone writes the code words. SW here always writes data+tag+valid from the address. TAG-mode loads return valid bits [3:0] plus a tag-match bit 4; SR.CM (bit 19) is never set.

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
                    /// @todo (timing): stall until the pending mul/div finishes (not implemented).

                    setRegister(registerTarget, hi);
                    break;
                }
                case 0x11: { // MTHI (Move To HI Register)
                    hi = getRegister(registerFirstSource);
                    break;
                }
                case 0x12: { // MFLO (Move from LO Register)
                    /// @todo (timing): stall until the pending mul/div finishes (not implemented).

                    setRegister(registerTarget, lo);
                    break;
                }
                case 0x13: { // MTLO (Move To LO Register)
                    lo = getRegister(registerFirstSource);
                    break;
                }
                case 0x18: { // MULT (Multiply Signed)
                    /// @todo (timing): MULT/MULTU take 6/9/13 cycles by rs, DIV/DIVU 36 (see docs cpu.md section 10).
                    /// Record a ready-clock here; MFHI/MFLO and the next mul/div should stall until it.

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
                    /// @todo hardware raises RI (triggerException(0x0A)) for reserved opcodes; this prints and exit(1)s.
                    /// Switch reserved encodings to RI once missing-instruction debugging is no longer needed.

                    std::cout << "Unimplemented R-Type function: 0x" << std::hex << function << " at PC: 0x" << (programCounter - Hardware::INSTRUCTION_SIZE) << std::endl;
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

                    /// @todo (bug): if registerSource == 31, the comparison must use $ra's value from BEFORE linking (according to psx-spx). Read/compare first, then call setRegister(31, ...).

                    setRegister(31, programCounter + Hardware::INSTRUCTION_SIZE); 
                    
                    if (static_cast<int32_t>(getRegister(registerSource)) < 0) {
                        nextProgramCounter = programCounter + offset;
                    }
                    break;
                }
                case 0x11: { // BGEZAL (Branch if Greater Than or Equal to Zero And Link)
                    nextIsDelaySlot = true;

                    /// @todo (bug): if registerSource == 31, the comparison must use $ra's value from BEFORE linking (according to psx-spx). Read/compare first, then call setRegister(31, ...).

                    setRegister(31, programCounter + Hardware::INSTRUCTION_SIZE); 
                    
                    if (static_cast<int32_t>(getRegister(registerSource)) >= 0) {
                        nextProgramCounter = programCounter + offset;
                    }
                    break;
                }
                default: {
                    triggerException(0x0A);                
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
        case 0x08: { // ADDI (Add Immediate)
            /// @todo (bug): raise Ovf (triggerException(0x0C)) on signed overflow like ADD does. Currently wraps silently.
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
            /// @todo COP0 debug registers (r3 BPC, r5 BDA, r7 DCIC, r9 BDAM, r11 BPCM) are plain storage. psx-spx: breakpoint/trace matches set DCIC status bits and jump to 80000040h (ExcCode 09h), and DCIC has jump-redirect bits. None of that exists.
            /// @todo (bug): psx-spx says cop0r0-r2, r4 and r10 raise RI (ExcCode 0Ah) when read, and cop0r16-r31 return garbage WITHOUT an exception, even in user mode with CU0=0 (the CU0 check above would raise CpU for them).

            // Kernel mode (Status.KUc = bit 1 clear) can always use COP0; user mode needs Status.CU0
            if ((coprocessor0Registers[12] & 0x2) && !isCoprocessorEnabled(0)) {
                triggerCoprocessorUnusable(0);
                break;
            }

            uint32_t coprocessorOopcode = (instruction >> 21) & 0x1F;

            switch (coprocessorOopcode) {
                case 0x00: { // MFC0 (Move From Coprocessor 0)
                    /// @todo psx-spx says coprocessor reads have a 1-instruction load delay. 
                    /// This writes the register immediately; queue via pendingLoadRegister/pendingLoadValue like loads.

                    uint32_t registerTarget = (instruction >> 16) & 0x1F;
                    uint32_t coprocessor0Register = (instruction >> 11) & 0x1F;
                    
                    setRegister(registerTarget, coprocessor0Registers[coprocessor0Register]);
                    break;
                }
                case 0x04: { // MTC0 (Move To Coprocessor 0)
                    /// @todo CAUSE is read-only except bits 8-9, and EPC/PRID/BadVaddr/TAR (r6) are read-only. SR bits 6-7, 23-24 and 26-27 and DCIC (r7) bits 6-11 and 16-22 always read zero.
                    /// Apply per-register write masks instead of raw storage.

                    uint32_t cpuRegisterSource = (instruction >> 16) & 0x1F;
                    uint32_t coprocessor0RegisterTarget = (instruction >> 11) & 0x1F;

                    coprocessor0Registers[coprocessor0RegisterTarget] = getRegister(cpuRegisterSource);
                    break;
                }
                case 0x10: { // RFE (Return From Exception)
                    /// @todo (bug): dispatch is on rs only; psx-spx says only function field 10h is RFE. TLBR/TLBWI/TLBWR/TLBP (function 01h/02h/06h/08h) should raise RI (ExcCode 0Ah) but currently execute as RFE.

                    uint32_t statusRegister = coprocessor0Registers[12];
                    
                    uint32_t mode = statusRegister & 0x0000003F;
                    statusRegister = (statusRegister & 0xFFFFFFF0) | (mode >> 2);
                    
                    coprocessor0Registers[12] = statusRegister;
                    break;
                }
                default: {
                    /// @todo (bug): psx-spx says CFC0/CTC0 (rs=2/6) and BC0F/BC0T (rs=8) are unsupported and raise Coprocessor Unusable (ExcCode 0Bh). This prints and exit(1)s instead (fine while debugging, same reasoning as the R-type RI todo).

                    // Hardware-verified (ps1-tests cpu/cop): unknown COP0 "CO" commands (rs >= 0x10) do nothing
                    if (coprocessorOopcode >= 0x10) break;

                    std::cout << "Unimplemented COP0 instruction: " << std::hex << coprocessorOopcode << " at PC: 0x" << std::hex << (programCounter - Hardware::INSTRUCTION_SIZE) << std::endl;
                    exit(1);
                }
            }
            break;
        }
        case 0x11: { // Coprocessor 1 (doesn't exist on the PS1): no-op when enabled, CpU when disabled
            uint32_t coprocessorNumber = (instruction >> 26) & 0x3;
            if (!isCoprocessorEnabled(coprocessorNumber)) triggerCoprocessorUnusable(coprocessorNumber);
            break;
        }
        case 0x12: { // Coprocessor 2 (Geometry Transformation Engine)
            /// @todo psx-spx: setting SR.CU2 (bit 30) takes effect about 2 cycles late. The enable is instant here.
            if (!isCoprocessorEnabled(2)) { triggerCoprocessorUnusable(2); break; }
            if (instruction & 0x02000000) { // Bit 25
                gte.executeCommand(instruction);
                break;
            }

            // Otherwise, it's a register move operation
            uint32_t coprocessor2Opcode = (instruction >> 21) & 0x1F;

            switch (coprocessor2Opcode) {
                case 0x00: { // MFC2 (Move From Coprocessor 2 Data Register)
                    /// @todo psx-spx says coprocessor reads have a 1-instruction load delay. 
                    /// This writes the register immediately; queue via pendingLoadRegister/pendingLoadValue like loads.

                    uint32_t registerTarget = (instruction >> 16) & 0x1F;
                    uint32_t gteRegister = (instruction >> 11) & 0x1F;
                    setRegister(registerTarget, gte.readDataRegister(gteRegister));
                    break;
                }
                case 0x02: { // CFC2 (Move From Coprocessor 2 Control Register)
                    /// @todo psx-spx says coprocessor reads have a 1-instruction load delay. 
                    /// This writes the register immediately; queue via pendingLoadRegister/pendingLoadValue like loads.

                    uint32_t registerTarget = (instruction >> 16) & 0x1F;
                    uint32_t gteRegister = (instruction >> 11) & 0x1F;
                    setRegister(registerTarget, gte.readControlRegister(gteRegister));
                    break;
                }
                case 0x04: { // MTC2 (Move To Coprocessor 2 Data Register)
                    /// @todo psx-spx: writes to COP2 registers (MTC2/CTC2) take 2-3 cycles to land (3 for IRGB, which also updates IR1-3). Writes are instant here.

                    uint32_t cpuRegisterSource = (instruction >> 16) & 0x1F;
                    uint32_t gteRegisterTarget = (instruction >> 11) & 0x1F;
                    gte.writeDataRegister(gteRegisterTarget, getRegister(cpuRegisterSource));
                    break;
                }
                case 0x06: { // CTC2 (Move To Coprocessor 2 Control Register)
                    uint32_t cpuRegisterSource = (instruction >> 16) & 0x1F;
                    uint32_t gteRegisterTarget = (instruction >> 11) & 0x1F;
                    gte.writeControlRegister(gteRegisterTarget, getRegister(cpuRegisterSource));
                    break;
                }
                default: {
                    std::cout << "Unimplemented COP2 instruction: 0x" << std::hex << coprocessor2Opcode << " at PC: 0x" << std::hex << (programCounter - Hardware::INSTRUCTION_SIZE) << std::endl;
                    exit(1);
                }  
            }
            break;
        }
        case 0x13: { // Coprocessor 3 (doesn't exist on the PS1): no-op when enabled, CpU when disabled
            uint32_t coprocessorNumber = (instruction >> 26) & 0x3;
            if (!isCoprocessorEnabled(coprocessorNumber)) triggerCoprocessorUnusable(coprocessorNumber);
            break;
        }
        /// @todo (bug): data bus error (DBE, ExcCode 07h) is never raised, and no address errors for bad segments. psx-spx: unused regions (including gaps in the I/O region) cause a Bus Error (Bus.cpp currently exit(1)s there); in user mode, accesses outside KUSEG raise AdEL/AdES with BadVaddr set.
        case 0x20: { // LB (Load Byte)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            chargeLoad(getRegister(registerSource) + immediate);

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

            chargeLoad(getRegister(registerSource) + immediate);

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

            chargeLoad(alignedAddress);

            uint32_t alignedWord = bus->read32(alignedAddress);
            uint32_t shift = (address & 3) * 8; 

            uint32_t currentRegValue = getRegister(registerTarget);
            if (registerTarget != 0 && inFlightLoadRegister == registerTarget) {
                currentRegValue = inFlightLoadValue;
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

            chargeLoad(getRegister(registerSource) + immediate);

            pendingLoadRegister = registerTarget;
            pendingLoadValue = bus->read32(getRegister(registerSource) + immediate);

            break;
        }
        case 0x24: { // LBU (Load Byte Unsigned)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            chargeLoad(getRegister(registerSource) + immediate);

            uint8_t byte = bus->read8(getRegister(registerSource) + immediate);

            pendingLoadRegister = registerTarget;
            pendingLoadValue = byte; 
            break;
        }
        case 0x25: { // LHU (Load Halfword Unsigned)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            chargeLoad(getRegister(registerSource) + immediate);

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

            chargeLoad(alignedAddress);

            uint32_t alignedWord = bus->read32(alignedAddress);
            uint32_t shift = (address & 3) * 8; 

            uint32_t currentRegValue = getRegister(registerTarget);
            if (registerTarget != 0 && inFlightLoadRegister == registerTarget) {
                currentRegValue = inFlightLoadValue;
            }

            uint32_t mask = 0xFFFFFF00 << (24 - shift);
            uint32_t newValue = (currentRegValue & mask) | (alignedWord >> shift);
            
            pendingLoadRegister = registerTarget;
            pendingLoadValue = newValue;
            break;
        }
        case 0x28: { // SB (Store Byte)
            /// @todo (bug): for 8/16-bit stores to I/O registers, hardware puts all 32 bits of the GPR on the bus (psx-spx: soundscope's `sh` to DMA registers depends on it). 
            /// Only the low bits reach the bus here; needs a Bus write API that receives the full register value.

            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);

            uint8_t byte = getRegister(registerTarget) & 0xFF;

            if (isInstructionCacheIsolated()) break;

            bus->write8(getRegister(registerSource) + immediate, byte);
            break;
        }
        case 0x29: { // SH (Store Halfword)
            /// @todo (bug): for 8/16-bit stores to I/O registers, hardware puts all 32 bits of the GPR on the bus (psx-spx: soundscope's `sh` to DMA registers depends on it). 
            /// Only the low bits reach the bus here; needs a Bus write API that receives the full register value.

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
            /// @todo psx-spx: stores go through a 4-word write queue and the CPU stalls only when it is full. Stores always cost one cycle here. (Ordering effects on hardware registers: see Bus.cpp.)
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
        /// @todo (bug): psx-spx says LWC0/SWC0 are unsupported and ALWAYS raise Coprocessor Unusable (ExcCode 0Bh). Here CpU only fires when CU0 is clear. If this was hardware-verified, replace this todo with a "hardware-verified" note.
        case 0x30: case 0x31: case 0x33:   // LWC0/1/3
        case 0x38: case 0x39: case 0x3B: { // SWC0/1/3
            uint32_t coprocessorNumber = (instruction >> 26) & 0x3;
            if (!isCoprocessorEnabled(coprocessorNumber)) triggerCoprocessorUnusable(coprocessorNumber);
            break; // enabled: hardware raises nothing; the transfer itself isn't modelled
        }
        case 0x32: { // LWC2 (Load Word to Coprocessor 2)
            if (!isCoprocessorEnabled(2)) { triggerCoprocessorUnusable(2); break; }
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t coprocessor2RegisterTarget = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;

            if (address & 3) {
                triggerException(0x04); // AdEL
                break;
            }

            gte.writeDataRegister(coprocessor2RegisterTarget, bus->read32(address));
            break;
        }
        case 0x3A: { // SWC2 (Store Word from Coprocessor 2)
            if (!isCoprocessorEnabled(2)) { triggerCoprocessorUnusable(2); break; }
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t coprocessor2RegisterSource = (instruction >> 16) & 0x1F;
            int32_t immediate = signExtend16(instruction & 0xFFFF);
            uint32_t address = getRegister(registerSource) + immediate;

            if (address & 3) {
                triggerException(0x05); // AdES
                break;
            }

            
            bus->write32(address, gte.readDataRegister(coprocessor2RegisterSource));
            break;
        }
        default: {  // The Safety Net Crash
            /// @todo (bug): psx-spx says every N/A primary opcode raises RI (ExcCode 0Ah); this prints and exit(1)s. The isGarbage list also treats 2Ch/2Dh as "valid" although psx-spx marks them N/A. Switch to RI together with the R-type todo.

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
                std::cout << "FATAL: CPU executed garbage memory. Unknown opcode 0x" << std::hex << opcode << " in instruction 0x" << instruction << " at PC: 0x" << (programCounter - Hardware::INSTRUCTION_SIZE) << std::endl;
            } 
            else {
                std::cout << "Unimplemented valid instruction: 0x" << std::hex << instruction << " (Opcode 0x" << opcode << ") at PC: 0x" << (programCounter - Hardware::INSTRUCTION_SIZE) << std::endl;
            }

            exit(1); 
        }
    }
}


void CPU::triggerCoprocessorUnusable(uint32_t coprocessorNumber) {
    triggerException(0x0B); // CpU
    coprocessor0Registers[13] = (coprocessor0Registers[13] & ~0x30000000u) | (coprocessorNumber << 28); // Cause.CE (Coprocessor Exception field, bits 28-29: logs which COP failed)
}


void CPU::triggerException(uint32_t cause) {
    /// @todo BadVaddr (COP0 r8) must be set on AdEL/AdES (the faulting address). Never set today.
    /// @todo CAUSE.BT (bit 30) and TAR (cop0r6) are never written. psx-spx: for an exception in a delay slot, BT says whether the branch is taken and TAR holds its destination.

    if (isDelaySlot) {
        coprocessor0Registers[14] = currentProgramCounter - Hardware::INSTRUCTION_SIZE; 
        
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

    // Push the exception stack: Shift the KU/IE (Kernel/User and Interrupt Enable) bits left by 2
    uint32_t status = coprocessor0Registers[12];
    coprocessor0Registers[12] = (status & 0xFFFFFFC0) | ((status << 2) & 0x0000003F);

    if (status & 0x00400000) {
        programCounter = 0xBFC00180;
    } 
    else {
        programCounter = 0x80000080;
    }
    
    nextProgramCounter = programCounter + Hardware::INSTRUCTION_SIZE;

}


void CPU::triggerHardwareInterrupt() {
    // Save the current instruction's address to EPC
    coprocessor0Registers[14] = programCounter; 


    /// @todo (bug): IP2 is set here but nothing ever clears it; see the TODO at the top of step().

    
    // Preserve IP bits (8-15), clear ExcCode (2-6) and BD (31), and hard-assert IP2 (Bit 10)
    coprocessor0Registers[13] = (coprocessor0Registers[13] & 0x0000FF00) | (1 << 10); 

    // Push the exception stack: Shift the KU/IE (Kernel/User and Interrupt Enable) bits left by 2
    uint32_t status = coprocessor0Registers[12];
    coprocessor0Registers[12] = (status & 0xFFFFFFC0) | ((status << 2) & 0x0000003F);

    // Hijack the Program Counter based on the BEV bit (Bit 22)
    if (status & 0x00400000) {
        /// @todo vector addresses are inconsistent: this uses 0x1FC00180, triggerException uses 0xBFC00180, and reset uses 0x1FC00000 (constructor). psx-spx lists BFC00180h / BFC00000h. Harmless while the I-cache is not modelled.

        programCounter = 0x1FC00180;
    } 
    else {
        programCounter = 0x80000080;
    }

    nextProgramCounter = programCounter + Hardware::INSTRUCTION_SIZE;
}


void CPU::chargeLoad(uint32_t address) {
    /// @todo psx-spx gives Main RAM a 6-cycle access time; 7 extra cycles is an empirical value for RE2 and not derived from that. Revisit once timing is modelled properly.

    // Empirical: 7 was the smallest value that made RE2's second movie start. Not a measured RAM latency.
    constexpr int RAM_LOAD_EXTRA_CYCLES = 7;
    if ((address & 0x1FFFFFFF) < 0x00800000) bus->advanceClock(RAM_LOAD_EXTRA_CYCLES); // RAM and mirrors only; scratchpad and I/O untouched
}
