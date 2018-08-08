#include <cstdio>
#include <SDL2/SDL.h>
#include <fstream>

#include "dcpu16.h"
#include "assembler.h"
#include "hardware.h"
#include "lem1802.h"
#include "keyboard.h"

int main(int argc, char* argv[])
{
    if(argc > 2)
    {
        std::ifstream infile(argv[1], std::ifstream::binary);

        if (!infile.good()) return -1;

        infile.seekg(0, std::ios::end);
        int len = (int)infile.tellg();
        infile.seekg(0, std::ios::beg);

        char* Buf = new char[len];

        infile.read((char*)Buf, len);
        infile.close();

        std::vector<uint16> mem = Assembler( {Buf,Buf+len} );

        DCPU16* cpu = new DCPU16(mem);
        LEM1802* lem = new LEM1802(cpu, atoi(argv[2]));

        cpu->installHardware(lem);
        cpu->run();

        //while(!lem->keepAlive());

        delete cpu;
        delete lem;
    }
    else
    {
    	printf("Usage:\t./dcpu <program file> <delay>\n");
    }

    /*for(int i = 0; i < 0x20; i++)
	{
		if(i % 8 == 0) printf("\n\t%04x: ", i);

		printf("%04X ", mem[i]);
	}

	puts("");*/

	return 0;
}