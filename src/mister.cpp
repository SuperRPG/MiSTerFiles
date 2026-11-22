// MiSTerFiles: MiSTer FPGA platform layer (see mister.h)
#ifdef DEVICE_MISTER

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <map>
#include <cmath>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>
#include <string>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/input.h>
#include "mister.h"

namespace MiSTer
{
int screenW = 640;
int screenH = 480;
bool lowRes = false;
int overscan = 0;
int marginX = 0;
int marginY = 0;
}

namespace
{
// ---------------------------------------------------------------- video ----
int            fbFd = -1;
uint8_t       *fbMem = NULL;
size_t         fbMemSize = 0;
size_t         fbOffset = 0;
int            fbPitch = 0;
SDL_Surface   *backSurface = NULL;
SDL_Renderer  *renderer = NULL;
const char    *screenshotPath = NULL;   // MF_SCREENSHOT: dump last frame (PPM) on exit

// -------------------------------------------------------------- console ----
int            ttyFd = -1;
int            ttyOldMode = KD_TEXT;
bool           ttyModeSet = false;
struct termios ttyOldTermios;
bool           ttyTermiosSet = false;

// ---------------------------------------------------------------- input ----
// Per-source set of held SDL scancodes, global hold counters
struct Source { std::vector<bool> down; Source() : down(SDL_NUM_SCANCODES, false) {} };
std::map<long, Source> sources;              // key: evdev fd, or 1e6 + SDL joystick instance id
int  holdCount[SDL_NUM_SCANCODES] = {0};

struct EvDev { std::string path; int fd; };
std::vector<EvDev> evdevs;
Uint32 lastScan = 0;

bool useJoystick = false;
struct Pad { SDL_Joystick *joy; SDL_GameController *gc; SDL_JoystickID id; };
std::vector<Pad> pads;

void pushKey(SDL_Scancode sc, bool down)
{
   SDL_Event e;
   SDL_zero(e);
   e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
   e.key.timestamp = SDL_GetTicks();
   e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
   e.key.repeat = 0;
   e.key.keysym.scancode = sc;
   e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
   SDL_PushEvent(&e);
}

void setKey(long src, SDL_Scancode sc, bool down)
{
   if (sc == SDL_SCANCODE_UNKNOWN) return;
   Source &s = sources[src];
   if (s.down[sc] == down) return;
   s.down[sc] = down;
   if (down) { if (holdCount[sc]++ == 0) pushKey(sc, true); }
   else if (holdCount[sc] > 0) { if (--holdCount[sc] == 0) pushKey(sc, false); }
}

void releaseSource(long src)
{
   std::map<long, Source>::iterator it = sources.find(src);
   if (it == sources.end()) return;
   for (int sc = 0; sc < SDL_NUM_SCANCODES; ++sc)
      if (it->second.down[sc]) setKey(src, (SDL_Scancode)sc, false);
   sources.erase(src);
}

SDL_Scancode linuxToSdl(int code)
{
   switch (code)
   {
      case KEY_UP: return SDL_SCANCODE_UP;
      case KEY_DOWN: return SDL_SCANCODE_DOWN;
      case KEY_LEFT: return SDL_SCANCODE_LEFT;
      case KEY_RIGHT: return SDL_SCANCODE_RIGHT;
      case KEY_ENTER: return SDL_SCANCODE_RETURN;
      case KEY_KPENTER: return SDL_SCANCODE_KP_ENTER;
      case KEY_ESC: return SDL_SCANCODE_ESCAPE;
      case KEY_BACKSPACE: return SDL_SCANCODE_BACKSPACE;
      case KEY_TAB: return SDL_SCANCODE_TAB;
      case KEY_SPACE: return SDL_SCANCODE_SPACE;
      case KEY_INSERT: return SDL_SCANCODE_INSERT;
      case KEY_DELETE: return SDL_SCANCODE_DELETE;
      case KEY_HOME: return SDL_SCANCODE_HOME;
      case KEY_END: return SDL_SCANCODE_END;
      case KEY_PAGEUP: return SDL_SCANCODE_PAGEUP;
      case KEY_PAGEDOWN: return SDL_SCANCODE_PAGEDOWN;
      case KEY_COMPOSE: return SDL_SCANCODE_APPLICATION;
      case KEY_MENU: return SDL_SCANCODE_MENU;
      case KEY_X: return SDL_SCANCODE_X;
      case KEY_Q: return SDL_SCANCODE_Q;
      case KEY_LEFTCTRL: return SDL_SCANCODE_LCTRL;
      case KEY_RIGHTCTRL: return SDL_SCANCODE_RCTRL;
      case KEY_LEFTSHIFT: return SDL_SCANCODE_LSHIFT;
      case KEY_RIGHTSHIFT: return SDL_SCANCODE_RSHIFT;
      case KEY_C: return SDL_SCANCODE_C;
      case KEY_F1: return SDL_SCANCODE_F1;
      case KEY_F10: return SDL_SCANCODE_F10;
      case KEY_KP8: return SDL_SCANCODE_KP_8;
      case KEY_KP2: return SDL_SCANCODE_KP_2;
      case KEY_KP4: return SDL_SCANCODE_KP_4;
      case KEY_KP6: return SDL_SCANCODE_KP_6;
      default: return SDL_SCANCODE_UNKNOWN;
   }
}

#define BITS_PER_LONG (sizeof(long) * 8)
#define TEST_BIT(bit, arr) ((arr[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

bool isKeyboard(int fd)
{
   unsigned long keys[(KEY_MAX + BITS_PER_LONG) / BITS_PER_LONG];
   memset(keys, 0, sizeof(keys));
   if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0) return false;
   // Anything able to send Enter or arrows counts (real keyboards, MiSTer virtual input)
   return TEST_BIT(KEY_ENTER, keys) || TEST_BIT(KEY_UP, keys) || TEST_BIT(KEY_ESC, keys);
}

