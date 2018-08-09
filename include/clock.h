class Clock: public Hardware // Generic Clock
{
    unsigned int divider;
    unsigned int counter;
    unsigned int cycles;

public:
    Clock(DCPU16* c) : Hardware(c, 0x12d0b402, 1, 0), divider(0), counter(0), cycles(0) { }

    void interrupt() override;
    void tick() override;
};