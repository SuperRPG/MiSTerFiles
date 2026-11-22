TARGET = MiSTerFiles

# DEVICE ?= RG351P
# DEVICE ?= RG351V
# DEVICE ?= RG351MP
# DEVICE ?= RGB10
# DEVICE ?= RK2020
# DEVICE ?= CHI
# DEVICE ?= MISTER   (see build_MiSTer.sh)
# DEVICE ?= GROOVY   (GroovyArcade / x86_64 Linux, see build_GroovyArcade.sh)
DEVICE ?= PC

START_PATH ?= "/"
RES_PATH ?= "./res"

ifeq ($(DEVICE),PC)
	CC = $(CXX)
	SDL2_CONFIG = sdl2-config
	START_PATH = $(PWD)
endif

ifeq ($(DEVICE),MISTER)
	# Cross-compiled, fully static (no dependency on the MiSTer rootfs glibc/SDL)
	MISTER_PREFIX ?= $(CURDIR)/../deps/prefix
	CC = arm-linux-gnueabihf-g++
	STRIP = arm-linux-gnueabihf-strip
	SDL2_CONFIG = $(MISTER_PREFIX)/bin/sdl2-config
	START_PATH = /media/fat
	EXTRA_CFLAGS = -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -O2 -I$(MISTER_PREFIX)/include/SDL2
	EXTRA_LDFLAGS = -static -L$(MISTER_PREFIX)/lib -lSDL2_image -lSDL2_ttf -lfreetype $(MISTER_PREFIX)/lib/libSDL2.a -lpthread -lm -ldl -lrt
	EXTRA_CFLAGS += -DMF_PLATFORM
endif

ifeq ($(DEVICE),GROOVY)
	# x86_64, built against a glibc 2.28 sysroot (runs on any distro with glibc >= 2.28).
	# SDL2/SDL2_image/SDL2_ttf/FreeType are static; libstdc++/libgcc are static; the only
	# dynamic libraries are glibc's (libc, libm, libdl, libpthread, librt). SDL dlopen()s
	# X11 or DRM/GBM/EGL/GLES and udev from the system at runtime.
	GROOVY_PREFIX ?= $(CURDIR)/../deps/prefix-x86_64
	GLIBC_SYSROOT ?= $(CURDIR)/../deps/sysroot-glibc228
	CC = g++
	CCC = gcc
	STRIP = strip
	SDL2_CONFIG = $(GROOVY_PREFIX)/bin/sdl2-config
	START_PATH = /
	SYSROOT_FLAGS = --sysroot=$(GLIBC_SYSROOT)
	EXTRA_CFLAGS = $(SYSROOT_FLAGS) -march=x86-64 -mtune=generic -O2 -I$(GROOVY_PREFIX)/include/SDL2 -DMF_PLATFORM
	EXTRA_LDFLAGS = $(SYSROOT_FLAGS) -B$(GLIBC_SYSROOT)/usr/lib/x86_64-linux-gnu/ -L$(GLIBC_SYSROOT)/usr/lib/x86_64-linux-gnu -L$(GLIBC_SYSROOT)/lib/x86_64-linux-gnu \
		-static-libstdc++ -static-libgcc -L$(GROOVY_PREFIX)/lib -lSDL2_image -lSDL2_ttf -lfreetype $(GROOVY_PREFIX)/lib/libSDL2.a \
		-Wl,--push-state,--no-as-needed -lpthread -lm -ldl -lrt -Wl,--pop-state -Wl,--gc-sections -Wl,-z,noexecstack
	COMPAT_OBJ = src/compat/glibc_compat.o
endif

SRC = $(wildcard src/*.cpp)
OBJ = $(patsubst %cpp,%o,$(SRC))
COMPILER_FLAGS =  $(shell $(SDL2_CONFIG) --cflags) -Wall -pedantic -Wfatal-errors -DDEVICE_$(DEVICE) -DSTART_PATH=\"$(START_PATH)\" -DRES_PATH=\"$(RES_PATH)\" $(EXTRA_CFLAGS)
ifneq ($(filter MISTER GROOVY,$(DEVICE)),)
LINKER_FLAGS = $(EXTRA_LDFLAGS)
else
LINKER_FLAGS = $(shell $(SDL2_CONFIG) --libs) -lSDL2_image -lSDL2_ttf
endif

all : $(OBJ) $(COMPAT_OBJ)
	$(CC) $(OBJ) $(COMPAT_OBJ) -o $(TARGET) $(LINKER_FLAGS)

%.o:%.cpp
	$(CC) -c $< -o $@ $(COMPILER_FLAGS)

src/compat/glibc_compat.o : src/compat/glibc_compat.c
	$(CCC) -c $< -o $@ $(SYSROOT_FLAGS) -O2 -Wall

clean :
	rm -f $(OBJ) $(TARGET) src/compat/*.o
