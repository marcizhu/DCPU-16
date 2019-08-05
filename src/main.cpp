#include <cstdio>
#include <fstream>

#include "dcpu16.h"
#include "assembler.h"
#include "lem1802.h"
#include "keyboard.h"
#include "clock.h"

int main(int argc, char* argv[])
{
	if(argc <= 2) return printf("Usage:\t./dcpu <program file> <delay>\n");

	std::ifstream infile(argv[1], std::ifstream::binary);

	if (!infile.good()) return -1;

	infile.seekg(0, std::ios::end);
	int len = (int)infile.tellg();
	infile.seekg(0, std::ios::beg);

	char* Buf = new char[len];

	infile.read((char*)Buf, len);
	infile.close();

	std::vector<uint16_t> mem = Assembler({ Buf, Buf + len });

	uint16_t delay = (uint16_t)atoi(argv[2]);

	DCPU16* cpu = new DCPU16(mem);
	LEM1802* lem = new LEM1802(cpu, delay);
	Keyboard* kb = new Keyboard(cpu);
	Clock* clock = new Clock(cpu);

	cpu->run();

	delete cpu;
	delete lem;
	delete kb;
	delete clock;

	return 0;
}