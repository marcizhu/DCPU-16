// DCPU-16 v1.7 emulator
#pragma once

#include <vector>
#include <cstdio>

#define MEM		0x80
#define IMM		0x40
#define NOREG	0x0F

typedef signed short sint16;
typedef signed int sint32;

enum REGISTERS { A, B, C, X, Y, Z, I, J, PC, SP, EX, IA };
enum INSTR { NBI, SET, ADD, SUB, MUL, MLI, DIV, DVI, MOD, MDI, AND, BOR, XOR, SHR, ASR, SHL, IFB, IFC, IFE, IFN, IFG, IFA, IFL, IFU, ADX = 0x1a, SBX, STI = 0x1e, STD };
enum NBI { JSR = 0x01, INT = 0x08, IAG, IAS, RFI, IAQ, HWN = 0x10, HWQ, HWI };

static const uint8_t reg_specs[0x20] =
	{
		A             , B             , C             , X             , Y             , Z             , I                , J                , // regs
		A        | MEM, B        | MEM, C        | MEM, X        | MEM, Y        | MEM, Z        | MEM, I           | MEM, J           | MEM,
		A  | IMM | MEM, B  | IMM | MEM, C  | IMM | MEM, X  | IMM | MEM, Y  | IMM | MEM, Z  | IMM | MEM, I     | IMM | MEM, J     | IMM | MEM,
		SP            , SP       | MEM, SP | IMM | MEM, SP            , PC            , EX            , NOREG | IMM | MEM, NOREG | IMM
	};

class Hardware;

class DCPU16
{
private:
	uint16_t reg[12] = {};
	uint16_t* mem;
	uint16_t irqQueue[256];
	uint8_t irqHead = 0, irqTail = 0;
	std::vector<Hardware*> hardware;
	bool irqQueuing = false;
	bool running = true;

	void tick(unsigned int n = 1);

	template<char tag>
	uint16_t& value(uint16_t val, bool skipping = false);

	void execute(bool skipping = false);

	friend class Hardware;
	friend class LEM1802;
	friend class Keyboard;
	friend class Clock;

public:
	DCPU16(std::vector<uint16_t> prog);

	void installHardware(Hardware* hw) { hardware.push_back(hw); }

	void interrupt(uint16_t a, bool from_hardware = false);

	void run();
	void halt();
	void dump();
};