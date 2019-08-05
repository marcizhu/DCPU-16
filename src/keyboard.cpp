#include <SDL2/SDL.h>
#include "keyboard.h"

uint8_t Keyboard::translate(const SDL_Keysym& key)
{
	switch(key.sym)
	{
		case SDLK_BACKSPACE: return 0x10;
		case SDLK_RETURN:    return 0x11;
		case SDLK_INSERT:    return 0x12;
		case SDLK_DELETE:    return 0x13;
		case SDLK_ESCAPE:    return 0x1B;
		case SDLK_UP:        return 0x80;
		case SDLK_DOWN:      return 0x81;
		case SDLK_LEFT:      return 0x82;
		case SDLK_RIGHT:     return 0x83;
		case SDLK_RSHIFT:    return 0x90;
		case SDLK_RCTRL:     return 0x91;
		case SDLK_LSHIFT:    return 0x90;
		case SDLK_LCTRL:     return 0x91;
		default: if(key.sym >= 0x20 && key.sym <= 0x7F) return (uint8_t)key.sym;
	}

	return 0;
}

void Keyboard::interrupt()
{
	switch(cpu->reg[A])
	{
		case 0: bufhead = buftail; break;
		case 1: cpu->reg[C] = (bufhead == buftail ? 0 : buffer[buftail++]); break;
		case 2: cpu->reg[C] = (cpu->reg[B] < 0x100 && state[cpu->reg[B]]); break;
		case 3: irq = cpu->reg[B]; break;
	}
}

void Keyboard::tick()
{
	for(counter += 3; counter >= 10000; counter -= 10000) // Check keyboard events 50 times in a second
	{
		SDL_Event event;
		while(SDL_PollEvent(&event))
		{
			if(event.type == SDL_QUIT)
			{
				cpu->halt();
				break;
			}
			else if(event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
			{
				uint8_t code = translate(event.key.keysym);

				state[code] = (event.type == SDL_KEYDOWN);
				if(code && event.type == SDL_KEYDOWN) buffer[bufhead++] = code;
				if(irq) cpu->interrupt(irq, true);
			}
		}
	}
}