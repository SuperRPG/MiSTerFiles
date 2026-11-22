#!/bin/bash
# Adds MiSTerFiles to Attract-Mode / Attract-Mode Plus (emulator + one-entry romlist), offline.
# Run as the frontend user (arcade):  bash instalar-attract.sh
APPDIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
AM="${1:-$HOME/.attract}"
if [ ! -d "$AM" ]; then echo "Pasta do Attract-Mode nao encontrada: $AM (passe o caminho como argumento)"; exit 1; fi
mkdir -p "$AM/emulators" "$AM/romlists"
sed -e "s|/home/arcade/MiSTerFiles|$APPDIR|g" "$APPDIR/attract/MiSTerFiles.cfg" > "$AM/emulators/MiSTerFiles.cfg"
cp "$APPDIR/attract/MiSTerFiles.txt" "$AM/romlists/MiSTerFiles.txt"
chmod +x "$APPDIR/MiSTerFiles" "$APPDIR/MiSTerFiles.sh"
echo "OK: $AM/emulators/MiSTerFiles.cfg e $AM/romlists/MiSTerFiles.txt"
echo "No Attract-Mode: Tab -> Displays -> Add -> nome 'Utilitarios', Collection/Romlist = MiSTerFiles."
