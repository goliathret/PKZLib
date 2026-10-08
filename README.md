## PKZLib
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/goliathret/PKZLib)
[![Discord Server](https://img.shields.io/badge/-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.gg/Pv4STSn6d9)

PKZLib is a C++ library, which aims to recreate parts of the Goliath Engine, created by Beenox, so they can be usable in modern projects, with the main goal being *accurate reading/writing for the engine's proprietary formats*.
This includes chunk handling, and a *Package Manager* for multiple package files at once.

## BaseUtils
BaseUtils is what powers the Goliath Engine's math, logic, and any algorithms implemented in the games. PKZLib contains accurate recreations of the BaseUtils system.

## Building

Header-only C++17, except `Graphics/XenosTexture.cpp` and `BaseUtils/BUPng.cpp`, which build into the
`pkzlib_textures` library on the `thirdparty/textures` submodules (ReXGlue, libsquish, stb). zlib (`contrib/zlib`)
is needed for the `.pkz` container.

```
git submodule update --init
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out
out/pkzlib_selftest
```

Consumers link the `pkzlib` INTERFACE target.

## Schema-driven field dumps (Heavy beta)

`tools/pkzfields.py` walks a raw `.pak` chunk tree and decodes leaf payloads
with composable XML schemas. Engine-wide layouts live in
`schemas/goliath.xml`; title-specific layouts stay in separate files instead
of entering the C++ parser.

```sh
# Decode common package/resource fields.
python tools/pkzfields.py game.pak --chunk GenSub_ResourceHeader

# Add Spider-Man: Edge of Time layouts and inspect its world draw table.
python tools/pkzfields.py level.pak --game eot \
  --chunk RenderOctree_Prims --max-records 8

# Machine-readable output for corpus analysis.
python tools/pkzfields.py game.pak --game eot --json > fields.json
```
