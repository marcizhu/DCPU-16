#include <cstdio>
#include <sys/stat.h>
#include <string>
#include <vector>

#include "dcpu16.h"
#include "assembler.h"
#include "lem1802.h"
#include "keyboard.h"
#include "clock.h"

int main(int argc, char* argv[])
{
	if(argc <= 2) return printf("Usage:\t./dcpu <program file> <delay>\n");

	struct stat info;
	uint64_t size = stat(argv[1], &info) < 0 ? 0 : (uint64_t)info.st_size;
	std::string buff(size, '\0');

	FILE* file = fopen(argv[1], "rb");

	if (!file) return 0;

	fread((char*)buff.data(), size, 1, file);
	fclose(file);

	std::vector<uint16_t> mem = Assembler(buff);

	uint16_t delay = (uint16_t)atoi(argv[2]);

	DCPU16* cpu = new DCPU16(mem);
	cpu->installHardware(new LEM1802(cpu, delay));
	cpu->installHardware(new Keyboard(cpu));
	cpu->installHardware(new Clock(cpu));
	cpu->run();

	delete cpu;

	return 0;
}