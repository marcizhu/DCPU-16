// DCPU-16 v1.7 emulator
#pragma once

#include <vector>
#include <cstdio>

typedef unsigned short uint16;
typedef signed short sint16;
typedef unsigned int uint32;
typedef signed int sint32;
typedef unsigned char uint8;

enum REGISTERS { A, B, C, X, Y, Z, I, J, PC, SP, EX, IA };
enum INSTR { NBI, SET, ADD, SUB, MUL, MLI, DIV, DVI, MOD, MDI, AND, BOR, XOR, SHR, ASR, SHL, IFB, IFC, IFE, IFN, IFG, IFA, IFL, IFU, ADX = 0x1a, SBX, STI = 0x1e, STD };
enum NBI { JSR = 0x01, INT = 0x08, IAG, IAS, RFI, IAQ, HWN = 0x10, HWQ, HWI };
enum reg_index { noreg=0xF, mem=0x80, imm=0x40 };

static const uint8 reg_specs[0x20] =
    {A        ,B        ,C        ,X        ,Y        ,Z        ,I        ,J, // regs
     A    |mem,B    |mem,C    |mem,X    |mem,Y    |mem,Z    |mem,I    |mem,J    |mem,
     A|imm|mem,B|imm|mem,C|imm|mem,X|imm|mem,Y|imm|mem,Z|imm|mem,I|imm|mem,J|imm|mem,
     SP,       SP|mem, SP|imm|mem, SP, PC, EX, noreg|imm|mem, noreg|imm};

class DCPU16
{
private:
	uint16 reg[12] = {};
	uint16* mem;
	uint16 irqQueue[256];
	uint8 irqHead = 0, irqTail = 0;
	bool irqQueuing = false;
	std::vector<class Hardware*> hardware;
	bool running = true;

	void tick(unsigned int n = 1);

	template<char tag>
	uint16& value(uint16 val, bool skipping = false);

	void execute(bool skipping = false);

	friend class Hardware;
	friend class LEM1802;
	friend class Keyboard;

public:
	DCPU16(std::vector<uint16> prog);

	void installHardware(Hardware* hw) { hardware.push_back(hw); }

	void interrupt(uint16 a, bool from_hardware = false);

	void run();
	void halt();
	void dump();
};

#include "hardware.h"