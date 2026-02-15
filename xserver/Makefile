# Makefile for the X11 health‑monitor server
# ---------------------------------------------------------
#   Petzold‑style low‑level Xlib implementation
#
#   make        – build the executable (xserver)
#   make clean  – remove generated files
#
#   Dependencies:
#       libX11, pthread (both present on a typical Linux system)

CC            = gcc
CFLAGS_COMMON = -Wall -D_POSIX_C_SOURCE=200809L
CFLAGS_RELEASE= -O2
CFLAGS_DEBUG  = -O0 -g -DDEBUG=1
LDFLAGS       = -lX11 -lpthread

TARGET  = xserver
SRC     = xserver.c

all: release

release: CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_RELEASE)
release: $(TARGET)

debug: CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_DEBUG)
debug: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o
