#include "dcpu16.h"
#include "lem1802.h"

LEM1802::LEM1802(DCPU16* c, uint16_t delay) : Hardware(c, 0x7349f615, 0x1802, 0x1c6c8b36), delay(delay)
{
	pixels.resize(SCREEN_WIDTH * SCREEN_HEIGHT * 4, 0);
	this->counter = 0;

	SDL_Init(SDL_INIT_EVERYTHING);
	atexit(SDL_Quit);

	window = SDL_CreateWindow("LEM1802", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH * SCALE, SCREEN_HEIGHT * SCALE, SDL_WINDOW_SHOWN);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
}

LEM1802::~LEM1802()
{
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

void LEM1802::interrupt()
{
	switch(cpu->reg[A])
	{
		case 0: ramBase = cpu->reg[B]; break;
		case 1: fontBase    = cpu->reg[B]; break;
		case 2: paletteBase = cpu->reg[B]; break;
		case 3: borderColor = cpu->reg[B] & 0xF; break;
		case 4: for(uint16_t n = 0; n < 256; ++n) { cpu->mem[uint16_t(cpu->reg[B] + n)] = getFontCell(n, true); cpu->tick(1); } break;
		case 5: for(uint16_t n = 0; n <  16; ++n) { cpu->mem[uint16_t(cpu->reg[B] + n)] = getPalette(n,  true); cpu->tick(1); } break;
	}
}

void LEM1802::tick()
{
	// The screen refreshes at 60 Hz. The CPU ticks at 100 kHz. The ratio is 5000/3.
	for(this->counter += 3; this->counter >= 5000; this->counter -= 5000)
	{
		render(++blink & 32);
	}
}

void LEM1802::render(bool blink)
{
	Uint8 r, g, b;
	uint16_t backColor = getPalette(borderColor);

	r = ((backColor >> 8) & 0x0F) * 17; // adjustment
	g = ((backColor >> 4) & 0x0F) * 17; // adjustment
	b = ((backColor >> 0) & 0x0F) * 17; // adjustment

	std::fill(pixels.begin(), pixels.end(), 0);

	SDL_SetRenderDrawColor(renderer, r, g, b, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(renderer);

	for(unsigned int y = 0; y < 12; y++)
	{
		for(unsigned int x = 0; x < 32; x++)
		{
			uint16_t v = cpu->mem[(uint16_t)(ramBase + x + y * 32)];

			uint8_t fg = (v >> 12) & 0x0F;
			uint8_t bg = (v >>  8) & 0x0F;
			uint8_t ch = (v >>  0) & 0x7F;
			uint8_t bl = (v >>  0) & 0x80;

			uint16_t font[2] = { getFontCell(ch * 2 + 0), getFontCell(ch * 2 + 1) };

			if(bl && blink) fg = bg;

			for(unsigned int yp = 0; yp < 8; yp++)
			{
				for(unsigned int xp = 0; xp < 4; ++xp)
				{
					uint8_t color = ((font[xp / 2] & (1 << (yp + 8 * ((xp & 1) ^ 1)))) ? fg : bg);

					const unsigned int _x = x * 4 + xp;
					const unsigned int _y = y * 8 + yp;
					const unsigned int offset = (SCREEN_WIDTH * 4 * _y) + _x * 4;

					Uint8 r, g, b;
					uint16_t col = getPalette(color);

					r = ((col >> 8) & 0x0F) * 17; // adjustment
					g = ((col >> 4) & 0x0F) * 17; // adjustment
					b = ((col >> 0) & 0x0F) * 17; // adjustment

					pixels[offset + 0] = b;
					pixels[offset + 1] = g;
					pixels[offset + 2] = r;
					pixels[offset + 3] = SDL_ALPHA_OPAQUE;
				}
			}
		}
	}

	SDL_Delay(delay);

	SDL_UpdateTexture(texture, NULL, &pixels[0], SCREEN_WIDTH * 4);

	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

uint16_t LEM1802::getPalette(unsigned int n, bool force) const
{
	static const uint16_t palette[16] = { 0x000, 0x00A, 0x0A0, 0x0AA, 0xA00, 0xA0A, 0xAA5, 0xAAA, 0x555, 0x55F, 0x5F5, 0x5FF, 0xF55, 0xF5F, 0xFF5, 0xFFF };

	return (force || !paletteBase) ? palette[n] : cpu->mem[(uint16_t)(paletteBase + n)];
}

uint16_t LEM1802::getFontCell(unsigned int n, bool force) const
{
	static const uint16_t font4x8[256] =
		{
			0x0000, 0x0000, 0x3E65, 0x653E, 0x3E5B, 0x5B3E, 0x1E7C, 0x1E00, 0x1C7F, 0x1C00, 0x4C73, 0x4C00, 0x5C7F, 0x5C00, 0x183C, 0x1800,
			0xE7C3, 0xE7FF, 0x1824, 0x1800, 0xE7DB, 0xE7FF, 0xE7DB, 0xE7FF, 0x2C72, 0x2C00, 0x607F, 0x0507, 0x607F, 0x617F, 0x2A1F, 0x7C2A,
			0x7F3E, 0x1C08, 0x081C, 0x3E7F, 0x227F, 0x7F22, 0x5F00, 0x5F00, 0x0609, 0x7F7F, 0x9AA5, 0xA559, 0x6060, 0x6060, 0xA2FF, 0xFFA2,
			0x027F, 0x7F02, 0x207F, 0x7F20, 0x1818, 0x3C18, 0x183C, 0x1818, 0x3020, 0x2020, 0x081C, 0x1C08, 0x707E, 0x7E70, 0x0E7E, 0x7E0E,
			0x0000, 0x0000, 0x005F, 0x0000, 0x0700, 0x0700, 0x3E14, 0x3E00, 0x266B, 0x3200, 0x611C, 0x4300, 0x6659, 0xE690, 0x0005, 0x0300,
			0x1C22, 0x4100, 0x4122, 0x1C00, 0x2A1C, 0x2A00, 0x083E, 0x0800, 0x00A0, 0x6000, 0x0808, 0x0800, 0x0060, 0x0000, 0x601C, 0x0300,
			0x3E4D, 0x3E00, 0x427F, 0x4000, 0x6259, 0x4600, 0x2249, 0x3600, 0x0E08, 0x7F00, 0x2745, 0x3900, 0x3E49, 0x3200, 0x6119, 0x0700,
			0x3649, 0x3600, 0x2649, 0x3E00, 0x0066, 0x0000, 0x8066, 0x0000, 0x0814, 0x2241, 0x1414, 0x1400, 0x4122, 0x1408, 0x0259, 0x0600,
			0x3E59, 0x5E00, 0x7E09, 0x7E00, 0x7F49, 0x3600, 0x3E41, 0x2200, 0x7F41, 0x3E00, 0x7F49, 0x4100, 0x7F09, 0x0100, 0x3E49, 0x3A00,
			0x7F08, 0x7F00, 0x417F, 0x4100, 0x2040, 0x3F00, 0x7F0C, 0x7300, 0x7F40, 0x4000, 0x7F0E, 0x7F00, 0x7E1C, 0x7F00, 0x7F41, 0x7F00,
			0x7F09, 0x0600, 0x3E41, 0xBE00, 0x7F09, 0x7600, 0x2649, 0x3200, 0x017F, 0x0100, 0x7F40, 0x7F00, 0x1F60, 0x1F00, 0x7F30, 0x7F00,
			0x771C, 0x7700, 0x0778, 0x0700, 0x615D, 0x4300, 0x007F, 0x4100, 0x0618, 0x6000, 0x0041, 0x7F00, 0x0C06, 0x0C00, 0x8080, 0x8080,
			0x0003, 0x0500, 0x2454, 0x7800, 0x7F44, 0x3800, 0x3844, 0x2800, 0x3844, 0x7F00, 0x3854, 0x5800, 0x087E, 0x0900, 0x98A4, 0x7C00,
			0x7F04, 0x7800, 0x047D, 0x0000, 0x4080, 0x7D00, 0x7F10, 0x6C00, 0x417F, 0x4000, 0x7C18, 0x7C00, 0x7C04, 0x7800, 0x3844, 0x3800,
			0xFC24, 0x1800, 0x1824, 0xFC80, 0x7C04, 0x0800, 0x4854, 0x2400, 0x043E, 0x4400, 0x3C40, 0x7C00, 0x1C60, 0x1C00, 0x7C30, 0x7C00,
			0x6C10, 0x6C00, 0x9CA0, 0x7C00, 0x6454, 0x4C00, 0x0836, 0x4100, 0x0077, 0x0000, 0x4136, 0x0800, 0x0201, 0x0201, 0x704C, 0x7000
		};

	return (force || !fontBase) ? font4x8[n] : cpu->mem[(uint16_t)(fontBase + n)];
}