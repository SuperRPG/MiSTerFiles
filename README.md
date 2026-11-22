# MiSTerFiles

A single panel file manager for the MiSTer FPGA (DE10-Nano), runnable from the
MiSTer Scripts menu and controlled with a gamepad or keyboard.

## Credits / license

MiSTerFiles is a fork of **351Files** by Tardigrade-nx
(https://github.com/Tardigrade-nx/351Files), itself based on DinguxCommander.
It is distributed under the same license as the upstream project (GPLv3).
The original upstream README is kept in `README-upstream.md`.

MiSTer-specific code: `src/mister.cpp`, `src/mister.h`, the `DEVICE_MISTER`
blocks in `src/def.h`, `src/sdlutils.cpp`, `src/window.cpp`, `src/main.cpp`
and the `MISTER` target in the `Makefile`.

Build: see `../build_MiSTer.sh` (`make DEVICE=MISTER`).
