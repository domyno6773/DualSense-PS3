#!/bin/bash
# DS45PAD - build script (Git Bash / MSYS2 on Windows)
#
# Reproduces the build exactly, without GNU make. The make.exe that ships in
# the webMAN MOD repo is linked as a GUI subsystem binary, so under Git Bash it
# returns no output and no usable exit code - calling the compiler directly is
# both simpler and easier to debug.
#
# Usage:  ./build.sh [game]
#   (no argument) -> ds45pad_vsh.sprx   (boot plugin, runs in the VSH)
#   game          -> ds45pad_game.sprx  (-DDS45_GAME; injected into a running
#                                        game to correct the stick curve)

set -e

SDK="${CELL_SDK:-C:/ps3sdk/cell}"
TOOLS="${DS45_TOOLS:-C:/ps3sdk/tools}"

PPU_BIN="$SDK/host-win32/ppu/bin"
HOST_BIN="$SDK/host-win32/bin"
INCLUDE="$SDK/target/ppu/include"
LIBDIR="$SDK/target/ppu/lib"

# Auth id decides where the module is allowed to live.
#   1070000052000001 = sys/internal + vsh/module   -> the VSH build
#   1010000001000003 = retail game/update          -> the in-game build
# PS3xPAD signs its game plugin with the retail id for the same reason.
AUTH_VSH=1070000052000001
AUTH_GAME=1010000001000003

if [ "$1" = "game" ]; then
	EXTRA="-DDS45_GAME"
	OUT="ds45pad_game.sprx"
	AUTH="$AUTH_GAME"
	# A game process loads libio and libfs but *not* libusbd, so the in-game
	# build must not contain a single reference to it - otherwise the loader
	# cannot resolve the module and PS3MAPI hangs on "Load" forever.
	SRCS="libc.c util.c hook.c config.c sticks.c gamehook.c diag.c main.c"
	LDLIBS="-lio_stub -lfs_stub"
else
	EXTRA=""
	OUT="ds45pad_vsh.sprx"
	AUTH="$AUTH_VSH"
	SRCS="libc.c util.c hook.c config.c ds4.c dualsense.c sticks.c padldd.c \
	      usb.c passthru.c diag.c main.c"
	LDLIBS="-lusbd_stub -lio_stub -lfs_stub -llv2_stub"
fi

COMPAT_PAD="../patches/compat_pad.h"
EXTRA_INC=""
if [ -f "$COMPAT_PAD" ]; then
	EXTRA_INC="-include $COMPAT_PAD"
fi

CFLAGS="-mprx -Os -ffunction-sections -fdata-sections -fno-builtin-printf \
        -nodefaultlibs -std=gnu99 -Wall -Wno-unused-parameter -I $INCLUDE $EXTRA_INC $EXTRA"

# -zgenprx/-zgenstub come from the SDK's own PRX rules; they are what turn the
# link output into a loadable module rather than an executable.
LDFLAGS="-mprx -zgenprx -zgenstub -ffunction-sections -fdata-sections \
         -Wl,--strip-unused-data"
# Key revision 0A (3.55) is mandatory for PS3HEN - an older revision makes the
# console hang on boot.
SCEFLAGS="-0 SELF -1 TRUE -s FALSE -2 0A -3 $AUTH -4 01000002 \
          -5 APP -6 0003004000000000 -A 0001000000000000"

# scetool resolves its key file through $PS3, so it works from any directory.
export PS3="$TOOLS/data"

# Tools are called by full path rather than through PATH: these are native
# Windows binaries and Git Bash does not accept "C:/..." entries in PATH.
GCC="$PPU_BIN/ppu-lv2-gcc.exe"
PRXSTRIP="$HOST_BIN/ppu-lv2-prx-strip.exe"
SCETOOL="$TOOLS/scetool.exe"

for t in "$GCC" "$PRXSTRIP" "$SCETOOL" "$TOOLS/data/keys"; do
	[ -f "$t" ] || { echo "faltando: $t"; exit 1; }
done

mkdir -p obj
rm -f obj/*.o

echo "compilando..."
for f in $SRCS; do
	"$GCC" $CFLAGS -c "$f" -o "obj/${f%.c}.o"
done

echo "linkando..."
"$GCC" $LDFLAGS obj/*.o -L "$LIBDIR" $LDLIBS -o obj/ds45pad.sym

echo "strip..."
"$PRXSTRIP" --strip-debug --strip-section-header \
            obj/ds45pad.sym -o obj/ds45pad.prx

echo "assinando..."
"$SCETOOL" $SCEFLAGS -e obj/ds45pad.prx "$OUT" > /dev/null

# A SPRX that still starts with 7F 45 4C 46 ("ELF") means signing silently did
# nothing, and an unsigned module in boot_plugins.txt hangs HEN at boot.
magic=$(od -A n -t x1 -N 4 "$OUT" | tr -d ' ')
if [ "$magic" != "53434500" ]; then
	echo "ERRO: $OUT nao comeca com SCE (magic=$magic) - NAO instale"
	exit 1
fi

echo "ok: $OUT ($(stat -c %s "$OUT") bytes, magic SCE)"
