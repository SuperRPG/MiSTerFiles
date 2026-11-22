#!/usr/bin/env bash
# Builds static SDL2, SDL2_image (stb backend: PNG/JPG, no external libs) and
# SDL2_ttf (vendored FreeType, no HarfBuzz) for MiSTer (armhf) into deps/prefix.
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
export MISTER_PREFIX="$ROOT/deps/prefix"
TC="$ROOT/cmake/mister-armhf.cmake"
SRC="$ROOT/deps/src"
J=$(nproc)
mkdir -p "$SRC" "$MISTER_PREFIX"
cd "$SRC"
get() { [ -f "$(basename "$1")" ] || wget -q "$1"; d="$(basename "$1" .tar.gz)"; [ -d "$d" ] || tar xzf "$(basename "$1")"; }
get https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz
get https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.4/SDL2_image-2.8.4.tar.gz
get https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-2.24.0.tar.gz

common=(-DCMAKE_TOOLCHAIN_FILE="$TC" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$MISTER_PREFIX" -DCMAKE_PREFIX_PATH="$MISTER_PREFIX" -DBUILD_SHARED_LIBS=OFF)

# SDL2: no X11/Wayland/KMSDRM/GL, no audio; keep events, joystick (evdev), gamecontroller, software renderer, dummy video
cmake -S SDL2-2.30.11 -B build-sdl2 "${common[@]}" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF \
  -DSDL_AUDIO=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF \
  -DSDL_VULKAN=OFF -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF -DSDL_VIVANTE=OFF -DSDL_OFFSCREEN=ON -DSDL_DUMMYVIDEO=ON \
  -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_HIDAPI=OFF -DSDL_HIDAPI_LIBUSB=OFF \
  -DSDL_PIPEWIRE=OFF -DSDL_PULSEAUDIO=OFF -DSDL_ALSA=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_SAMPLERATE=OFF \
  -DSDL_JOYSTICK=ON -DSDL_HAPTIC=ON -DSDL_SENSOR=OFF -DSDL_POWER=OFF -DSDL_RENDER=ON -DSDL_LIBSAMPLERATE=OFF
cmake --build build-sdl2 -j"$J" && cmake --install build-sdl2

cmake -S SDL2_image-2.8.4 -B build-img "${common[@]}" -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF \
  -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_BACKEND_STB=ON -DSDL2IMAGE_PNG=ON -DSDL2IMAGE_JPG=ON \
  -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF -DSDL2IMAGE_DEPS_SHARED=OFF \
  -DSDL2IMAGE_BMP=ON -DSDL2IMAGE_GIF=ON -DSDL2IMAGE_TGA=ON -DSDL2IMAGE_PCX=ON -DSDL2IMAGE_SVG=OFF -DSDL2IMAGE_QOI=ON
cmake --build build-img -j"$J" && cmake --install build-img

cmake -S SDL2_ttf-2.24.0 -B build-ttf "${common[@]}" -DSDL2TTF_SAMPLES=OFF -DSDL2TTF_VENDORED=ON \
  -DSDL2TTF_HARFBUZZ=OFF -DSDL2TTF_FREETYPE=ON
cmake --build build-ttf -j"$J" && cmake --install build-ttf
echo "deps OK -> $MISTER_PREFIX"
