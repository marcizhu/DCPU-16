#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_render.h>

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
	uint16 ramBase;
	uint16 fontBase = 0;
	uint16 paletteBase = 0;
	uint16 borderColor = 0;
	uint8 blink = 0;
	uint16 delay;

	void render(bool blink);

	uint16 getPalette(unsigned int n, bool force = false) const;
	uint16 getFontCell(unsigned int n, bool force = false) const;

public:
	LEM1802(DCPU16* c, uint16 delay);
	~LEM1802() override;

	int keepAlive();

	void interrupt() override;
	void tick() override;
};