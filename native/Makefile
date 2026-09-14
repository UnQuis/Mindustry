CC ?= cc
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
CPPFLAGS ?= -Iinclude
LDFLAGS ?=

CORE_SOURCES := src/mc_content.c src/mc_world.c src/mc_format.c src/mc_save.c src/mc_save_map.c src/mc_deflate.c src/mc_typeio.c src/mc_map.c src/mc_schematic.c src/mc_png.c src/mc_simulation.c
CORE_OBJECTS := $(CORE_SOURCES:src/%.c=build/%.o)

.PHONY: all test run sdl clean

all: build/mindustry-c

test: build/test_mindustry
	build/test_mindustry

run: build/mindustry-c
	build/mindustry-c

build/mindustry-c: $(CORE_OBJECTS) build/main.o | build
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

build/test_mindustry: $(CORE_OBJECTS) build/test_mindustry.o | build
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/test_mindustry.o: tests/test_mindustry.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build:
	mkdir -p build

# Optional SDL2 desktop shell. The simulation remains dependency-free and is
# still built by the default target when SDL2 development files are absent.
sdl: $(CORE_OBJECTS) src/sdl_main.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $$(pkg-config --cflags sdl2) $(CORE_OBJECTS) src/sdl_main.c $$(pkg-config --libs sdl2) -lm -o build/mindustry-c-sdl

-include $(CORE_OBJECTS:.o=.d) build/main.d build/test_mindustry.d

clean:
	rm -rf build
