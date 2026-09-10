## PKZLib
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/goliathret/PKZLib)
[![Discord Server](https://img.shields.io/badge/-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.gg/Pv4STSn6d9)

PKZLib is a C++ library, which aims to recreate parts of the Goliath Engine, created by Beenox, so they can be usable in modern projects, with the main goal being *accurate reading/writing for the engine's proprietary formats*.
This includes chunk handling, and a *Package Manager* for multiple package files at once.

## BaseUtils
BaseUtils is what powers the Goliath Engine's math, logic, and any algorithms implemented in the games. PKZLib contains accurate recreations of the BaseUtils system.

## Building

Header-only C++17; zlib (`contrib/zlib`) is needed for the `.pkz` container.

```
git submodule update --init
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out
out/pkzlib_selftest
```

Consumers link the `pkzlib` INTERFACE target