#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_render.h>

#include "hardware.h"

#define SCREEN_WIDTH 	128
#define SCREEN_HEIGHT 	96
#define SCALE 			4

//screen hardware

class LEM1802 : public Hardware
{
private:
	SDL_Window* window;
	SDL_Renderer* renderer;
	SDL_Texture* texture;
	std::vector<unsigned char> pixels;
	unsigned int counter; // cycle counter
	uint16_t ramBase;
	uint16_t fontBase = 0;
	uint16_t paletteBase = 0;
	uint16_t borderColor = 0;
	uint8_t blink = 0;
	uint16_t delay;

	void render(bool blink);

	uint16_t getPalette(unsigned int n, bool force = false) const;
	uint16_t getFontCell(unsigned int n, bool force = false) const;

public:
	LEM1802(DCPU16* c, uint16_t delay);
	~LEM1802() override;

	int keepAlive();

	void interrupt() override;
	void tick() override;
};
