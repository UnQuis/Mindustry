# C port status

Date of this milestone: 2026-09-15.

The source repository currently has 871 Java sources under `core/src`, uses Arc as an external engine, and contains desktop, Android, iOS and server modules. The target is a full C implementation with current behavior, assets, persistence, networking and mod compatibility. This document prevents a partial port from being mistaken for that target.

## Milestone 1 — complete in this change

- [x] C17 build that does not depend on Gradle or Java.
- [x] Dependency-free simulation/world API.
- [x] 60 Hz fixed-step clock and deterministic state hash.
- [x] Basic tile/block/entity/inventory primitives.
- [x] Initial item values copied from `core/src/mindustry/content/Items.java`.
- [x] C-only built-in content registry with stable per-type IDs, canonical Java names/order, legacy item IDs, validation/hash/manifest APIs and remapping metadata for all declared built-in content groups.
- [!] Registry identity/order is canonical, but `MC_REGISTRY_FLAG_METADATA_PARTIAL` is set on entries: block/unit/bullet gameplay metadata still needs Java differential fixtures before it can claim 1:1 simulation parity.
- [x] Headless executable and tests.
- [x] Optional SDL2 frontend boundary.

## Milestone 2 — in progress

- [x] Java-compatible plain-tile map-section codec: big-endian shorts and floor/block RLE.
- [x] Java-compatible MSAV header, length-prefixed region, StringMap and modified UTF-8 primitives.
- [x] Dependency-free zlib/DEFLATE wrapper: stored-block writer and stored/fixed/dynamic Huffman reader.
- [x] Negative tests for truncation, checksum, trailing bytes, invalid flags and malformed container data.
- [x] Validated read/write composition for current `.msav` streams.
- [x] Raw save loader/writer preserving patches, entity, marker and custom regions as opaque bytes.
- [x] Semantic patch, entity/team-plan, UBJSON MapMarkers and custom-chunk codecs with raw-region preservation.
- [x] Content header mapping and serialized-name remapping, including legacy block aliases.
- [x] Map-section to `McWorld` restoration API.
- [x] Lossless raw map-section codec for building entity chunks and tile data records.
- [x] Version-1 `msch` codec for block dictionary, tags, positions, rotations and raw TypeIO configs.
- [x] Pure-C PNG RGB/RGBA codec with CRC validation and all five row filters.
- [x] Gameplay-state bridge: load MSAV metadata, map tile data, building records and raw world entities into native state; synchronize map/entity mutations and runtime metadata back into stateful MSAV saves.

## Milestone 3 — semantic Building state

- [x] Public C Building model for common block fields, inventories, liquids, power, production, drills, turrets, conveyors, factories, cores, processors and opaque config.
- [x] Owning store with entity/tile lookup, placement/removal, links, deterministic resource transfer and separate power/liquid network bookkeeping.
- [x] Fixed-tick Building update: production recipes, drill output, turret reload, conveyor movement, processor execution, heat, efficiency and power accounting.
- [x] MSAV-compatible native custom payload (`mindustry-native-buildings`) with fixed-width codec, round-trip tests and preservation of unknown custom chunks.
- [x] Rule validation/normalization, typed config access, inspection/path queries, event journal and transactional snapshot/delta helpers.
- [x] Regression coverage for semantic mutation, malformed payloads, deterministic ticks, network routing, gameplay integration and MSAV round trips.

The Building model covers the native block families currently represented in the compact C content table. Complete Java parity still requires expanding the content registry and differential fixtures for every upstream block class.

## Not ported yet

- [ ] Arc replacement: graphics, audio, file system, input, UI scene graph, fonts, shaders, threading and platform services.
- [ ] All content definitions and generated registries (`Blocks`, `Bullets`, `UnitTypes`, liquids, planets, tech trees, effects, sounds and generated IDs).
- [ ] Full world simulation: building behaviors, item/liquid/power graphs, fluids, status effects, damage, physics and collision.
- [ ] Unit AI, pathfinding, wave spawning, campaign and objectives.
- [ ] Logic assembler and executor, including all instructions and sensors.
- [ ] Full semantic gameplay parity for decoded state: Java-generated entity field schemas, building inventories/networks, team plans, MapMarkers/objectives and registered custom-chunk behavior are not all modeled yet. The bridge preserves unmodeled bytes.
- [ ] Client/server protocol, LAN discovery, administration and replay behavior.
- [ ] JavaScript mods and the public mod/content API.
- [ ] Pixel-equivalent rendering, audio mixing, particles, shaders, UI and localization.
- [ ] Windows/macOS, Android and iOS platform backends.
- [ ] Differential fixtures against the Java reference implementation.

## Compatibility gates

A subsystem may be marked complete only when it has:

1. a C implementation with no Java runtime dependency;
2. fixtures generated by the current Java implementation;
3. byte-for-byte or documented semantic comparison at deterministic tick boundaries;
4. failure tests for malformed and newer data;
5. a documented migration for mods and platform behavior, where applicable.

The Java implementation therefore remains in the repository during migration. Removing it before the gates above pass would make it impossible to establish the requested 1:1 behavior.
