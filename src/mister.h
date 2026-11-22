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
//
// The same layer also serves the GroovyArcade / x86_64 Linux target (DEVICE_GROOVY):
// - Video: normal SDL2 video driver (KMSDRM or X11, picked by SDL), fullscreen window;
//   the frame is still rendered in software into an SDL_Surface and copied (scaled if
//   needed) to the window surface on every present.
// - Input: SDL keyboard events and SDL GameController/joystick, translated through the
//   configurable bindings in MiSTerFiles.ini (KEY_A=..., JOY_A=...) into the same keys.

#include <SDL.h>

namespace MiSTer
{
   // Screen size, read from the framebuffer at startup
   extern int screenW;
   extern int screenH;
   extern bool lowRes;     // true when screen height < 400 (e.g. 240p CRT)

   // Overscan / safe area. OVERSCAN is a percentage of the screen height applied
   // to all four sides; the horizontal margin is scaled for a 4:3 display so the
   // border looks the same size on every side (640x240: marginX = 2 x marginY).
   // The UI is drawn inside the safe area (SDL viewport), the outside is black.
   extern int overscan;    // percent, 0..OVERSCAN_MAX
   extern int marginX;     // pixels
   extern int marginY;     // pixels
   const int OVERSCAN_MAX = 15;
   extern double displayAspect;   // physical aspect of the TV (4:3 by default; ASPECT=16:9 on GroovyArcade)
   int  overscanMarginX(int pct);
   int  overscanMarginY(int pct);
   // Change overscan and apply the viewport
   void setOverscan(int pct);
   // Persist KEY=value in MiSTerFiles.ini (keeps other lines/comments, creates from example)
   bool saveSetting(const char *key, int value);

   // Open framebuffer (MiSTer) / window (GroovyArcade) and create software renderer. NULL on failure.
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