void scanEvdev(void)
{
   DIR *dir = opendir("/dev/input");
   if (dir == NULL) return;
   struct dirent *ent;
   while ((ent = readdir(dir)) != NULL)
   {
      if (strncmp(ent->d_name, "event", 5) != 0) continue;
      std::string path = std::string("/dev/input/") + ent->d_name;
      bool known = false;
      for (size_t i = 0; i < evdevs.size(); ++i) if (evdevs[i].path == path) { known = true; break; }
      if (known) continue;
      int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
      if (fd < 0) continue;
      if (!isKeyboard(fd)) { ::close(fd); continue; }
      char name[128] = "?";
      ioctl(fd, EVIOCGNAME(sizeof(name)), name);
      std::cout << "MiSTer: keyboard input " << path << " (" << name << ")" << std::endl;
      EvDev d; d.path = path; d.fd = fd;
      evdevs.push_back(d);
   }
   closedir(dir);
}

void readEvdev(void)
{
   for (size_t i = 0; i < evdevs.size(); )
   {
      struct input_event ev[32];
      bool dead = false;
      for (;;)
      {
         ssize_t n = read(evdevs[i].fd, ev, sizeof(ev));
         if (n < 0) { if (errno != EAGAIN && errno != EINTR) dead = true; break; }
         if (n == 0) break;
         for (size_t k = 0; k < n / sizeof(struct input_event); ++k)
         {
            if (ev[k].type != EV_KEY || ev[k].value == 2) continue;   // ignore autorepeat
            setKey(evdevs[i].fd, linuxToSdl(ev[k].code), ev[k].value != 0);
         }
      }
      if (dead)
      {
         releaseSource(evdevs[i].fd);
         ::close(evdevs[i].fd);
         evdevs.erase(evdevs.begin() + i);
      }
      else ++i;
   }
}

// --------------------------------------------------------- SDL joystick ----
void openPad(int index)
{
   Pad p; p.joy = NULL; p.gc = NULL;
   if (SDL_IsGameController(index))
   {
      p.gc = SDL_GameControllerOpen(index);
      if (p.gc) p.joy = SDL_GameControllerGetJoystick(p.gc);
   }
   if (p.joy == NULL) p.joy = SDL_JoystickOpen(index);
   if (p.joy == NULL) return;
   p.id = SDL_JoystickInstanceID(p.joy);
   for (size_t i = 0; i < pads.size(); ++i) if (pads[i].id == p.id) return;
   std::cout << "MiSTer: gamepad " << SDL_JoystickName(p.joy) << (p.gc ? " (GameController)" : " (raw joystick)") << std::endl;
   pads.push_back(p);
}

void closePad(SDL_JoystickID id)
{
   for (size_t i = 0; i < pads.size(); ++i)
      if (pads[i].id == id)
      {
         releaseSource(1000000L + id);
         if (pads[i].gc) SDL_GameControllerClose(pads[i].gc); else SDL_JoystickClose(pads[i].joy);
         pads.erase(pads.begin() + i);
         return;
      }
}

