// MiSTerFiles: "Tela / Overscan" adjust screen (MiSTer only)
#ifdef DEVICE_MISTER

#include <string>
#include <sstream>
#include <SDL.h>
#include "def.h"
#include "sdlutils.h"
#include "overscan.h"

namespace
{
void draw(int value)
{
   // Preview: apply the margins being edited, the outside is blacked out by MiSTer::present()
   MiSTer::setOverscan(value);
   const int w = SCREEN_WIDTH, h = SCREEN_HEIGHT;

   SDL_SetRenderDrawColor(g_renderer, COLOR_BODY_BG, 255);
   SDL_RenderClear(g_renderer);
   SDL_Rect body = { 0, 0, w, h };
   SDL_RenderFillRect(g_renderer, &body);

   // Frame around the safe area + corner marks
   const int t = 2, c = MiSTer::lowRes ? 16 : 24;
   SDL_SetRenderDrawColor(g_renderer, 255, 200, 0, 255);
   SDL_Rect frame[4] = { { 0, 0, w, t }, { 0, h - t, w, t }, { 0, 0, t, h }, { w - t, 0, t, h } };
   SDL_RenderFillRects(g_renderer, frame, 4);
   SDL_Rect corners[8] = { { 0, 0, c, 2 * t }, { 0, 0, 2 * t, c }, { w - c, 0, c, 2 * t }, { w - 2 * t, 0, 2 * t, c },
                           { 0, h - 2 * t, c, 2 * t }, { 0, h - c, 2 * t, c }, { w - c, h - 2 * t, c, 2 * t }, { w - 2 * t, h - c, 2 * t, c } };
   SDL_RenderFillRects(g_renderer, corners, 8);

   std::ostringstream m;
   m << "Margem: " << value << "%  (" << MiSTer::marginX << " x " << MiSTer::marginY << " px)";
   const char *lines[5] = { "Tela / Overscan", "", "Esquerda / Direita: ajustar", "A / Enter: salvar    B / Esc: cancelar", "As bordas amarelas devem aparecer inteiras na TV" };
   std::string l1 = m.str();
   lines[1] = l1.c_str();
   const int n = 5;
   int y = h / 2 - (n * LINE_HEIGHT) / 2 + LINE_HEIGHT / 2;
   for (int i = 0; i < n; ++i, y += LINE_HEIGHT)
      SDLUtils::renderText(lines[i], g_font, w / 2, y, i == 0 ? SDL_Color{255, 200, 0, 255} : SDL_Color{COLOR_TEXT_NORMAL}, {COLOR_BODY_BG},
                           SDLUtils::T_ALIGN_CENTER, SDLUtils::T_ALIGN_MIDDLE, w - 2 * MARGIN_X, SDLUtils::T_ALIGN_CENTER);
   SDL_RenderPresent(g_renderer);
   MiSTer::present();
}
}

int OverscanScreen::run(void)
{
   const int original = MiSTer::overscan;
   int value = original, result = -1, holdDir = 0;
   Uint32 nextRepeat = 0;
   bool redraw = true, done = false;
   SDL_Event event;
   while (!done)
   {
      Uint32 now = SDL_GetTicks();
      MiSTer::pumpInput();
      while (!done && SDL_PollEvent(&event))
      {
         if (event.type == SDL_QUIT) { done = true; break; }
         if (BUTTON_PRESSED_RIGHT || BUTTON_PRESSED_UP)
         {
            if (value < MiSTer::OVERSCAN_MAX) { ++value; redraw = true; }
            holdDir = 1; nextRepeat = now + 400;
         }
         else if (BUTTON_PRESSED_LEFT || BUTTON_PRESSED_DOWN)
         {
            if (value > 0) { --value; redraw = true; }
            holdDir = -1; nextRepeat = now + 400;
         }
         else if (BUTTON_PRESSED_VALIDATE) { result = value; done = true; }
         else if (BUTTON_PRESSED_BACK) { result = -1; done = true; }
      }
      // Key hold: repeat
      bool plus = BUTTON_HELD_RIGHT || BUTTON_HELD_UP, minus = BUTTON_HELD_LEFT || BUTTON_HELD_DOWN;
      if ((holdDir > 0 && plus) || (holdDir < 0 && minus))
      {
         if (now >= nextRepeat)
         {
            int nv = value + holdDir;
            if (nv >= 0 && nv <= MiSTer::OVERSCAN_MAX) { value = nv; redraw = true; }
            nextRepeat = now + 120;
         }
      }
      else holdDir = 0;
      if (redraw && !done) { draw(value); redraw = false; }
      SDL_Delay(16);
   }
   MiSTer::setOverscan(original);
   return result;
}

#endif // DEVICE_MISTER
