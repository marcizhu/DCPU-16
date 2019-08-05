#pragma once

#include <cstdint>

#include "dcpu16.h"

class Hardware
{
protected:
	DCPU16* cpu;
	uint32_t id, manufacturer;
	uint16_t version, irq;

	Hardware(DCPU16* c, uint32_t id, uint16_t ver, uint32_t man) : cpu(c), id(id), manufacturer(man), version(ver), irq(0) { cpu->installHardware(this); }

public:
	virtual ~Hardware() = default;
	virtual void interrupt() { }
	virtual void tick() { }

	void query()
	{
		cpu->reg[A] = (uint16_t)((id >>  0) & 0xFFFF);
		cpu->reg[B] = (uint16_t)((id >> 16) & 0xFFFF);
		cpu->reg[C] = version;
		cpu->reg[X] = (uint16_t)((manufacturer >>  0) & 0xFFFF);
		cpu->reg[Y] = (uint16_t)((manufacturer >> 16) & 0xFFFF);
	}
};