void pollPads(void)
{
   const int T = 16000;
   for (size_t i = 0; i < pads.size(); ++i)
   {
      Pad &p = pads[i];
      bool up = false, down = false, left = false, right = false, a = false, b = false, sel = false, menu = false, pgu = false, pgd = false;
      if (p.gc)
      {
         SDL_GameController *g = p.gc;
         up    = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_DPAD_UP)    || SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_LEFTY) < -T;
         down  = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_LEFTY) >  T;
         left  = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_LEFTX) < -T;
         right = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_LEFTX) >  T;
         a     = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_A);
         b     = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_B);
         sel   = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_X) || SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_BACK);
         menu  = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_Y) || SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_START);
         pgu   = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)  || SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > T;
         pgd   = SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) || SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > T;
      }
      else
      {
         SDL_Joystick *j = p.joy;
         if (SDL_JoystickNumHats(j) > 0)
         {
            Uint8 h = SDL_JoystickGetHat(j, 0);
            up = h & SDL_HAT_UP; down = h & SDL_HAT_DOWN; left = h & SDL_HAT_LEFT; right = h & SDL_HAT_RIGHT;
         }
         if (SDL_JoystickNumAxes(j) >= 2)
         {
            Sint16 x = SDL_JoystickGetAxis(j, 0), y = SDL_JoystickGetAxis(j, 1);
            up |= y < -T; down |= y > T; left |= x < -T; right |= x > T;
         }
         int nb = SDL_JoystickNumButtons(j);
         a    = nb > 0 && SDL_JoystickGetButton(j, 0);
         b    = nb > 1 && SDL_JoystickGetButton(j, 1);
         sel  = (nb > 2 && SDL_JoystickGetButton(j, 2)) || (nb > 6 && SDL_JoystickGetButton(j, 6));
         menu = (nb > 3 && SDL_JoystickGetButton(j, 3)) || (nb > 7 && SDL_JoystickGetButton(j, 7));
         pgu  = nb > 4 && SDL_JoystickGetButton(j, 4);
         pgd  = nb > 5 && SDL_JoystickGetButton(j, 5);
      }
      long src = 1000000L + p.id;
      setKey(src, SDL_SCANCODE_UP, up);
      setKey(src, SDL_SCANCODE_DOWN, down);
      setKey(src, SDL_SCANCODE_LEFT, left);
      setKey(src, SDL_SCANCODE_RIGHT, right);
      setKey(src, SDL_SCANCODE_RETURN, a);
      setKey(src, SDL_SCANCODE_ESCAPE, b);
      setKey(src, SDL_SCANCODE_SPACE, sel);
      setKey(src, SDL_SCANCODE_TAB, menu);
      setKey(src, SDL_SCANCODE_PAGEUP, pgu);
      setKey(src, SDL_SCANCODE_PAGEDOWN, pgd);
   }
}

// -------------------------------------------------------------- console ----
void consoleRestore(void)
{
   if (ttyFd < 0) return;
   if (ttyModeSet) ioctl(ttyFd, KDSETMODE, ttyOldMode);
   tcflush(ttyFd, TCIFLUSH);   // drop keys typed while we were running
   if (ttyTermiosSet) tcsetattr(ttyFd, TCSANOW, &ttyOldTermios);
   ttyModeSet = ttyTermiosSet = false;
}

void consoleSetup(void)
{
   const char *names[] = { "/dev/tty", "/dev/tty0" };
   for (int i = 0; i < 2 && ttyFd < 0; ++i)
   {
      int fd = open(names[i], O_RDWR | O_CLOEXEC);
      if (fd < 0) continue;
      int mode;
      if (ioctl(fd, KDGETMODE, &mode) == 0) { ttyFd = fd; ttyOldMode = mode; }
      else ::close(fd);
   }
   if (ttyFd < 0) { std::cout << "MiSTer: no console tty, text console stays as is" << std::endl; return; }
   if (ioctl(ttyFd, KDSETMODE, KD_GRAPHICS) == 0) ttyModeSet = true;
   if (tcgetattr(ttyFd, &ttyOldTermios) == 0)
   {
      struct termios t = ttyOldTermios;
      t.c_lflag &= ~(ICANON | ECHO);
      if (tcsetattr(ttyFd, TCSANOW, &t) == 0) ttyTermiosSet = true;
   }
}

void signalHandler(int sig)
{
   consoleRestore();
   signal(sig, SIG_DFL);
   raise(sig);
}

