TARGET = MiSTerFiles

# DEVICE ?= RG351P
# DEVICE ?= RG351V
# DEVICE ?= RG351MP
# DEVICE ?= RGB10
# DEVICE ?= RK2020
# DEVICE ?= CHI
# DEVICE ?= MISTER   (see build_MiSTer.sh)
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
endif

SRC = $(wildcard src/*.cpp)
OBJ = $(patsubst %cpp,%o,$(SRC))
COMPILER_FLAGS =  $(shell $(SDL2_CONFIG) --cflags) -Wall -pedantic -Wfatal-errors -DDEVICE_$(DEVICE) -DSTART_PATH=\"$(START_PATH)\" -DRES_PATH=\"$(RES_PATH)\" $(EXTRA_CFLAGS)
ifeq ($(DEVICE),MISTER)
LINKER_FLAGS = $(EXTRA_LDFLAGS)
else
LINKER_FLAGS = $(shell $(SDL2_CONFIG) --libs) -lSDL2_image -lSDL2_ttf
endif

all : $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LINKER_FLAGS)

%.o:%.cpp
	$(CC) -c $< -o $@ $(COMPILER_FLAGS)

clean :
	rm -f $(OBJ) $(TARGET)
