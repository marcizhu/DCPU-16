#include "dcpu16.h"
#include "hardware.h"

#define PUSH DCPU16::value<'b'>(0x18, false)
#define POP  DCPU16::value<'a'>(0x18, false)

DCPU16::DCPU16(std::vector<uint16_t> prog)
{
	mem = new uint16_t[0x10000];
	memset(mem, 0, 0x10000);

	for(unsigned int i = 0; i < prog.size(); i++)
	{
		mem[i] = prog[i];
	}
}

void DCPU16::tick(unsigned int n)
{
	while(n-- > 0)
	{
		for(const auto& p : hardware)
		{
			p->tick();
		}
	}
}

template<char tag>
uint16_t& DCPU16::value(uint16_t v, bool skipping)
{
	static uint16_t tmp;

	if(v == 0x18 && !skipping) return mem[tag == 'a' ? reg[SP]++ : --reg[SP]];

	if(v >= 0x20) return tmp = v-0x21;   // 20..3F, read-only immediate
	const auto specs = reg_specs[v];
	uint16_t* val = nullptr; tmp = 0;
	if((specs & 0xFu) != NOREG) tmp = *(val = &reg[specs & 0xFu]);
	if(specs & IMM) { tick(); val = &(tmp += mem[reg[PC]++]); }
	if(specs & MEM) return mem[tmp];
	return *val;
}

void DCPU16::interrupt(uint16_t num, bool fromHardware)
{
	if(!reg[IA]) return; // Interrupts disabled

	if(irqQueuing || fromHardware)
	{
		irqQueue[irqHead++] = num;
		if(irqHead == irqTail ) { /* Queue overflow! TODO: Execute HCF instruction */ }
	}
	else
	{
		PUSH = reg[PC];
		reg[PC] = reg[IA];
		PUSH = reg[A]; // MOVED  --  Move DOWN by 1 if issues!!!
		reg[A] = num;
		irqQueuing = true;
	}
}

void DCPU16::run()
{
	while(this->running == true)
	{
		if(!irqQueuing && irqHead != irqTail)
		{
			 uint16_t intno = irqQueue[irqTail++];
			 this->interrupt(intno);
		}

		execute();
	}
}

void DCPU16::halt()
{
	this->running = false;
}

void DCPU16::dump()
{
	printf("Register dump:\n");
	printf("\tA: %04x  [%04x]\n", reg[A], mem[reg[A]]);
	printf("\tB: %04x  [%04x]\n", reg[B], mem[reg[B]]);
	printf("\tC: %04x  [%04x]\n", reg[C], mem[reg[C]]);
	printf("\tX: %04x  [%04x]\n", reg[X], mem[reg[X]]);
	printf("\tY: %04x  [%04x]\n", reg[Y], mem[reg[Y]]);
	printf("\tZ: %04x  [%04x]\n", reg[Z], mem[reg[Z]]);
	printf("\tI: %04x  [%04x]\n", reg[I], mem[reg[I]]);
	printf("\tJ: %04x  [%04x]\n", reg[J], mem[reg[J]]);
	printf("\tPC: %04x  [%04x]\n", reg[PC], mem[reg[PC]]);
	printf("\tSP: %04x  [%04x]\n", reg[SP], mem[reg[SP]]);
	printf("\tEX: %04x  [%04x]\n", reg[EX], mem[reg[EX]]);
	printf("\tIA: %04x  [%04x]\n\n", reg[IA], mem[reg[IA]]);


	printf("Memory dump:");
	for(int i = 0; i < 0x10000; i++)
	{
		if(i % 8 == 0) printf("\n\t%04x: ", i);

		printf("%04X ", mem[i]);
	}
}

