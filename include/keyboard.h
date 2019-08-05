#pragma once

#include "hardware.h"

class Keyboard : public Hardware
{
private:
	uint8_t buffer[0x100];
	uint8_t state[0x100];
	uint8_t bufhead = 0, buftail = 0;
	unsigned int counter = 0; // tick counter

	// Translate SDL key symbol into DCPU key code.
	static uint8_t translate(const SDL_Keysym& key);

public:
	Keyboard(DCPU16* c) : Hardware(c, 0x30cf7406, 1, 0) { }

	void tick() override;
	void interrupt() override;
};