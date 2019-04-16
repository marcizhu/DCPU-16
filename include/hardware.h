#pragma once

#include <cstdint>

#include "dcpu16.h"

class Hardware
{
protected:
	DCPU16* cpu;
	uint32_t id, version, manufacturer, irq;

	Hardware(DCPU16* c, uint32_t id, uint32_t ver, uint32_t man) : cpu(c), id(id), version(ver), manufacturer(man), irq(0) { }

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