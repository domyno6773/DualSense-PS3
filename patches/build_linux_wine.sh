#!/bin/bash
# Uso: ds45_build.sh <cartella_sorgenti> <vsh|game> <file_output.sprx>
# Replica build.sh di DS45Pad su Linux: compilatore Cell SDK sotto Wine + scetool nativo.
set -e
SRCDIR="$1"; MODE="$2"; OUT="$3"
SDK=/tmp/ps3sdk
SCEDIR=/tmp/claude-0/-home-claude/eb99eedb-7192-5b41-a036-a3ef28189405/scratchpad/scetool_run
export WINEPREFIX=/tmp/claude-0/-home-claude/eb99eedb-7192-5b41-a036-a3ef28189405/scratchpad/wineprefix
export WINEARCH=win32 WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" DISPLAY=
WINE=/usr/lib/wine/wine
GCC="$SDK/host-win32/ppu/bin/ppu-lv2-gcc.exe"
PRXSTRIP="$SDK/host-win32/bin/ppu-lv2-prx-strip.exe"
INCLUDE="$SDK/target/ppu/include"; LIBDIR="$SDK/target/ppu/lib"

AUTH_VSH=1070000052000001; AUTH_GAME=1010000001000003
if [ "$MODE" = "game" ]; then
  EXTRA="-DDS45_GAME"; AUTH=$AUTH_GAME
  SRCS="libc.c util.c hook.c config.c sticks.c diag.c main.c"; LDLIBS="-lio_stub -lfs_stub"
else
  EXTRA=""; AUTH=$AUTH_VSH
  SRCS="libc.c util.c hook.c config.c ds4.c dualsense.c sticks.c padldd.c usb.c passthru.c diag.c main.c"
  LDLIBS="-lusbd_stub -lio_stub -lfs_stub -llv2_stub"
fi
CFLAGS="-include compat_pad.h -mprx -Os -ffunction-sections -fdata-sections -fno-builtin-printf -nodefaultlibs -std=gnu99 -Wall -Wno-unused-parameter -I $INCLUDE $EXTRA"
LDFLAGS="-mprx -zgenprx -zgenstub -ffunction-sections -fdata-sections"
SCEFLAGS="-0 SELF -1 TRUE -s FALSE -2 0A -3 $AUTH -4 01000002 -5 APP -6 0003004000000000 -A 0001000000000000"

cd "$SRCDIR"; mkdir -p obj; rm -f obj/*.o
echo "compilando ($MODE)..."
for f in $SRCS; do $WINE "$GCC" $CFLAGS -c "$f" -o "obj/${f%.c}.o"; done
echo "linkando..."
$WINE "$GCC" $LDFLAGS obj/*.o -L "$LIBDIR" $LDLIBS -o obj/ds45pad.sym
echo "strip..."
$WINE "$PRXSTRIP" --strip-debug --strip-section-header obj/ds45pad.sym -o obj/ds45pad.prx
echo "assinando..."
PS3="$SCEDIR/data" "$SCEDIR/scetool" $SCEFLAGS -e obj/ds45pad.prx "$OUT" > /dev/null
magic=$(od -A n -t x1 -N 4 "$OUT" | tr -d ' ')
[ "$magic" = "53434500" ] || { echo "ERRORE: $OUT non inizia con SCE ($magic)"; exit 1; }
echo "ok: $OUT ($(stat -c %s "$OUT") bytes)"
