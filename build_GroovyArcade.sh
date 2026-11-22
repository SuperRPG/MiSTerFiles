#!/usr/bin/env bash
# Full build for GroovyArcade (x86_64 Linux): glibc 2.28 sysroot + static deps (once) + MiSTerFiles
# + dist-groovy/ package + MiSTerFiles-groovyarcade.tar.gz.
# Requires (Debian/Ubuntu build host): g++ cmake make curl dpkg-deb + X11/DRM/EGL/udev -dev headers
# (see build_deps_groovy.sh). Nothing is needed on the GroovyArcade machine.
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
[ -d "$ROOT/deps/sysroot-glibc228/usr/include" ] || "$ROOT/make_sysroot_glibc228.sh"
[ -f "$ROOT/deps/prefix-x86_64/lib/libSDL2.a" ] || "$ROOT/build_deps_groovy.sh"
cd "$ROOT"
make DEVICE=GROOVY clean >/dev/null
make DEVICE=GROOVY -j"$(nproc)" GROOVY_PREFIX="$ROOT/deps/prefix-x86_64" GLIBC_SYSROOT="$ROOT/deps/sysroot-glibc228"
strip MiSTerFiles

# Portability check: highest glibc symbol version and dynamic libraries
MAXGLIBC=$(objdump -T MiSTerFiles | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -n1)
echo "needs $MAXGLIBC; NEEDED: $(readelf -d MiSTerFiles | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')"

DIST="$ROOT/dist-groovy"
APP="$DIST/MiSTerFiles"
rm -rf "$DIST"; mkdir -p "$APP/res" "$APP/licenses" "$APP/attract"
cp MiSTerFiles "$APP/"
cp -r res/24 res/*.ttf "$APP/res/"
cp "$ROOT/launcher/gamecontrollerdb.txt" "$APP/res/"
cp "$ROOT/launcher/groovy/MiSTerFiles.sh" "$ROOT/launcher/groovy/instalar-attract.sh" "$APP/"
cp "$ROOT/launcher/groovy/MiSTerFiles.ini.example" "$APP/"
cp "$ROOT"/launcher/groovy/attract/* "$APP/attract/"
cp "$ROOT"/launcher/licenses/* "$APP/licenses/"
cp README.md "$APP/README.md"
cp "$ROOT/README-GroovyArcade-pt-BR.md" "$DIST/LEIA-ME.md"
cp "$ROOT/README-GroovyArcade-pt-BR.md" "$APP/LEIA-ME.md"
chmod +x "$APP/MiSTerFiles" "$APP/MiSTerFiles.sh" "$APP/instalar-attract.sh"
(cd "$APP" && sha256sum MiSTerFiles > MiSTerFiles.sha256)
# tar keeps the executable bits (a FAT/exFAT USB stick may not: see LEIA-ME, chmod +x)
(cd "$DIST" && tar -czf "$ROOT/MiSTerFiles-groovyarcade.tar.gz" --owner=0 --group=0 MiSTerFiles LEIA-ME.md)
(cd "$DIST" && rm -f "$ROOT/MiSTerFiles-groovyarcade.zip" && zip -qr "$ROOT/MiSTerFiles-groovyarcade.zip" MiSTerFiles LEIA-ME.md)
echo "dist ready: $DIST"
