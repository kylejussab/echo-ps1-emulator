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
}

// Fetch the 32-bit word, advance the clocks, and execute
void CPU::step() {
    uint32_t registerToUpdate = pendingLoadRegister;
    uint32_t valueToUpdate = pendingLoadValue;

    pendingLoadRegister = 0;
    pendingLoadValue = 0;

    uint32_t instruction = bus->read32(programCounter);
    
    programCounter = nextProgramCounter;
    nextProgramCounter += 4;
    
    execute(instruction);

    if (registerToUpdate != 0) { 
        setRegister(registerToUpdate, valueToUpdate);
    }

    instructionCount++; // Temporary tracker
}

void CPU::execute(uint32_t instruction) {
    // Isolate the 6-bit opcode
    uint32_t opcode = instruction >> 26;
    
    // 100011 01

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
                case 0x21: { // ADDU (Add Unsigned)
                    setRegister(registerTarget, getRegister(registerFirstSource) + getRegister(registerSecondSource));
                    break;
                }
                case 0x25: { // OR (Bitwise OR)
                    setRegister(registerTarget, getRegister(registerFirstSource) | getRegister(registerSecondSource));
                    break;
                }
                case 0x2b: { // SLTU (Set to 1 if Less Than Unsigned)
                    setRegister(registerTarget, getRegister(registerFirstSource) < getRegister(registerSecondSource));
                    break;
                }
                default:
                    cout << "Unimplemented R-Type function: 0x" << hex << function 
                      << " at PC: 0x" << (programCounter - 4) << endl;
                    cout << "\nTotal Instructions Executed: " << dec << instructionCount << endl; // Temporary tracking
                    exit(1);
            }
            break;
        }
        case 0x02: { // J (Jump to Address)
            uint32_t target = (instruction & 0x3FFFFFF) << 2;

            uint32_t programCounterRegion = programCounter & 0xF0000000;
            nextProgramCounter = programCounterRegion | target;
            
            break;
        }
        case 0x03: { // JAL (Jump and Link)
            uint32_t target = (instruction & 0x3FFFFFF) << 2;
    
            // Save the return address into Register 31
            setRegister(31, nextProgramCounter);

            uint32_t programCounterRegion = programCounter & 0xF0000000;
            nextProgramCounter = programCounterRegion | target;
            
            break;
        }
        case 0x05: { // BNE (Branch if Not Equal)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            uint32_t immediate = (uint32_t)(int32_t)(int16_t)(instruction & 0xFFFF); // This needs to be signed

            if(getRegister(registerSource) != getRegister(registerTarget)){
                nextProgramCounter = programCounter + (immediate << 2);
            }

            break;
        }
        case 0x08: { // ADDI (Add Immediate) // TODO: Implement signed overflow exception (Coprocessor 0)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            uint32_t immediate = (uint32_t)(int32_t)(int16_t)(instruction & 0xFFFF);

            setRegister(registerTarget, getRegister(registerSource) + immediate);
            break;
        }
        case 0x09: { // ADDIU (Add Unsigned Immediate)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            uint32_t immediate = (uint32_t)(int32_t)(int16_t)(instruction & 0xFFFF); // This needs to be signed, depending on what the left most bit is, if its 1, all 1s, if its 0, all 0s

            setRegister(registerTarget, getRegister(registerSource) + immediate);

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
                case 0x04: { // MTC0 (Move To Coprocessor 0)
                    uint32_t cpuRegisterSource = (instruction >> 16) & 0x1F;
                    uint32_t coprocessor0RegisterTarget = (instruction >> 11) & 0x1F;

                    // Copy data from the standard CPU register to the COP0 register
                    coprocessor0Registers[coprocessor0RegisterTarget] = getRegister(cpuRegisterSource);
                    break;
                }
                default:
                    cout << "Unimplemented COP0 instruction: " << dec << coprocessorOopcode 
                         << " at PC: 0x" << hex << (programCounter - 4) << endl;
                    exit(1);
            }
            break;
        }
        case 0x23: { // LW (Load Word)
            // Critical: It is important that after every LW is a NOP (0x00)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            uint32_t immediate = (uint32_t)(int32_t)(int16_t)(instruction & 0xFFFF); // This needs to be signed, depending on what the left most bit is, if its 1, all 1s, if its 0, all 0s

            pendingLoadRegister = registerTarget;
            pendingLoadValue = bus->read32(getRegister(registerSource) + immediate);
            break;
        }
        case 0x29: { // SH (Store Halfword)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            uint32_t immediate = (uint32_t)(int32_t)(int16_t)(instruction & 0xFFFF);

            // Mask to ensure we only send the bottom 16 bits
            uint16_t halfword = getRegister(registerTarget) & 0xFFFF;
            
            bus->write16(getRegister(registerSource) + immediate, halfword);
            break;
        }
        case 0x2B: { // SW (Store Word)
            uint32_t registerSource = (instruction >> 21) & 0x1F;
            uint32_t registerTarget = (instruction >> 16) & 0x1F;
            uint32_t immediate = (uint32_t)(int32_t)(int16_t)(instruction & 0xFFFF); // This needs to be signed, depending on what the left most bit is, if its 1, all 1s, if its 0, all 0s

            bus->write32(getRegister(registerSource) + immediate, getRegister(registerTarget));
            break;
        }
        default: // The Safety Net Crash
            cout << "Unimplemented instruction: 0x" << hex << instruction << " at PC: 0x" << (programCounter - 4) << endl;

            cout << "\nTotal Instructions Executed: " << dec << instructionCount << endl; // Temporary tracking
            exit(1); 
    }
}

// Register 0 is hardwired to 0 in physical silicon
void CPU::setRegister(uint32_t index, uint32_t value) {
    if (index == 0) return; 
    registers[index] = value;
}

uint32_t CPU::getRegister(uint32_t index) const {
    if (index == 0) return 0;
    return registers[index];
}
