TARGET = psp_home_ctrl
OBJS = psp_home_ctrl.o

USE_KERNEL_LIBS = 1

CFLAGS = -Os -G0 -Wall -Wextra
ASFLAGS = $(CFLAGS)

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
