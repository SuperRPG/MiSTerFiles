// MiSTerFiles: MiSTer FPGA platform layer, also used by the GroovyArcade target (see mister.h)
#if defined(DEVICE_MISTER) || defined(DEVICE_GROOVY)

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
double displayAspect = 4.0 / 3.0;
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
struct Pad { SDL_Joystick *joy; SDL_GameController *gc; SDL_JoystickID id; bool quitHeld; };
std::vector<Pad> pads;

const Uint32 SYNTH_WINDOW_ID = 0x4D460000;   // "MF"

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
   e.key.windowID = SYNTH_WINDOW_ID;   // ours: let it through the GroovyArcade event filter
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

#if defined(DEVICE_GROOVY)
// ------------------------------------------------------ GroovyArcade ----
SDL_Window    *window = NULL;
int            winW = 0, winH = 0;

// Settings from MiSTerFiles.ini (MF_INI); environment variables MF_<KEY> override them
std::map<std::string, std::string> ini;

std::string trim(const std::string &s)
{
   size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
   return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

void loadIni(void)
{
   const char *path = getenv("MF_INI") ? getenv("MF_INI") : "./MiSTerFiles.ini";
   std::ifstream in(path, std::ios::binary);
   if (!in.good()) in.open(getenv("MF_INI_EXAMPLE") ? getenv("MF_INI_EXAMPLE") : "./MiSTerFiles.ini.example", std::ios::binary);
   std::string line;
   while (in.good() && std::getline(in, line))
   {
      line = trim(line);
      if (line.empty() || line[0] == '#' || line[0] == ';') continue;
      size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
      // bash-style quoting ("..." or '...'), drop a trailing # comment outside quotes
      if (!v.empty() && (v[0] == '"' || v[0] == '\''))
      {
         size_t e = v.find(v[0], 1);
         v = v.substr(1, e == std::string::npos ? std::string::npos : e - 1);
      }
      else
      {
         size_t h = v.find(" #");
         if (h != std::string::npos) v = trim(v.substr(0, h));
      }
      ini[k] = v;
   }
}

std::string setting(const char *key, const char *def)
{
   std::string env = std::string("MF_") + key;
   if (getenv(env.c_str())) return getenv(env.c_str());
   std::map<std::string, std::string>::const_iterator it = ini.find(key);
   return it != ini.end() ? it->second : std::string(def);
}

std::vector<std::string> splitList(const std::string &s)
{
   std::vector<std::string> out;
   std::string cur;
   std::istringstream ss(s);
   while (std::getline(ss, cur, ',')) { cur = trim(cur); if (!cur.empty()) out.push_back(cur); }
   return out;
}

// Logical actions -> the keys the UI understands (see def.h)
struct Action { const char *name; SDL_Scancode logical; const char *keys; const char *joy; };
const Action actions[] =
{
   { "UP",    SDL_SCANCODE_UP,       "Up,Keypad 8",                     "" },
   { "DOWN",  SDL_SCANCODE_DOWN,     "Down,Keypad 2",                   "" },
   { "LEFT",  SDL_SCANCODE_LEFT,     "Left,Keypad 4",                   "" },
   { "RIGHT", SDL_SCANCODE_RIGHT,    "Right,Keypad 6",                  "" },
   { "A",     SDL_SCANCODE_RETURN,   "Return,Keypad Enter,Left Ctrl",   "0" },
   { "B",     SDL_SCANCODE_ESCAPE,   "Escape,Backspace,Left Alt",       "1" },
   { "X",     SDL_SCANCODE_SPACE,    "Space,Insert",                    "2,6" },
   { "Y",     SDL_SCANCODE_TAB,      "Tab,Left Shift,F1,Menu",          "3,7" },
   { "L",     SDL_SCANCODE_PAGEUP,   "PageUp,Home",                     "4" },
   { "R",     SDL_SCANCODE_PAGEDOWN, "PageDown,End",                    "5" },
   { "QUIT",  SDL_SCANCODE_UNKNOWN,  "",                                "" },
};
const int NB_ACTIONS = sizeof(actions) / sizeof(actions[0]);
const int ACTION_QUIT = NB_ACTIONS - 1;

std::map<int, int> keyToAction;      // physical SDL scancode -> action index
std::vector<int>   joyButtons[NB_ACTIONS];   // raw joystick button numbers per action

void loadBindings(void)
{
   for (int a = 0; a < NB_ACTIONS; ++a)
   {
      std::string kname = std::string("KEY_") + actions[a].name;
      std::vector<std::string> keys = splitList(setting(kname.c_str(), actions[a].keys));
      std::ostringstream log;
      for (size_t i = 0; i < keys.size(); ++i)
      {
         SDL_Scancode sc = SDL_GetScancodeFromName(keys[i].c_str());
         if (sc == SDL_SCANCODE_UNKNOWN)
         {
            // also accept a number (SDL scancode)
            char *end = NULL;
            long n = strtol(keys[i].c_str(), &end, 10);
            if (end != NULL && *end == '\0' && n > 0 && n < SDL_NUM_SCANCODES && keys[i].size() > 1) sc = (SDL_Scancode)n;
         }
         if (sc == SDL_SCANCODE_UNKNOWN) { std::cerr << "Groovy: " << kname << ": unknown key '" << keys[i] << "'" << std::endl; continue; }
         keyToAction[sc] = a;
         log << " [" << SDL_GetScancodeName(sc) << "]";
      }
      std::string jname = std::string("JOY_") + actions[a].name;
      std::vector<std::string> btn = splitList(setting(jname.c_str(), actions[a].joy));
      for (size_t i = 0; i < btn.size(); ++i) joyButtons[a].push_back(atoi(btn[i].c_str()));
      std::cout << "Groovy: " << actions[a].name << " =" << log.str() << " / joystick buttons " << setting(jname.c_str(), actions[a].joy) << std::endl;
   }
}

std::vector<int> padsAdded;
bool debugInput = false;    // MF_DEBUG_INPUT=1: log every physical key
std::vector<SDL_JoystickID> padsRemoved;

// Physical key from SDL (or the test input file) -> logical key
void physicalKey(SDL_Scancode sc, bool down)
{
   std::map<int, int>::const_iterator it = keyToAction.find(sc);
   if (it == keyToAction.end()) return;
   if (it->second == ACTION_QUIT)
   {
      if (down) { SDL_Event q; SDL_zero(q); q.type = SDL_QUIT; SDL_PushEvent(&q); }
      return;
   }
   // one source per physical key: two keys bound to the same action do not cancel out
   setKey(2000000L + sc, actions[it->second].logical, down);
}

// Real keyboard and joystick events never reach the UI: they are translated here
int SDLCALL eventFilter(void *, SDL_Event *e)
{
   switch (e->type)
   {
      case SDL_KEYDOWN:
      case SDL_KEYUP:
         if (e->key.windowID == SYNTH_WINDOW_ID)
         {
            if (debugInput) std::cout << "Groovy:  -> " << SDL_GetScancodeName(e->key.keysym.scancode) << (e->type == SDL_KEYDOWN ? " down" : " up") << std::endl;
            return 1;
         }
         if (debugInput) std::cout << "Groovy: key " << SDL_GetScancodeName(e->key.keysym.scancode) << (e->type == SDL_KEYDOWN ? " down" : " up") << (e->key.repeat ? " (repeat)" : "") << std::endl;
         if (e->key.repeat == 0) physicalKey(e->key.keysym.scancode, e->type == SDL_KEYDOWN);
         return 0;
      case SDL_TEXTINPUT:
      case SDL_TEXTEDITING:
         return 0;
      case SDL_JOYDEVICEADDED: padsAdded.push_back(e->jdevice.which); return 0;
      case SDL_JOYDEVICEREMOVED: padsRemoved.push_back(e->jdevice.which); return 0;
      default:
         if (e->type >= SDL_JOYAXISMOTION && e->type <= SDL_CONTROLLERSENSORUPDATE) return 0;
         return 1;
   }
}
#endif

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
            #if defined(DEVICE_GROOVY)
            physicalKey(linuxToSdl(ev[k].code), ev[k].value != 0);   // test hook only (MF_INPUT_FILE)
            #else
            setKey(evdevs[i].fd, linuxToSdl(ev[k].code), ev[k].value != 0);
            #endif
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
   Pad p; p.joy = NULL; p.gc = NULL; p.quitHeld = false;
   if (SDL_IsGameController(index))
   {
      p.gc = SDL_GameControllerOpen(index);
      if (p.gc) p.joy = SDL_GameControllerGetJoystick(p.gc);
   }
   if (p.joy == NULL) p.joy = SDL_JoystickOpen(index);
   if (p.joy == NULL) return;
   p.id = SDL_JoystickInstanceID(p.joy);
   for (size_t i = 0; i < pads.size(); ++i)
      if (pads[i].id == p.id)
      {
         // already open (startup scan + its DEVICEADDED event): drop the extra reference
         if (p.gc) SDL_GameControllerClose(p.gc); else SDL_JoystickClose(p.joy);
         return;
      }
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
         #if defined(DEVICE_GROOVY)
         // raw joysticks (arcade encoders in joystick mode): buttons from JOY_A=... in the ini
         bool *dst[6] = { &a, &b, &sel, &menu, &pgu, &pgd };
         for (int k = 0; k < 6; ++k)
            for (size_t n = 0; n < joyButtons[4 + k].size(); ++n)
               if (joyButtons[4 + k][n] < nb && SDL_JoystickGetButton(j, joyButtons[4 + k][n])) *dst[k] = true;
         bool quit = false;
         for (size_t n = 0; n < joyButtons[ACTION_QUIT].size(); ++n)
            if (joyButtons[ACTION_QUIT][n] < nb && SDL_JoystickGetButton(j, joyButtons[ACTION_QUIT][n])) quit = true;
         if (quit && !p.quitHeld) { SDL_Event q; SDL_zero(q); q.type = SDL_QUIT; SDL_PushEvent(&q); }
         p.quitHeld = quit;
         #else
         a    = nb > 0 && SDL_JoystickGetButton(j, 0);
         b    = nb > 1 && SDL_JoystickGetButton(j, 1);
         sel  = (nb > 2 && SDL_JoystickGetButton(j, 2)) || (nb > 6 && SDL_JoystickGetButton(j, 6));
         menu = (nb > 3 && SDL_JoystickGetButton(j, 3)) || (nb > 7 && SDL_JoystickGetButton(j, 7));
         pgu  = nb > 4 && SDL_JoystickGetButton(j, 4);
         pgd  = nb > 5 && SDL_JoystickGetButton(j, 5);
         #endif
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

#if !defined(DEVICE_GROOVY)
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
#endif

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

#if defined(DEVICE_GROOVY)
SDL_Renderer *MiSTer::init(void)
{
   screenshotPath = getenv("MF_SCREENSHOT");
   loadIni();

   // Physical aspect of the TV, for the overscan side margins (ASPECT=4:3, 16:9, 1.333...)
   {
      std::string a = setting("ASPECT", "4:3");
      double n = 0, d = 0;
      if (sscanf(a.c_str(), "%lf:%lf", &n, &d) == 2 && n > 0 && d > 0) displayAspect = n / d;
      else if (atof(a.c_str()) > 0.5) displayAspect = atof(a.c_str());
   }

   // Window: fullscreen on the current mode (what switchres/the frontend set up), or a given mode
   SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
   SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
   SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
   std::cout << "Groovy: SDL video driver " << (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?") << std::endl;
   // X11: copy frames with plain X (XShm), no OpenGL needed. KMSDRM needs SDL's GLES path.
   const char *drv = SDL_GetCurrentVideoDriver();
   if (drv != NULL && strcmp(drv, "x11") == 0 && atoi(setting("X11_OPENGL", "0").c_str()) == 0)
      SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
   SDL_DisplayMode desk;
   if (SDL_GetDesktopDisplayMode(0, &desk) == 0)
      std::cout << "Groovy: desktop mode " << desk.w << "x" << desk.h << " @" << desk.refresh_rate << "Hz" << std::endl;
   else { desk.w = 640; desk.h = 480; desk.refresh_rate = 0; }

   int reqW = 0, reqH = 0;
   std::string mode = setting("VIDEO_MODE", "desktop");    // desktop | WxH
   sscanf(mode.c_str(), "%dx%d", &reqW, &reqH);
   const bool windowed = atoi(setting("WINDOWED", "0").c_str()) != 0;
   Uint32 flags = SDL_WINDOW_SHOWN;
   if (windowed) { winW = reqW > 0 ? reqW : 640; winH = reqH > 0 ? reqH : 480; }
   else if (reqW > 0 && reqH > 0) { winW = reqW; winH = reqH; flags |= SDL_WINDOW_FULLSCREEN; }
   else { winW = desk.w; winH = desk.h; flags |= SDL_WINDOW_FULLSCREEN_DESKTOP; }

   window = SDL_CreateWindow("MiSTerFiles", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, winW, winH, flags);
   if (window == NULL) { std::cerr << "Groovy: window: " << SDL_GetError() << std::endl; return NULL; }
   if ((flags & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN)
   {
      // exact mode switch: closest mode the display offers
      SDL_DisplayMode want = { 0, reqW, reqH, 0, 0 }, got;
      if (SDL_GetClosestDisplayMode(0, &want, &got) != NULL) SDL_SetWindowDisplayMode(window, &got);
   }
   SDL_ShowCursor(SDL_DISABLE);
   SDL_GetWindowSize(window, &winW, &winH);

   // Logical UI size: the window size, or RENDER_SIZE=WxH (then scaled to the window)
   screenW = winW; screenH = winH;
   int rw = 0, rh = 0;
   if (sscanf(setting("RENDER_SIZE", "").c_str(), "%dx%d", &rw, &rh) == 2 && rw >= 160 && rh >= 120) { screenW = rw; screenH = rh; }
   std::cout << "Groovy: window " << winW << "x" << winH << (windowed ? " (windowed)" : " (fullscreen)") << ", UI " << screenW << "x" << screenH << std::endl;

   std::string lr = setting("LOWRES", "auto");
   lowRes = (lr == "auto" || lr.empty()) ? (screenH < 400) : (atoi(lr.c_str()) != 0);

   backSurface = SDL_CreateRGBSurfaceWithFormat(0, screenW, screenH, 32, SDL_PIXELFORMAT_XRGB8888);
   if (backSurface == NULL) { std::cerr << "Groovy: surface: " << SDL_GetError() << std::endl; return NULL; }
   renderer = SDL_CreateSoftwareRenderer(backSurface);
   if (renderer == NULL) { std::cerr << "Groovy: renderer: " << SDL_GetError() << std::endl; return NULL; }
   setOverscan(atoi(setting("OVERSCAN", "0").c_str()));
   std::cout << "Groovy: overscan " << overscan << "% (aspect " << displayAspect << ") -> margins " << marginX << "x" << marginY
             << ", safe area " << (screenW - 2 * marginX) << "x" << (screenH - 2 * marginY) << std::endl;

   signal(SIGINT, signalHandler);
   signal(SIGTERM, signalHandler);
   signal(SIGHUP, signalHandler);

   // Input: keyboard events from SDL + gamepads, translated by the bindings
   loadBindings();
   debugInput = getenv("MF_DEBUG_INPUT") && atoi(getenv("MF_DEBUG_INPUT")) != 0;
   SDL_SetEventFilter(eventFilter, NULL);
   useJoystick = atoi(setting("JOYSTICK", "1").c_str()) != 0;
   if (useJoystick)
   {
      if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0)
      {
         std::string db = setting("GAMECONTROLLERDB", "./res/gamecontrollerdb.txt");
         int n = SDL_GameControllerAddMappingsFromFile(db.c_str());
         if (n > 0) std::cout << "Groovy: loaded " << n << " controller mappings from " << db << std::endl;
         SDL_PumpEvents();
         padsAdded.clear();
         for (int i = 0; i < SDL_NumJoysticks(); ++i) openPad(i);
      }
      else
      {
         std::cerr << "Groovy: SDL gamecontroller init failed: " << SDL_GetError() << std::endl;
         useJoystick = false;
      }
   }
   if (getenv("MF_INPUT_FILE"))
   {
      int fd = open(getenv("MF_INPUT_FILE"), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
      if (fd >= 0) { EvDev d; d.path = getenv("MF_INPUT_FILE"); d.fd = fd; evdevs.push_back(d); }
   }
   lastScan = SDL_GetTicks();
   return renderer;
}
#else
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
#endif

//------------------------------------------------------------------------------

int MiSTer::overscanMarginY(int pct)
{
   if (pct < 0) pct = 0;
   if (pct > OVERSCAN_MAX) pct = OVERSCAN_MAX;
   return (int)lround(screenH * pct / 100.0);
}

int MiSTer::overscanMarginX(int pct)
{
   // Same physical size as the vertical margin on a 4:3 (displayAspect) display:
   // pixel aspect = (4/3) / (W/H)  =>  marginX = marginY * (W/H) * 3/4
   return (int)lround(overscanMarginY(pct) * ((double)screenW / screenH) / displayAspect);
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
   std::cout << "MiSTerFiles: saved " << val.str() << " in " << path << std::endl;
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
   #if defined(DEVICE_GROOVY)
   if (window == NULL || backSurface == NULL) return;
   SDL_Surface *ws = SDL_GetWindowSurface(window);   // re-created by SDL if the window changed
   if (ws == NULL) { static bool told = false; if (!told) { std::cerr << "Groovy: window surface: " << SDL_GetError() << std::endl; told = true; } return; }
   if (ws->w == screenW && ws->h == screenH) SDL_BlitSurface(backSurface, NULL, ws, NULL);
   else SDL_BlitScaled(backSurface, NULL, ws, NULL);   // RENDER_SIZE != window size: nearest-neighbour stretch
   SDL_UpdateWindowSurface(window);
   return;
   #endif
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
   #if defined(DEVICE_GROOVY)
   readEvdev();          // only the MF_INPUT_FILE test hook
   SDL_PumpEvents();     // keyboard/joystick events go through eventFilter
   while (!padsAdded.empty()) { int i = padsAdded.back(); padsAdded.pop_back(); if (useJoystick) openPad(i); }
   while (!padsRemoved.empty()) { SDL_JoystickID id = padsRemoved.back(); padsRemoved.pop_back(); closePad(id); }
   if (useJoystick) pollPads();
   return;
   #endif
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
   #if defined(DEVICE_GROOVY)
   SDL_SetEventFilter(NULL, NULL);
   if (window) { SDL_DestroyWindow(window); window = NULL; }
   #endif
}

#endif // DEVICE_MISTER || DEVICE_GROOVY
