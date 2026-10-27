CC      = gcc
CFLAGS  = -Wall -Wextra -g -O2 -pthread \
          -I../core/include \
          -I../external/rcheevos/include \
          -I../external/rcheevos/src \
          -I../external/rcheevos/src/client \
          -I../external/rcheevos/src/util \
          -I../external/rcheevos/src/rapi \
          -I../external/rcheevos/src/rcheevos

# PSPRA Core & Test sources
PSPRA_SRCS = main_linux.c \
             ../core/src/pspra_net.c \
             ../core/src/pspra_client.c

# Automatically find all .c files inside external/rcheevos/src (excluding rhash and libretro)
RCHEEVOS_SRCS = $(shell find ../external/rcheevos/src -name "*.c" ! -path "*/rhash/*" ! -path "*/libretro/*")

OBJS   = $(PSPRA_SRCS:.c=.o) $(RCHEEVOS_SRCS:.c=.o)
TARGET = pspra_test

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) -lm -lpthread

clean:
	rm -f $(OBJS) $(TARGET)