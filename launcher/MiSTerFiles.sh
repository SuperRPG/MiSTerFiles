#!/bin/bash
# MiSTerFiles - file manager for MiSTer FPGA (fork of 351Files by Tardigrade-nx)
# Install: this script in /media/fat/Scripts, the app in /media/fat/Scripts/.MiSTerFiles
#
# How the display works (Main_MiSTer):
#  - fb_terminal=1/2: Main runs scripts on the Linux framebuffer (tty2) -> the app can draw.
#    fb_terminal=2 + vga_scaler=0 + direct_video=0 = framebuffer sized for 15 kHz CRT (640x240).
#  - fb_terminal=0: Main runs scripts with their text output in the OSD and never enables
#    the framebuffer; /dev/MiSTer_cmd has no command to enable it. In that case (AUTO_FB=1)
#    this script mounts a temporary copy of MiSTer.ini with fb_terminal set (the file on the
#    SD card is NOT changed), reloads the menu, and asks you to start MiSTerFiles again.
#    On exit the temporary ini is removed and the menu reloads with your original settings.

T="${MF_TEST_ROOT:-}"   # off-target testing only
LOG=$T/tmp/MiSTerFiles.log
[ -f "$LOG" ] && mv -f "$LOG" "$LOG.old" 2>/dev/null
log() { echo "$(date '+%F %T') $*" >> "$LOG"; }
say() { echo "$*"; log "say: $*"; }
log "=== MiSTerFiles launcher start (pid $$, script $0)"

APPDIR="$T/media/fat/Scripts/.MiSTerFiles"
BIN="$APPDIR/MiSTerFiles"
INI="$T/media/fat/MiSTer.ini"
TMPINI="$T/tmp/MiSTerFiles-MiSTer.ini"
MARK="$T/tmp/MiSTerFiles.tmpini"
MENU_RBF="$T/media/fat/menu.rbf"
CMD="$T/dev/MiSTer_cmd"

# ---- defaults (override in $APPDIR/MiSTerFiles.ini, plain bash "KEY=value" lines) ----
FB_WIDTH=""           # empty = automatic (CRT 15 kHz: keep Main's 640x240; HDMI/scaler: 640x480)
FB_HEIGHT=""
LAYOUT=auto           # auto | small (240-line layout) | large (480-line layout)
START_DIR="/media/fat"
AUTO_FB=1             # 1 = if fb_terminal=0, use a temporary ini (see above); 0 = only show help
MF_JOYSTICK=0         # 1 = also read gamepads directly through SDL
MF_SWAP_RB=0          # 1 = swap red/blue if colors look wrong
OVERSCAN=0            # safe-area margin, % of screen height on all 4 sides (0-15); set in-app: Y/Tab -> Tela / Overscan
# (strip Windows line endings in case the file was edited on a PC)
[ -f "$APPDIR/MiSTerFiles.ini" ] && . <(tr -d '\r' < "$APPDIR/MiSTerFiles.ini") && log "loaded $APPDIR/MiSTerFiles.ini"