void saveScreenshot(void)
{
   if (screenshotPath == NULL || backSurface == NULL) return;
   SDL_Surface *rgb = SDL_ConvertSurfaceFormat(backSurface, SDL_PIXELFORMAT_RGB24, 0);
   if (rgb == NULL) return;
   FILE *f = fopen(screenshotPath, "wb");
   if (f)
   {
      fprintf(f, "P6\n%d %d\n255\n", rgb->w, rgb->h);
      for (int y = 0; y < rgb->h; ++y) fwrite((uint8_t *)rgb->pixels + y * rgb->pitch, 3, rgb->w, f);
      fclose(f);
   }
   SDL_FreeSurface(rgb);
}
} // namespace

//------------------------------------------------------------------------------

SDL_Renderer *MiSTer::init(void)
{
   const char *fbPath = getenv("MF_FBDEV") ? getenv("MF_FBDEV") : "/dev/fb0";
   const char *fake = getenv("MF_FAKEFB");   // "WxH": no framebuffer, for off-target testing
   screenshotPath = getenv("MF_SCREENSHOT");
   Uint32 rmask = 0x00FF0000, gmask = 0x0000FF00, bmask = 0x000000FF;
   int bpp = 32;

   if (fake != NULL && sscanf(fake, "%dx%d", &screenW, &screenH) == 2)
   {
      std::cout << "MiSTer: fake framebuffer " << screenW << "x" << screenH << std::endl;
   }
   else
   {
      fbFd = open(fbPath, O_RDWR | O_CLOEXEC);
      if (fbFd < 0) { std::cerr << "MiSTer: cannot open " << fbPath << ": " << strerror(errno) << std::endl; return NULL; }
      struct fb_var_screeninfo var;
      struct fb_fix_screeninfo fix;
      if (ioctl(fbFd, FBIOGET_VSCREENINFO, &var) < 0 || ioctl(fbFd, FBIOGET_FSCREENINFO, &fix) < 0)
      {
         std::cerr << "MiSTer: framebuffer ioctl failed: " << strerror(errno) << std::endl;
         return NULL;
      }
      screenW = var.xres;
      screenH = var.yres;
      bpp = var.bits_per_pixel;
      fbPitch = fix.line_length;
      std::cout << "MiSTer: " << fbPath << " " << var.xres << "x" << var.yres << " (virtual " << var.xres_virtual << "x" << var.yres_virtual
                << ") " << bpp << "bpp pitch " << fbPitch << " R" << var.red.offset << "/" << var.red.length
                << " G" << var.green.offset << "/" << var.green.length << " B" << var.blue.offset << "/" << var.blue.length << std::endl;
      if (bpp != 32 && bpp != 16)
      {
         std::cerr << "MiSTer: unsupported framebuffer depth " << bpp << " (use vmode -r W H rgb32)" << std::endl;
         return NULL;
      }
      rmask = ((1u << var.red.length) - 1) << var.red.offset;
      gmask = ((1u << var.green.length) - 1) << var.green.offset;
      bmask = ((1u << var.blue.length) - 1) << var.blue.offset;
      fbMemSize = fix.smem_len;
      fbOffset = var.yoffset * fix.line_length + var.xoffset * (bpp / 8);
      fbMem = (uint8_t *)mmap(NULL, fbMemSize, PROT_READ | PROT_WRITE, MAP_SHARED, fbFd, 0);
      if (fbMem == MAP_FAILED) { fbMem = NULL; std::cerr << "MiSTer: mmap failed: " << strerror(errno) << std::endl; return NULL; }
      if (fbOffset + (size_t)fbPitch * screenH > fbMemSize) fbOffset = 0;
   }

   // MF_SWAP_RB=1: swap red/blue if colors come out wrong on the real hardware
   if (getenv("MF_SWAP_RB") && atoi(getenv("MF_SWAP_RB")) != 0) { Uint32 t = rmask; rmask = bmask; bmask = t; }

   if (getenv("MF_LOWRES")) lowRes = atoi(getenv("MF_LOWRES")) != 0;
   else lowRes = screenH < 400;

   backSurface = SDL_CreateRGBSurface(0, screenW, screenH, bpp, rmask, gmask, bmask, 0);
   if (backSurface == NULL) { std::cerr << "MiSTer: surface: " << SDL_GetError() << std::endl; return NULL; }
   renderer = SDL_CreateSoftwareRenderer(backSurface);
   if (renderer == NULL) { std::cerr << "MiSTer: renderer: " << SDL_GetError() << std::endl; return NULL; }
   setOverscan(getenv("MF_OVERSCAN") ? atoi(getenv("MF_OVERSCAN")) : 0);
   std::cout << "MiSTer: overscan " << overscan << "% -> margins " << marginX << "x" << marginY
             << ", safe area " << (screenW - 2 * marginX) << "x" << (screenH - 2 * marginY) << std::endl;

   if (fbFd >= 0) consoleSetup();
   signal(SIGINT, signalHandler);
   signal(SIGTERM, signalHandler);
   signal(SIGHUP, signalHandler);
   signal(SIGSEGV, signalHandler);
   signal(SIGABRT, signalHandler);

   // Input
   useJoystick = getenv("MF_JOYSTICK") && atoi(getenv("MF_JOYSTICK")) != 0;
   if (useJoystick)
   {
      SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
      if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0)
      {
         const char *db = getenv("MF_GAMECONTROLLERDB") ? getenv("MF_GAMECONTROLLERDB") : "./res/gamecontrollerdb.txt";
         int n = SDL_GameControllerAddMappingsFromFile(db);
         if (n > 0) std::cout << "MiSTer: loaded " << n << " controller mappings from " << db << std::endl;
         for (int i = 0; i < SDL_NumJoysticks(); ++i) openPad(i);
      }
      else
      {
         std::cerr << "MiSTer: SDL gamecontroller init failed: " << SDL_GetError() << std::endl;
         useJoystick = false;
      }
   }
   // Debug aid: MF_INPUT_FILE=<fifo> carrying raw struct input_event records
   if (getenv("MF_INPUT_FILE"))
   {
      int fd = open(getenv("MF_INPUT_FILE"), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
      if (fd >= 0) { EvDev d; d.path = getenv("MF_INPUT_FILE"); d.fd = fd; evdevs.push_back(d); }
   }
   scanEvdev();
   lastScan = SDL_GetTicks();
   return renderer;
}

