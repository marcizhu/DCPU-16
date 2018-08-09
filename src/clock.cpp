#include "dcpu16.h"
#include "clock.h"

void Clock::interrupt()
{
    switch(cpu->reg[A])
    {
        case 0: counter = cycles = 0; divider = 5000 * cpu->reg[B]; break;
        case 1: cpu->reg[C] = counter; break;
        case 2: irq = cpu->reg[B]; break;
    }
}
void Clock::tick()
{
    // The CPU ticks at 100 kHz; the Clock at 60 Hz. The ratio is 5000/3.
    for(cycles += 3; divider && cycles >= divider; cycles -= divider)
    {
        ++counter;
        if(irq) cpu->interrupt(irq, true);
    }
}