# ---- read MiSTer.ini: [MiSTer] value, overridden by [Menu] (we only run in the menu core) ----
ini_get() {   # $1=key $2=default
   local v
   v=$(awk -v key="$1" '
      { sub(/\r$/, ""); line=$0; sub(/[;#].*$/, "", line) }
      line ~ /^[ \t]*\[/ { s=line; sub(/^[ \t]*\[/, "", s); sub(/\].*$/, "", s); gsub(/[ \t]/, "", s); sec=tolower(s); next }
      { split(line, kv, "="); k=kv[1]; gsub(/[ \t]/, "", k)
        if (tolower(k) == key) { v=substr(line, index(line, "=") + 1); gsub(/^[ \t]+|[ \t]+$/, "", v)
          if (sec == "mister") m=v; else if (sec == "menu") n=v } }
      END { if (n != "") print n; else print m }' "$INI" 2>/dev/null)
   echo "${v:-$2}"
}
FB_TERMINAL=$(ini_get fb_terminal 1)
VGA_SCALER=$(ini_get vga_scaler 0)
DIRECT_VIDEO=$(ini_get direct_video 0)
FORCED_SD=$(ini_get forced_scandoubler 0)
MENU_PAL=$(ini_get menu_pal 0)
VGA_MODE=$(ini_get vga_mode "")
VIDEO_MODE=$(ini_get video_mode "")
FB_SIZE=$(ini_get fb_size 0)
INI_MOUNTED=0; grep -q " $INI " /proc/mounts 2>/dev/null && INI_MOUNTED=1
ON_FB=0; [ -t 1 ] && ON_FB=1
log "ini: fb_terminal=$FB_TERMINAL vga_scaler=$VGA_SCALER direct_video=$DIRECT_VIDEO forced_scandoubler=$FORCED_SD menu_pal=$MENU_PAL vga_mode=$VGA_MODE video_mode=$VIDEO_MODE fb_size=$FB_SIZE"
log "state: running_on_framebuffer=$ON_FB (stdout $(readlink /proc/$$/fd/1)) temp_ini_mounted=$INI_MOUNTED marker=$([ -f $MARK ] && echo yes || echo no)"
log "fb: mode='$(cat $T/sys/module/MiSTer_fb/parameters/mode 2>/dev/null)' virtual_size='$(cat $T/sys/class/graphics/fb0/virtual_size 2>/dev/null)' bpp='$(cat $T/sys/class/graphics/fb0/bits_per_pixel 2>/dev/null)'"
case "$OVERSCAN" in ''|*[!0-9]*) log "invalid OVERSCAN='$OVERSCAN', using 0"; OVERSCAN=0 ;; esac
log "settings: OVERSCAN=$OVERSCAN FB_WIDTH=$FB_WIDTH FB_HEIGHT=$FB_HEIGHT LAYOUT=$LAYOUT START_DIR=$START_DIR AUTO_FB=$AUTO_FB MF_JOYSTICK=$MF_JOYSTICK MF_SWAP_RB=$MF_SWAP_RB"

# fb_terminal value that gives a visible framebuffer: 2 on analog 15 kHz (no scaler), else 1
WANT_FBT=1
[ "$VGA_SCALER" = "0" ] && [ "$DIRECT_VIDEO" = "0" ] && WANT_FBT=2

manual_help() {
   say "MiSTerFiles precisa do"
   say "framebuffer do Linux."
   say "Edite /media/fat/MiSTer.ini"
   say "na secao [MiSTer]:"
   say "   fb_terminal=$WANT_FBT"
   [ "$WANT_FBT" = "2" ] && say "(2 = tela de TV/CRT 15kHz)"
   say "Depois reinicie o MiSTer."
}

# ======================= not on the framebuffer (fb_terminal=0) =======================
if [ "$ON_FB" = "0" ]; then
   log "not running on the framebuffer: script output goes to the OSD"
   if [ "$AUTO_FB" = "1" ] && [ "$INI_MOUNTED" = "0" ] && [ -f "$INI" ] && [ -f "$MENU_RBF" ]; then
      # Temporary copy of MiSTer.ini with fb_terminal forced in [MiSTer] and every section that sets it
      awk -v val="$WANT_FBT" '
         { raw=$0; cr=""; if (raw ~ /\r$/) { cr="\r"; sub(/\r$/, "", raw) } }
         raw ~ /^[ \t]*\[/ { s=raw; sub(/^[ \t]*\[/, "", s); sub(/\].*$/, "", s); gsub(/[ \t]/, "", s); s=tolower(s); print raw cr
                             if (s == "mister" || s == "menu") { print "fb_terminal=" val cr; if (s == "mister") seen=1 }; next }
         { k=raw; sub(/[;#].*$/, "", k); sub(/=.*/, "", k); gsub(/[ \t]/, "", k); if (tolower(k) == "fb_terminal") next; print raw cr }
         END { if (!seen) { print "[MiSTer]"; print "fb_terminal=" val } }' "$INI" > "$TMPINI"
      if [ -s "$TMPINI" ] && mount --bind "$TMPINI" "$INI" 2>>"$LOG"; then
         touch "$MARK"
         log "temporary ini mounted with fb_terminal=$WANT_FBT; reloading menu core"
         say "MiSTerFiles precisa do"
         say "framebuffer (fb_terminal=0)."
         say ""
         say "Ativando temporariamente"
         say "fb_terminal=$WANT_FBT (o MiSTer.ini"
         say "nao e alterado)."
         say ""
         say "O menu vai recarregar."
         say "Depois abra MiSTerFiles"
         say "de novo em Scripts."
         sleep 6
         exec >>"$LOG" 2>&1      # Main restarts: nobody reads the OSD pipe any more
         echo "load_core $MENU_RBF" > "$CMD"
         log "load_core sent"
         exit 0
      fi
      log "could not mount temporary ini"
      rm -f "$TMPINI"
   elif [ "$INI_MOUNTED" = "1" ]; then
      log "temporary ini already mounted but script still not on framebuffer (alternative ini in use?)"
      umount "$INI" 2>>"$LOG"; rm -f "$MARK" "$TMPINI"
   fi
   manual_help
   exit 0
fi

# ======================= on the framebuffer =======================
if [ ! -x "$BIN" ]; then
   chmod +x "$BIN" 2>/dev/null
   [ -x "$BIN" ] || { say "MiSTerFiles not found at $BIN"; exit 1; }
fi
echo "MiSTerFiles" > /tmp/CORENAME 2>/dev/null   # tty2oled / i2c2oled

CRT=0
[ "$FB_TERMINAL" = "2" ] && [ "$VGA_SCALER" = "0" ] && [ "$DIRECT_VIDEO" = "0" ] && CRT=1
[ "$DIRECT_VIDEO" != "0" ] && CRT=1
log "analog 15kHz/CRT framebuffer path: $CRT"

if [ -z "$FB_WIDTH" ] || [ -z "$FB_HEIGHT" ]; then
   if [ "$CRT" = "1" ]; then FB_WIDTH=""; FB_HEIGHT=""; else FB_WIDTH=640; FB_HEIGHT=480; fi
fi

case "$LAYOUT" in
   small) export MF_LOWRES=1 ;;
   large) export MF_LOWRES=0 ;;
   *) [ "$CRT" = "1" ] && [ "$FORCED_SD" = "0" ] && export MF_LOWRES=1 ;;   # else: app decides from fb height
esac
log "layout: LAYOUT=$LAYOUT MF_LOWRES=${MF_LOWRES:-auto}"

OLD_MODE="$(cat $T/sys/module/MiSTer_fb/parameters/mode 2>/dev/null)"
CHANGED_FB=0
set_fb() {   # $1=fmt $2=rb $3=width $4=height
   log "set framebuffer: $1 $2 $3x$4"
   if [ -x /usr/sbin/vmode ] && [ "$1" = "8888" ]; then
      /usr/sbin/vmode -r "$3" "$4" rgb32 >>"$LOG" 2>&1 || echo "fb_cmd1 $1 $2 $3 $4" > "$CMD"
   else
      echo "fb_cmd1 $1 $2 $3 $4" > "$CMD"
   fi
}

restore() {
   if [ "$CHANGED_FB" = "1" ] && [ -n "$OLD_MODE" ]; then
      set -- $OLD_MODE
      [ -n "$4" ] && set_fb "$1" "$2" "$3" "$4"
   fi
   echo "MENU" > /tmp/CORENAME 2>/dev/null
   if [ -f "$MARK" ]; then
      # We enabled the framebuffer with a temporary ini: put the original back and reload the menu
      umount "$INI" 2>>"$LOG"; rm -f "$MARK" "$TMPINI"
      log "temporary ini removed; reloading menu with the original MiSTer.ini"
      setsid sh -c 'sleep 2; echo "load_core $1" > "$2"' sh "$MENU_RBF" "$CMD" </dev/null >/dev/null 2>&1 &
   fi
   clear 2>/dev/null
   log "=== launcher end"
}
trap restore EXIT

if [ -n "$FB_WIDTH" ] && [ -n "$FB_HEIGHT" ]; then
   set_fb 8888 1 "$FB_WIDTH" "$FB_HEIGHT"; CHANGED_FB=1
   for i in $(seq 1 20); do   # Main applies the mode asynchronously
      case "$(cat $T/sys/module/MiSTer_fb/parameters/mode 2>/dev/null)" in *" $FB_WIDTH $FB_HEIGHT "*) break ;; esac
      sleep 0.1
   done
else
   log "keeping Main's console framebuffer as is"
fi
log "fb now: mode='$(cat $T/sys/module/MiSTer_fb/parameters/mode 2>/dev/null)' virtual_size='$(cat $T/sys/class/graphics/fb0/virtual_size 2>/dev/null)'"

cd "$APPDIR" || exit 1
log "starting $BIN $START_DIR"
MF_JOYSTICK="$MF_JOYSTICK" MF_SWAP_RB="$MF_SWAP_RB" MF_OVERSCAN="$OVERSCAN" \
   MF_INI="$APPDIR/MiSTerFiles.ini" MF_INI_EXAMPLE="$APPDIR/MiSTerFiles.ini.example" \
   HOME=/root "$BIN" "$START_DIR" >>"$LOG" 2>&1
RC=$?
log "app exit code $RC"
[ $RC -ne 0 ] && { echo "MiSTerFiles exited with code $RC, see $LOG"; tail -n 5 "$LOG"; }
exit 0
