#!/usr/bin/env bash
# Creates deps/sysroot-glibc228: Debian 10 (buster) glibc 2.28 + kernel headers, used to build the
# GroovyArcade binary so it only needs glibc >= 2.28 at runtime (any distro since 2018).
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
SR="$ROOT/deps/sysroot-glibc228"
A=https://archive.debian.org/debian/pool/main
DEBS="$ROOT/deps/src/debs-glibc228"     # kept, so a rebuild works offline
rm -rf "$SR" && mkdir -p "$SR" "$DEBS"
cd "$DEBS"
for p in g/glibc/libc6_2.28-10+deb10u1_amd64.deb g/glibc/libc6-dev_2.28-10+deb10u1_amd64.deb l/linux/linux-libc-dev_4.19.249-2_amd64.deb; do
   [ -f "$(basename "$p")" ] || curl -sfLO "$A/$p"
done
cd "$SR"
for d in "$DEBS"/*.deb; do dpkg-deb -x "$d" .; done
# absolute symlinks -> relative (inside the sysroot)
find . -type l -lname '/*' | while read -r l; do t=$(readlink "$l"); ln -sfn "$(realpath -m --relative-to="$(dirname "$l")" "$SR$t")" "$l"; done
# the compiler's own C++ headers
mkdir -p usr/include/x86_64-linux-gnu
ln -sfn /usr/include/c++ usr/include/c++
ln -sfn /usr/include/x86_64-linux-gnu/c++ usr/include/x86_64-linux-gnu/c++
# headers of the system libraries SDL dlopen()s at runtime (X11, GL/EGL/GLES, DRM/GBM, udev):
# taken from the build host, linked in so the compiler never searches the host /usr/include
for h in X11 xcb GL GLES GLES2 GLES3 EGL KHR libdrm gbm.h xf86drm.h xf86drmMode.h libudev.h; do
   [ -e "/usr/include/$h" ] && ln -sfn "/usr/include/$h" "usr/include/$h"
done
echo "sysroot OK -> $SR"
