TARGET = pspra_sample

PSPRA_SRCS = main_psp.c \
../core/src/pspra_net.c \
../core/src/pspra_client.c

RCHEEVOS_SRCS = $(shell find ../external/rcheevos/src -name "*.c" \
! -path "*/rhash/*" \
! -path "*/libretro/*" \
! -name "rc_validate.c" \
! -name "rc_client_raintegration.c" \
! -name "rc_api_editor.c")

OBJS = $(PSPRA_SRCS:.c=.o)
OBJS += $(RCHEEVOS_SRCS:.c=.o)

INCDIR = ../core/include \
../external/rcheevos/include \
../external/rcheevos/src \
../external/rcheevos/src/client \
../external/rcheevos/src/util \
../external/rcheevos/src/rapi \
../external/rcheevos/src/rcheevos

CFLAGS = -O2 -G0 -Wall -Wextra -Wno-unused-parameter -Wno-incompatible-pointer-types -Wno-format
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
LIBS =

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSPRA Client Test

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
