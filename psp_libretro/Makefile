TARGET = pspra_snes_libretro

PSPRA_SRCS = main_libretro_psp.c \
             ../core/src/pspra_net.c \
             ../core/src/pspra_client.c

RCHEEVOS_SRCS = $(wildcard ../external/rcheevos/src/rhash/md5.c) \
                $(shell find ../external/rcheevos/src -name "*.c" \
                ! -path "*/rhash/*" \
                ! -path "*/libretro/*" \
                ! -name "rc_validate.c" \
                ! -name "rc_client_raintegration.c" \
                ! -name "rc_api_editor.c")

SNES9X_DIR = ../external/snes9x2005
SNES9X_CORE_DIR = $(SNES9X_DIR)/source

SNES9X_SRCS = \
    $(SNES9X_CORE_DIR)/c4.c \
    $(SNES9X_CORE_DIR)/c4emu.c \
    $(SNES9X_CORE_DIR)/cheats2.c \
    $(SNES9X_CORE_DIR)/cheats.c \
    $(SNES9X_CORE_DIR)/clip.c \
    $(SNES9X_CORE_DIR)/cpu.c \
    $(SNES9X_CORE_DIR)/cpuexec.c \
    $(SNES9X_CORE_DIR)/cpuops.c \
    $(SNES9X_CORE_DIR)/data.c \
    $(SNES9X_CORE_DIR)/dma.c \
    $(SNES9X_CORE_DIR)/dsp1.c \
    $(SNES9X_CORE_DIR)/fxemu.c \
    $(SNES9X_CORE_DIR)/fxinst.c \
    $(SNES9X_CORE_DIR)/gfx.c \
    $(SNES9X_CORE_DIR)/getset.c \
    $(SNES9X_CORE_DIR)/globals.c \
    $(SNES9X_CORE_DIR)/memmap.c \
    $(SNES9X_CORE_DIR)/obc1.c \
    $(SNES9X_CORE_DIR)/ppu.c \
    $(SNES9X_CORE_DIR)/sa1.c \
    $(SNES9X_CORE_DIR)/sa1cpu.c \
    $(SNES9X_CORE_DIR)/sdd1.c \
    $(SNES9X_CORE_DIR)/sdd1emu.c \
    $(SNES9X_CORE_DIR)/seta010.c \
    $(SNES9X_CORE_DIR)/seta011.c \
    $(SNES9X_CORE_DIR)/seta018.c \
    $(SNES9X_CORE_DIR)/seta.c \
    $(SNES9X_CORE_DIR)/spc7110.c \
    $(SNES9X_CORE_DIR)/spc7110dec.c \
    $(SNES9X_CORE_DIR)/srtc.c \
    $(SNES9X_CORE_DIR)/tile.c \
    $(SNES9X_CORE_DIR)/apu.c \
    $(SNES9X_CORE_DIR)/soundux.c \
    $(SNES9X_CORE_DIR)/spc700.c \
    $(SNES9X_DIR)/libretro.c

OBJS = $(PSPRA_SRCS:.c=.o) \
       $(RCHEEVOS_SRCS:.c=.o) \
       $(SNES9X_SRCS:.c=.o)

INCDIR = ../core/include \
         ../external/rcheevos/include \
         ../external/rcheevos/src \
         ../external/rcheevos/src/client \
         ../external/rcheevos/src/util \
         ../external/rcheevos/src/rapi \
         ../external/rcheevos/src/rcheevos \
         $(SNES9X_CORE_DIR) \
         $(SNES9X_DIR) \
         $(SNES9X_DIR)/libretro-common/include

CFLAGS = -O3 -G0 -march=allegrex -fomit-frame-pointer -ffast-math -funroll-loops -finline-functions -fno-builtin -ffunction-sections \
         -DNDEBUG \
         -Wall -Wextra \
         -Wno-unused-parameter \
         -Wno-unused-variable \
         -Wno-unused-function \
         -Wno-sign-compare \
         -Wno-implicit-fallthrough \
         -Wno-incompatible-pointer-types \
         -Wno-format \
         -Wno-strict-aliasing \
         -Wno-char-subscripts \
         -Wno-array-bounds \
         -fno-strict-overflow \
         -UPSP \
         -DLOAD_FROM_MEMORY \
         -DLAGFIX \
         -DUSE_OLD_COLOUR_OPS

CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
LIBS = -lpsppower -lpspaudio

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSPRA SNES9x2005

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

%.prx: %.elf
	psp-fixup-imports $<
	psp-prxgen $< $@
