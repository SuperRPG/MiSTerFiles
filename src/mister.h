#ifndef _MISTER_H_
#define _MISTER_H_

// MiSTerFiles: MiSTer FPGA platform layer (fork of 351Files by Tardigrade-nx).
// - Video: SDL software renderer drawing into an SDL_Surface that is copied
//   to the Linux framebuffer (/dev/fb0, MiSTer_fb) on every present.
// - Input: keyboards read directly from evdev (/dev/input/event*), which
//   includes "MiSTer virtual input": while a script runs on the framebuffer,
//   Main_MiSTer translates the user's mapped gamepad into keys
//   (D-pad=arrows, A=Enter, B=Esc, X=Space, Y=Tab, L=PgUp, R=PgDn).
//   Optionally (MF_JOYSTICK=1) gamepads are also read through SDL
//   GameController / raw SDL joystick and translated to the same keys.
// Everything ends up as synthesized SDL_KEYDOWN/SDL_KEYUP events.

#include <SDL.h>

namespace MiSTer
{
   // Screen size, read from the framebuffer at startup
   extern int screenW;
   extern int screenH;
   extern bool lowRes;     // true when screen height < 400 (e.g. 240p CRT)

   // Open framebuffer and create software renderer. Returns NULL on failure.
   SDL_Renderer *init(void);
   // Copy rendered frame to framebuffer
   void present(void);
   // Read input devices and push SDL key events (call before SDL_PollEvent loop)
   void pumpInput(void);
   // Is key held (any keyboard, or a gamepad mapped to that key)?
   bool held(SDL_Scancode sc);
   // Restore console, close devices
   void close(void);
}

#endif
