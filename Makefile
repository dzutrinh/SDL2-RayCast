# Makefile — builds the SDL2/C raycasting demo
# Usage: make [clean]

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs) -lm
TARGET  = ray

.PHONY: all clean

all: $(TARGET)

$(TARGET): ray.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)
