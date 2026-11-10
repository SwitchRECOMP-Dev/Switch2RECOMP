# Switch2Recomp

Static recompiler for Nintendo Switch and Switch 2 games, by MZ Labs. The game's code (ExeFS) is translated from
ARM64 to C once and compiled into a native module for Windows or Linux. A C runtime emulates the Horizon OS services
and the GPU. The game's data (RomFS) is read as it is.

This repository has the source only. No games, keys or firmware are included, and none are downloaded. You need your
own dump of a game you own.

Player manual (recompiling a game, controls, `.ini` options): `README.txt`.
Code layout, logs and traces: `DEVELOPER_GUIDE.md`. Changes: `CHANGELOG.txt`.

## Renderers

The game's `.ini` picks one with `renderer=`:

| Value | What it does | Platforms |
| --- | --- | --- |
| `opengl` (default) | The runtime's own GPU emulation, with Ampere (Switch 2) shaders lowered and turned into GLSL | Windows, Linux |
| `vulkan` | The game's NVN calls drawn directly with Vulkan; shaders translated to SPIR-V while the game runs | Windows, Linux |
| `eden` | The runtime's command processing feeding Eden's Vulkan GPU | Windows, Linux |

`renderer=vulkan` needs Python 3 and the .NET 10 runtime for its shader translator. The first time a shader is used it
shows up about a second and a half later, and later runs read it from `<game>_nvn/cache`. On Linux it also needs
`sudo sysctl vm.mmap_min_addr=0` once, because games read through NULL pointers and expect zeros there.

## Layout

| Path | Contents |
| --- | --- |
| `switchrecomp.py`, `a64.py`, `codegen.py`, `nxloader.py`, `brt.py`, `tools_sym.py` | The recompiler (Python 3) |
| `runtime/` | The runtime (C): kernel, IPC, services, GPU, audio, video, window backends |
| `runtime/nvn/` | The Vulkan renderer (NVN layer) |
| `runtime/edenvk/` | The Eden renderer: Eden's video_core and shader_recompiler with the glue to our runtime |
| `tools/nxshader/` | The live shader translator used by `renderer=vulkan` |
| `tool/` | The GUI (`SwitchRecomp.exe`) and the install helpers |
| `tests/` | Unit and regression tests (`REGRESSION.md`) |

## Building

Requirements on Linux (Debian/Ubuntu names):

```sh
sudo apt install build-essential python3 libsdl2-dev libx11-dev libgl-dev libegl-dev \
    mingw-w64 gcc-mingw-w64-x86-64-win32 cmake ninja-build glslang-tools
```

The Windows builds are cross-compiled with the win32 thread model variant of `x86_64-w64-mingw32-gcc`.

The two Vulkan renderers are static libraries built first. Each is optional: without it the runtime builds with the
other renderers.

```sh
python3 tools/build_nvn.py              # renderer=vulkan, Windows: build/nvn/libnvn.a
python3 tools/build_nvn.py --linux      # renderer=vulkan, Linux:   build/nvn_lin/libnvn.a

cmake -S runtime/edenvk -B runtime/edenvk/build -G Ninja && ninja -C runtime/edenvk/build    # renderer=eden, Linux
cmake -S runtime/edenvk -B runtime/edenvk/buildw -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=runtime/edenvk/mingw.cmake && ninja -C runtime/edenvk/buildw     # renderer=eden, Windows
# then copy libedenvk.a and libsirit.a into runtime/edenvk/lin/ or runtime/edenvk/win/
```

`tools/build_nvn.py` expects Vulkan-Headers in `external/deps/Vulkan-Headers` (or `VULKAN_SDK`).

Then the runtimes, from the repository root:

```sh
python3 switchrecomp.py --build-runtime --target win64 --backend win32 --cc x86_64-w64-mingw32-gcc
python3 switchrecomp.py --build-runtime --target linux --backend x11   --cc gcc
```

The results go to `bin/`.

## Recompiling a game

```sh
python3 switchrecomp.py /path/to/game -o MyGame
```

`/path/to/game` holds the `exefs/` and `romfs/` folders extracted from a legally owned copy. `README.txt` has the
full steps and the options.

## License

SwitchRecomp is under the GNU AGPL v3 (`LICENSE.txt`) with the additional terms in `NOTICE.txt`: copies, forks and
derived works keep the credit to "SwitchRecomp by MZ Labs" and the startup screen. The code from Eden is
GPL-3.0-or-later, and a runtime built with `renderer=eden` is a GPL-3.0 work. Third-party components keep their own
licenses; `NOTICE.txt` lists them.
