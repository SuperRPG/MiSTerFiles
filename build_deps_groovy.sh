#!/usr/bin/env bash
# Static SDL2 + SDL2_image + SDL2_ttf for the GroovyArcade (x86_64 Linux) target, in deps/prefix-x86_64.
# SDL2 keeps X11 and KMSDRM (+ EGL/GLES, udev, evdev) but loads those system libraries at
# runtime with dlopen, so the binary only needs glibc + whatever the system already has.
# Build deps (Debian): libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev
#   libxss-dev libxinerama-dev libdrm-dev libgbm-dev libegl-dev libgles-dev libgl-dev libudev-dev
set -eu
[ -d "$(cd "$(dirname "$0")" && pwd)/deps/sysroot-glibc228/usr/include" ] || bash "$(cd "$(dirname "$0")" && pwd)/make_sysroot_glibc228.sh"
ROOT="$(cd "$(dirname "$0")" && pwd)"
PREFIX="$ROOT/deps/prefix-x86_64"
rm -rf "$PREFIX"
SRC="$ROOT/deps/src"
J=$(nproc)
mkdir -p "$SRC" "$PREFIX"
cd "$SRC"
rm -rf build-sdl2-x86_64 build-img-x86_64 build-ttf-x86_64
get() { [ -f "$(basename "$1")" ] || wget -q "$1"; d="$(basename "$1" .tar.gz)"; [ -d "$d" ] || tar xzf "$(basename "$1")"; }
get https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz
get https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.4/SDL2_image-2.8.4.tar.gz
get https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-2.24.0.tar.gz
# pkg-config results (libdrm, gbm, egl) point into the sysroot, where those headers are symlinked
export PKG_CONFIG_SYSROOT_DIR="$ROOT/deps/sysroot-glibc228"
common=(-DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/groovy-x86_64.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX" -DBUILD_SHARED_LIBS=OFF)

# (rpath-link: lets the configure checks link against the host libX11 & co., the result does not)
cmake -S SDL2-2.30.11 -B build-sdl2-x86_64 "${common[@]}" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,/usr/lib/x86_64-linux-gnu" -DX_INCLUDEDIR="$ROOT/deps/sysroot-glibc228/usr/include" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
  -DSDL_AUDIO=OFF -DSDL_X11=ON -DSDL_X11_SHARED=ON -DSDL_WAYLAND=OFF -DSDL_KMSDRM=ON -DSDL_KMSDRM_SHARED=ON \
  -DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_VULKAN=OFF -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF -DSDL_VIVANTE=OFF \
  -DSDL_OFFSCREEN=ON -DSDL_DUMMYVIDEO=ON -DSDL_LIBUDEV=ON -DSDL_DBUS=OFF -DSDL_IBUS=OFF \
  -DSDL_HIDAPI=ON -DSDL_HIDAPI_LIBUSB=OFF -DSDL_PIPEWIRE=OFF -DSDL_PULSEAUDIO=OFF -DSDL_ALSA=OFF -DSDL_JACK=OFF \
  -DSDL_SNDIO=OFF -DSDL_LIBSAMPLERATE=OFF -DSDL_JOYSTICK=ON -DSDL_HAPTIC=ON -DSDL_RENDER=ON
cmake --build build-sdl2-x86_64 -j"$J"
cmake --install build-sdl2-x86_64

cmake -S SDL2_image-2.8.4 -B build-img-x86_64 "${common[@]}" -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF \
  -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_BACKEND_STB=ON -DSDL2IMAGE_PNG=ON -DSDL2IMAGE_JPG=ON \
  -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF -DSDL2IMAGE_DEPS_SHARED=OFF \
  -DSDL2IMAGE_BMP=ON -DSDL2IMAGE_GIF=ON -DSDL2IMAGE_TGA=ON -DSDL2IMAGE_PCX=ON -DSDL2IMAGE_SVG=OFF -DSDL2IMAGE_QOI=ON
cmake --build build-img-x86_64 -j"$J"
cmake --install build-img-x86_64

cmake -S SDL2_ttf-2.24.0 -B build-ttf-x86_64 "${common[@]}" -DSDL2TTF_SAMPLES=OFF -DSDL2TTF_VENDORED=ON \
  -DSDL2TTF_HARFBUZZ=OFF -DSDL2TTF_FREETYPE=ON
cmake --build build-ttf-x86_64 -j"$J"
cmake --install build-ttf-x86_64
rm -rf build-sdl2-x86_64 build-img-x86_64 build-ttf-x86_64
echo "deps OK -> $PREFIX"