void DCPU16::execute(bool skipping)
{
	uint16_t inst = mem[reg[PC]++]; // read instruction and point to the next one

	uint16_t aa = (inst >> 10) & 0x3f;
	uint16_t bb = (inst >>  5) & 0x1f;
	uint16_t op = (inst >>  0) & 0x1f;

	uint16_t& a = value<'a'>(aa, skipping);
	uint16_t& b = (op == INSTR::NBI ? op : value<'b'>(bb, skipping));

	sint32 sa = (sint16)a;
	sint32 sb = (sint16)b;

	uint32_t t;
	sint32 s;

	uint32_t wb = b;

	if(skipping)
	{
		if(op >= INSTR::IFB && op <= INSTR::IFU)
		{
			tick(1);
			execute(true);
		}
	}
	else
	{
		switch(op)
		{
			case INSTR::NBI:
				switch(bb)
				{
					case NBI::JSR: tick(3); PUSH = reg[PC]; reg[PC] = a; break;
					case NBI::INT: tick(4); interrupt(a); break;
					case NBI::IAG: tick(1); a = reg[IA]; break;
					case NBI::IAS: tick(1); reg[IA] = a; break;
					case NBI::RFI: tick(3); irqQueuing = false; reg[A] = POP; reg[PC] = POP; break;
					case NBI::IAQ: tick(2); irqQueuing = (a == 0 ? false : true); break;
					case NBI::HWN: tick(2); a = (uint16_t)hardware.size(); break;
					case NBI::HWQ: tick(4); if(a < hardware.size()) hardware[a]->query(); break;
					case NBI::HWI: tick(4); if(a < hardware.size()) hardware[a]->interrupt(); break;
					default: std::fprintf(stderr, "Invalid opcode %04X at PC=%04X\n", op, reg[PC]); break;
				}
				break;

			case INSTR::SET: tick(1); b = a; break;
			case INSTR::ADD: tick(2); t =  b +  a; b = (uint16_t)t; reg[EX] = (uint16_t)(t >> 16); break;
			case INSTR::SUB: tick(2); t =  b -  a; b = (uint16_t)t; reg[EX] = (uint16_t)(t >> 16); break;
			case INSTR::MUL: tick(2); t =  b *  a; b = (uint16_t)t; reg[EX] = (uint16_t)(t >> 16); break;
			case INSTR::MLI: tick(2); s = sb * sa; b = (uint16_t)s; reg[EX] = (uint16_t)(s >> 16); break;
			case INSTR::DIV: tick(3); t =  a ? (wb << 16) /  a : 0; b = (uint16_t)(t >> 16); reg[EX] = (uint16_t)t; break;
			case INSTR::DVI: tick(3); s = sa ? (sb << 16) / sa : 0; b = (uint16_t)(s >> 16); reg[EX] = (uint16_t)s; break;
			case INSTR::MOD: tick(3); b = (uint16_t)( a ?  b %  a : 0); break;
			case INSTR::MDI: tick(3); b = (uint16_t)(sa ? sb % sa : 0); break;
			case INSTR::AND: tick(1); b &= a; break;
			case INSTR::BOR: tick(1); b |= a; break;
			case INSTR::XOR: tick(1); b ^= a; break;
			case INSTR::SHR: tick(1); t = (wb << 16) >> a; b = (uint16_t)(t >> 16); reg[EX] = (uint16_t)t; break;
			case INSTR::ASR: tick(1); s = (sb << 16) >> a; b = (uint16_t)(s >> 16); reg[EX] = (uint16_t)s; break;
			case INSTR::SHL: tick(1); t = wb << a; b = (uint16_t)t; reg[EX] = (uint16_t)(t >> 16); break;
			case INSTR::IFB: tick(2); if(!( b &  a)) execute(true); break;
			case INSTR::IFC: tick(2); if(   b &  a ) execute(true); break;
			case INSTR::IFE: tick(2); if(!( b == a)) execute(true); break;
			case INSTR::IFN: tick(2); if(!( b != a)) execute(true); break;
			case INSTR::IFG: tick(2); if(!( b >  a)) execute(true); break;
			case INSTR::IFA: tick(2); if(!(sb > sa)) execute(true); break;
			case INSTR::IFL: tick(2); if(!( b <  a)) execute(true); break;
			case INSTR::IFU: tick(2); if(!(sb < sa)) execute(true); break;
			case INSTR::ADX: tick(3); t = b + a + reg[EX]; b = (uint16_t)t; reg[EX] = (t >> 16) != 0 ? 0x0001 : 0x0000; break;
			case INSTR::SBX: tick(3); t = b - a + reg[EX]; b = (uint16_t)t; reg[EX] = (t >> 16); break; // EX should be 0xFFFF only if underflow!
			case INSTR::STI: tick(2); b = a; reg[I]++; reg[J]++; break;
			case INSTR::STD: tick(2); b = a; reg[I]--; reg[J]--; break;
			default: std::fprintf(stderr, "Invalid opcode %04X at PC=%04X\n", op, reg[PC]); break;
		}
	}
}