//------------------------------------------------------------------------------

int MiSTer::overscanMarginY(int pct)
{
   if (pct < 0) pct = 0;
   if (pct > OVERSCAN_MAX) pct = OVERSCAN_MAX;
   return (int)lround(screenH * pct / 100.0);
}

int MiSTer::overscanMarginX(int pct)
{
   // Same physical size as the vertical margin on a 4:3 display:
   // pixel aspect = (4/3) / (W/H)  =>  marginX = marginY * (W/H) * 3/4
   return (int)lround(overscanMarginY(pct) * ((double)screenW / screenH) * 0.75);
}

void MiSTer::setOverscan(int pct)
{
   if (pct < 0) pct = 0;
   if (pct > OVERSCAN_MAX) pct = OVERSCAN_MAX;
   overscan = pct;
   marginX = overscanMarginX(pct);
   marginY = overscanMarginY(pct);
   if (renderer != NULL)
   {
      SDL_Rect vp = { marginX, marginY, screenW - 2 * marginX, screenH - 2 * marginY };
      SDL_RenderSetViewport(renderer, &vp);
   }
}

//------------------------------------------------------------------------------

bool MiSTer::saveSetting(const char *key, int value)
{
   std::string path = getenv("MF_INI") ? getenv("MF_INI") : "./MiSTerFiles.ini";
   std::string example = getenv("MF_INI_EXAMPLE") ? getenv("MF_INI_EXAMPLE") : "./MiSTerFiles.ini.example";

   // Read the current file, or the example as a starting point
   std::vector<std::string> lines;
   bool crlf = false;
   {
      std::ifstream in(path.c_str(), std::ios::binary);
      if (!in.good()) in.open(example.c_str(), std::ios::binary);
      std::string line;
      while (in.good() && std::getline(in, line))
      {
         if (!line.empty() && line[line.size() - 1] == '\r') { line.erase(line.size() - 1); crlf = true; }
         lines.push_back(line);
      }
   }

   // Replace every "KEY=..." line (bash: last one wins, keep them consistent), else append
   std::ostringstream val;
   val << key << "=" << value;
   bool found = false;
   const size_t klen = strlen(key);
   for (size_t i = 0; i < lines.size(); ++i)
   {
      size_t p = lines[i].find_first_not_of(" \t");
      if (p == std::string::npos || lines[i].compare(p, klen, key) != 0) continue;
      size_t q = lines[i].find_first_not_of(" \t", p + klen);
      if (q == std::string::npos || lines[i][q] != '=') continue;
      lines[i] = val.str();
      found = true;
   }
   if (!found) lines.push_back(val.str());

   // Write to a temporary file, then rename over the original
   std::string tmp = path + ".tmp";
   FILE *f = fopen(tmp.c_str(), "wb");
   if (f == NULL) { std::cerr << "MiSTer: cannot write " << tmp << ": " << strerror(errno) << std::endl; return false; }
   for (size_t i = 0; i < lines.size(); ++i)
      fprintf(f, "%s%s", lines[i].c_str(), crlf ? "\r\n" : "\n");
   bool ok = (fflush(f) == 0);
   fsync(fileno(f));
   ok = (fclose(f) == 0) && ok;
   if (ok && rename(tmp.c_str(), path.c_str()) != 0)
   {
      // Some filesystems refuse to rename over an existing file: replace it
      unlink(path.c_str());
      ok = (rename(tmp.c_str(), path.c_str()) == 0);
   }
   if (!ok) { std::cerr << "MiSTer: cannot save " << path << ": " << strerror(errno) << std::endl; unlink(tmp.c_str()); return false; }
   sync();
   std::cout << "MiSTer: saved " << val.str() << " in " << path << std::endl;
   return true;
}

