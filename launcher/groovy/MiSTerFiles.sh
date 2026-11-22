#!/bin/bash
# MiSTerFiles launcher for GroovyArcade (x86_64 Linux): file manager controlled by joystick/arcade
# controls. Fork of 351Files (https://github.com/Tardigrade-nx/351Files).
# Usage: MiSTerFiles.sh [start directory]
# Runs from wherever the folder was copied (e.g. /home/arcade/MiSTerFiles), entirely offline.

APPDIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
BIN="$APPDIR/MiSTerFiles"
LOG="${MF_LOG:-/tmp/MiSTerFiles.log}"
: >"$LOG" 2>/dev/null || LOG=/dev/null
log() { echo "[$(date '+%H:%M:%S')] $*" >>"$LOG"; }
log "MiSTerFiles (GroovyArcade) launcher start: $0 $*"

# Settings file: next to the program if the folder is writable, else ~/.config/MiSTerFiles/
INI="$APPDIR/MiSTerFiles.ini"
if [ ! -w "$APPDIR" ]; then
   INI="${XDG_CONFIG_HOME:-$HOME/.config}/MiSTerFiles/MiSTerFiles.ini"
   mkdir -p "$(dirname "$INI")" 2>/dev/null
fi
[ -f "$INI" ] || cp "$APPDIR/MiSTerFiles.ini.example" "$INI" 2>/dev/null

# Defaults (MiSTerFiles.ini overrides; the program itself reads the other keys)
START_DIR="$HOME"
VIDEO_DRIVER=auto
# (strip Windows line endings in case the file was edited on a PC)
[ -f "$INI" ] && . <(tr -d '\r' < "$INI") && log "loaded $INI"

[ -n "$1" ] && START_DIR="$1"
[ -d "$START_DIR" ] || START_DIR="$HOME"
[ -d "$START_DIR" ] || START_DIR=/
START_DIR="$(cd "$START_DIR" && pwd)"

# Video: X11 when a display is there (frontend under X), else KMS/DRM straight on the console
case "$VIDEO_DRIVER" in
   ''|auto) if [ -n "$DISPLAY" ]; then VIDEO_DRIVER=x11; else VIDEO_DRIVER=kmsdrm; fi ;;
esac
export SDL_VIDEODRIVER="$VIDEO_DRIVER"

log "system: $(uname -srm); $(ldd --version 2>/dev/null | head -n1)"
log "user: $(id 2>/dev/null)"
log "DISPLAY='$DISPLAY' XDG_SESSION_TYPE='$XDG_SESSION_TYPE' SDL_VIDEODRIVER=$SDL_VIDEODRIVER"
log "dri: $(ls /dev/dri 2>/dev/null | tr '\n' ' ')  input: $(ls /dev/input 2>/dev/null | tr '\n' ' ')"
[ -n "$DISPLAY" ] && command -v xrandr >/dev/null && log "xrandr: $(xrandr --current 2>/dev/null | grep -E '\*|connected' | tr -s ' ' | tr '\n' ';')"
log "ini: $INI  START_DIR=$START_DIR"

if [ ! -x "$BIN" ]; then
   chmod +x "$BIN" 2>/dev/null
fi
MF_INI="$INI" MF_INI_EXAMPLE="$APPDIR/MiSTerFiles.ini.example" "$BIN" "$START_DIR" >>"$LOG" 2>&1
rc=$?
log "MiSTerFiles exited with code $rc"
# X11 fallback: if KMS/DRM could not be opened (e.g. the frontend still holds the screen), say so in the log
[ $rc -ne 0 ] && log "if it did not open, see LEIA-ME (VIDEO_DRIVER=x11 / kmsdrm in MiSTerFiles.ini)"
exit $rc
