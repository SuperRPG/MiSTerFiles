# MiSTerFiles

A single panel file manager for the MiSTer FPGA (DE10-Nano), runnable from the
MiSTer Scripts menu and controlled with a gamepad or keyboard. Also builds for
GroovyArcade (x86_64 Linux arcade cabinets).

## Credits / license

MiSTerFiles is a fork of **351Files** by Tardigrade-nx
(https://github.com/Tardigrade-nx/351Files), itself based on DinguxCommander.
It is distributed under the same license as the upstream project (GPLv3).
The original upstream README is kept in `README-upstream.md`.

MiSTer-specific code: `src/mister.cpp`, `src/mister.h`, the `DEVICE_MISTER`
blocks in `src/def.h`, `src/sdlutils.cpp`, `src/window.cpp`, `src/main.cpp`
and the `MISTER` target in the `Makefile`.

Build: see `../build_MiSTer.sh` (`make DEVICE=MISTER`).

GroovyArcade / x86_64 Linux target: `make DEVICE=GROOVY` (see `../build_GroovyArcade.sh`).
It shares the platform layer (`src/mister.cpp`, `DEVICE_GROOVY` blocks: SDL window on
KMSDRM/X11, SDL keyboard + gamepads with configurable bindings) and links against a
glibc 2.28 sysroot (`../make_sysroot_glibc228.sh`) with SDL2/SDL2_image/SDL2_ttf/FreeType,
libstdc++ and libgcc static; `src/compat/glibc_compat.c` provides the few newer glibc
symbols the toolchain's static libstdc++/libgcc reference. Code shared by both targets is
under `MF_PLATFORM`.
