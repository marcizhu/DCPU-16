#include "hardware.h"

class Clock: public Hardware // Generic Clock
{
private:
    uint32_t divider;
    uint16_t counter;
    uint32_t cycles;

public:
    Clock(DCPU16* c) : Hardware(c, 0x12d0b402, 1, 0), divider(0), counter(0), cycles(0) { }

    void interrupt() override;
    void tick() override;
};