# DS45PAD - PS3 VSH plugin build
#
# Requires the Cell SDK PPU toolchain (ppu-lv2-gcc) and scetool on PATH, the
# same combination webMAN MOD and PS3xPAD are built with. See ../docs/BUILD.md.
#
#   make            -> ds45pad_vsh.sprx  (plugin loader / XMB build)
#   make game       -> ds45pad_game.sprx (optional in-game build)
#   make all-builds -> both
#   make clean

CELL_MK_DIR = $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

CELL_BUILD_TOOLS = GCC

PPU_AR              = ppu-lv2-ar
PPU_RANLIB          = ppu-lv2-ranlib
PPU_STRIP           = ppu-lv2-strip
PPU_CC              = ppu-lv2-gcc
PPU_CXX             = ppu-lv2-g++
PPU_LD              = ppu-lv2-ld
PPU_CCLD            = ppu-lv2-gcc
PPU_CXXLD           = ppu-lv2-g++
PPU_PRX_CCLD        = ppu-lv2-gcc
PPU_PRX_CXXLD       = ppu-lv2-g++
PPU_PRX_STRIP       = ppu-lv2-prx-strip

PPU_PRX_FLAGS = -mprx -mno-sn-ld -Os -ffunction-sections -fdata-sections \
                -fno-builtin-printf -nodefaultlibs -std=gnu99 \
                -Wall -Wno-shadow -Wno-unused-parameter

EXTRA_CFLAGS    ?= -include ../patches/compat_pad.h
PPU_CFLAGS       = $(PPU_PRX_FLAGS) $(EXTRA_CFLAGS)
PPU_LDFLAGS     += $(PPU_PRX_FLAGS)
PPU_PRX_LDFLAGS += -mprx -ffunction-sections -fdata-sections -Wl,--strip-unused-data

PPU_PRX_STRIPFLAGS += --strip-debug --strip-section-header

# The two builds do NOT share a source list or a library list.
#
# A game process loads libio and libfs but *not* libusbd. A single reference to
# libusbd leaves the loader unable to resolve the module, and PS3MAPI then sits
# on "Load" forever - so usb.c, passthru.c and the pad-LDD code, which are all
# VSH-only anyway, must be absent from the in-game build rather than merely
# unused by it.
SRCS_VSH  = libc.c util.c hook.c config.c ds4.c dualsense.c sticks.c padldd.c \
            usb.c passthru.c diag.c main.c
SRCS_GAME = libc.c util.c hook.c config.c sticks.c gamehook.c diag.c main.c

LIBS_VSH  = -lusbd_stub -lio_stub -lfs_stub -llv2_stub
LIBS_GAME = -lio_stub -lfs_stub -llv2_stub

# Default to the VSH build so a bare `make` in this directory still works.
PPU_SRCS       = $(SRCS_VSH)
PPU_PRX_LDLIBS = $(LIBS_VSH)

PPU_PRX_TARGET = ds45pad.prx

# Signing parameters.
#
# -2 0A is the 3.55 key revision and it is not optional for PS3HEN: a plugin
# signed with an older revision makes HEN hang on boot. This is why PS3xPAD,
# which signs with -2 04, had to be re-released as "3.55 signed for HEN".
# webMAN MOD uses the values below and loads on both CFW and HEN.
#
# The auth id is passed per target, not baked in here: it decides where the
# module is allowed to live, and the two builds live in different places.
#   1070000052000001 = sys/internal + vsh/module   -> the VSH build
#   1010000001000003 = retail game/update          -> the in-game build
# Signing the in-game build with the VSH id produces a module a game process
# will not load.
SCETOOL_FLAGS = -0 SELF -1 TRUE -s FALSE -2 0A \
                -4 01000002 -5 APP -6 0003004000000000 -A 0001000000000000

AUTH_VSH  = 1070000052000001
AUTH_GAME = 1010000001000003

SPRX_VSH  = ds45pad_vsh.sprx
SPRX_GAME = ds45pad_game.sprx

all: vsh

# Both targets clean first. Without that, objects left over from the other
# variant get linked in silently - a "VSH" build made after a `make game` would
# contain -DDS45_GAME code.
vsh:
	$(MAKE) clean-obj
	$(MAKE) sign-one PPU_SRCS="$(SRCS_VSH)" PPU_PRX_LDLIBS="$(LIBS_VSH)" \
	                 AUTH=$(AUTH_VSH) SPRX=$(SPRX_VSH)

game:
	$(MAKE) clean-obj
	$(MAKE) sign-one PPU_SRCS="$(SRCS_GAME)" PPU_PRX_LDLIBS="$(LIBS_GAME)" \
	                 EXTRA_CFLAGS="-DDS45_GAME -include ../patches/compat_pad.h" \
	                 AUTH=$(AUTH_GAME) SPRX=$(SPRX_GAME)

sign-one:
	$(MAKE) $(PPU_OBJS_DEPENDS)
	$(PPU_PRX_STRIP) $(PPU_PRX_STRIPFLAGS) $(PPU_PRX_TARGET)
	scetool $(SCETOOL_FLAGS) -3 $(AUTH) -e $(PPU_PRX_TARGET) $(SPRX)
	@# A SPRX still starting with 7F 45 4C 46 ("ELF") means signing silently
	@# did nothing, and an unsigned module in boot_plugins.txt hangs HEN at
	@# boot. Delete it rather than leave it lying around to be installed.
	@magic=`od -A n -t x1 -N 4 $(SPRX) | tr -d ' '`; \
	 if [ "$$magic" != "53434500" ]; then \
	   echo "ERRO: $(SPRX) nao comeca com SCE (magic=$$magic) - NAO instale"; \
	   rm -f $(SPRX); exit 1; \
	 fi
	@echo "ok: $(SPRX) assinado (magic SCE)"

all-builds:
	$(MAKE) vsh
	$(MAKE) game

clean-obj:
	-rm -f *.o $(PPU_PRX_TARGET)

CLEANFILES = $(SPRX_VSH) $(SPRX_GAME) $(PPU_PRX_TARGET)

include $(CELL_MK_DIR)/sdk.target.mk
