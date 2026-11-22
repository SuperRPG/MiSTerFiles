#!/usr/bin/env bash
# Full build for MiSTer: static deps (once) + MiSTerFiles + dist/ package.
# Requires (Debian/Ubuntu): g++-arm-linux-gnueabihf cmake make wget
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
[ -f "$ROOT/deps/prefix/lib/libSDL2.a" ] || "$ROOT/build_deps.sh"
cd "$ROOT"
make DEVICE=MISTER clean >/dev/null
make DEVICE=MISTER -j"$(nproc)" MISTER_PREFIX="$ROOT/deps/prefix"
arm-linux-gnueabihf-strip MiSTerFiles

DIST="$ROOT/dist"
APP="$DIST/Scripts/.MiSTerFiles"
rm -rf "$DIST"; mkdir -p "$APP"
cp MiSTerFiles "$APP/"
mkdir -p "$APP/res"
cp -r res/24 res/*.ttf "$APP/res/"
cp "$ROOT/launcher/gamecontrollerdb.txt" "$APP/res/"
cp "$ROOT/launcher/MiSTerFiles.ini" "$APP/MiSTerFiles.ini.example"
cp "$ROOT/launcher/MiSTerFiles.sh" "$DIST/Scripts/"
cp "$ROOT/README-pt-BR.md" "$DIST/LEIA-ME.md"
cp README.md "$APP/README.md"
mkdir -p "$APP/licenses" && cp "$ROOT"/launcher/licenses/* "$APP/licenses/"
chmod +x "$APP/MiSTerFiles" "$DIST/Scripts/MiSTerFiles.sh"
(cd "$DIST" && rm -f ../MiSTerFiles.zip && zip -qr ../MiSTerFiles.zip Scripts LEIA-ME.md)
echo "dist ready: $DIST"
