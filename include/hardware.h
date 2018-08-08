#pragma once

class Hardware
{
protected:
	DCPU16* cpu;
	uint32 id, version, manufacturer, irq;

	Hardware(DCPU16* c, uint32 id, uint32 ver, uint32 man) : cpu(c), id(id), version(ver), manufacturer(man), irq(0) { }

public:
	virtual ~Hardware() { }
	virtual void interrupt() { }
	virtual void tick() { }

	void query()
	{
		cpu->reg[A] = id;
		cpu->reg[B] = id >> 16;
		cpu->reg[C] = version;
		cpu->reg[X] = manufacturer;
		cpu->reg[Y] = manufacturer >> 16;
	}
};