//------------------------------------------------------------------------------

void MiSTer::present(void)
{
   // Everything outside the safe area is black
   if (backSurface != NULL && (marginX > 0 || marginY > 0))
   {
      Uint32 black = SDL_MapRGB(backSurface->format, 0, 0, 0);
      SDL_Rect r[4] = { { 0, 0, screenW, marginY }, { 0, screenH - marginY, screenW, marginY },
                        { 0, marginY, marginX, screenH - 2 * marginY }, { screenW - marginX, marginY, marginX, screenH - 2 * marginY } };
      // The software renderer clips the surface to the viewport: lift it while filling
      SDL_Rect clip;
      SDL_GetClipRect(backSurface, &clip);
      SDL_SetClipRect(backSurface, NULL);
      SDL_FillRects(backSurface, r, 4, black);
      SDL_SetClipRect(backSurface, &clip);
   }
   if (screenshotPath != NULL) saveScreenshot();   // debug aid (MF_SCREENSHOT=/tmp/x.ppm)
   if (fbMem == NULL || backSurface == NULL) return;
   const int rowBytes = screenW * backSurface->format->BytesPerPixel;
   uint8_t *dst = fbMem + fbOffset;
   const uint8_t *src = (const uint8_t *)backSurface->pixels;
   for (int y = 0; y < screenH; ++y)
      memcpy(dst + (size_t)y * fbPitch, src + (size_t)y * backSurface->pitch, rowBytes);
}

//------------------------------------------------------------------------------

void MiSTer::pumpInput(void)
{
   // Hotplug: rescan input devices every second
   if (SDL_GetTicks() - lastScan > 1000) { scanEvdev(); lastScan = SDL_GetTicks(); }
   readEvdev();
   if (useJoystick)
   {
      SDL_PumpEvents();
      SDL_Event e;
      while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_JOYAXISMOTION, SDL_CONTROLLERSENSORUPDATE) > 0)
      {
         if (e.type == SDL_JOYDEVICEADDED) openPad(e.jdevice.which);
         else if (e.type == SDL_JOYDEVICEREMOVED) closePad(e.jdevice.which);
      }
      pollPads();
   }
}

//------------------------------------------------------------------------------

bool MiSTer::held(SDL_Scancode sc)
{
   return sc > SDL_SCANCODE_UNKNOWN && sc < SDL_NUM_SCANCODES && holdCount[sc] > 0;
}

//------------------------------------------------------------------------------

void MiSTer::close(void)
{
   if (fbMem != NULL)
   {
      // Clear the screen so the console comes back clean
      for (int y = 0; y < screenH; ++y) memset(fbMem + fbOffset + (size_t)y * fbPitch, 0, fbPitch);
      munmap(fbMem, fbMemSize);
      fbMem = NULL;
   }
   consoleRestore();
   if (ttyFd >= 0) { ::close(ttyFd); ttyFd = -1; }
   if (fbFd >= 0) { ::close(fbFd); fbFd = -1; }
   for (size_t i = 0; i < evdevs.size(); ++i) ::close(evdevs[i].fd);
   evdevs.clear();
   while (!pads.empty()) closePad(pads.back().id);
   renderer = NULL;   // destroyed by SDLUtils::close() before calling us
   if (backSurface) { SDL_FreeSurface(backSurface); backSurface = NULL; }
}

#endif // DEVICE_MISTER
