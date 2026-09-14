# Native C port

This directory is the first migration milestone for a native C implementation of Mindustry. It is intentionally **not** a claim that the Java game has already been replaced: the upstream checkout contains 871 Java source files, an external Arc runtime, 663 asset files, and four platform targets. A faithful port has to be migrated and verified subsystem by subsystem.

## What exists now

- A C17, dependency-free deterministic simulation kernel.
- Explicit world/tile, item/inventory, entity, block and tick APIs.
- A small content table seeded from the current `Items.java` values.
- A Java-compatible uncompressed map-section codec for plain tiles and RLE.
- Java-compatible MSAV headers, length-prefixed regions, string maps and modified UTF-8 helpers.
- A dependency-free zlib/DEFLATE wrapper: stored-block writer plus stored/fixed/dynamic Huffman reader.
- A validated reader for current plain-tile `.msav` streams, including metadata and content-name mappings.
- Fixed 60 Hz stepping and a state hash for differential/conformance tests.
- A headless executable and tests that compile without Java, Gradle or SDL.
- An optional SDL2 desktop shell (`make sdl`) that draws the current native state.

The default target is deliberately headless so CI can test the simulation on machines without graphics development packages:

```sh
cd native
make
make test
make run -- --ticks 180  # use `build/mindustry-c --ticks 180` directly
```

For the first desktop shell, install SDL2 development files and run:

```sh
make sdl
build/mindustry-c-sdl
```

## Porting rules

1. **C only in this directory.** Do not introduce C++ as an escape hatch. The public header rejects C++ compilation.
2. **Simulation is renderer-independent.** SDL is an adapter, not the game model. This is required for the future headless server and deterministic replay tests.
3. **No guessed compatibility.** Save, map, network and logic compatibility must be proven against the Java implementation before their C readers/writers are enabled.
4. **Every subsystem gets differential tests.** A C implementation is not considered equivalent because it merely builds; it must agree with Java reference fixtures at fixed tick boundaries.
5. **Assets are reused initially.** Replacing Arc's asset pipeline is a separate task; copying or silently changing the 62 MB asset set would make a 1:1 comparison impossible.

## Migration order

The planned order is: content IDs and registries, map/world format, deterministic entities and collision, item/liquid/power networks, block behaviors, waves and AI, logic processor, save/map/schematic I/O, networking, UI/input, rendering/audio, mod API, then Android/iOS backends. Java remains the executable reference until the corresponding C subsystem has conformance coverage.

`PORT_STATUS.md` records the boundary of this milestone and the parts that are intentionally still absent.
