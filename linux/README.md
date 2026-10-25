# PokeDaisy Linux foundation

SDL2 host for the pinned mGBA library and the existing portable companion in
`3ds/core/`. The host is isolated here beside `3ds/`. It does not introduce
another emulator, companion decoder, game-address table or data generator.

This first contribution deliberately excludes the downstream Polished adapter,
expanded overworlds, enhanced summaries, filters, automatic recovery and
firmware suspend/shutdown. Those implementations remain on the development
branches listed in HANDOFF.md. This branch is not an update of the installed
Brick development package. Use a separate data directory when evaluating it.

## Native build

Requires C11, CMake 3.18+, SDL2 development headers/library, POSIX threads,
libm/libdl, Python 3.12+ for tests. zlib is optional. On Ubuntu 24.04:

```
sudo apt-get install cmake build-essential libsdl2-dev python3
git submodule update --init third_party/mgba
cmake -S linux -B linux/build -DCMAKE_BUILD_TYPE=Release
cmake --build linux/build -j2
ctest --test-dir linux/build --output-on-failure
linux/build/pokedaisy-linux --data /your/test-directory /your/game.gba
```

Without a ROM argument, choose a `.gb`, `.gbc` or `.gba` from DATA/roms.
The host creates DATA/saves and DATA/states. It backs up an existing battery
save before opening the game and stops emulation on a latched save I/O failure.
State writes use a temporary file and rename. States include RTC, not battery
save restoration. These are not guarantees against sudden power loss.
No existing emulator save is discovered or imported automatically. Copy only
a backed-up raw battery save to DATA/saves/COMPLETE-ROM-FILENAME.sav when needed.
Two ROMs with the same filename share this path; keep such games in separate
DATA directories. DATA/settings.ini is host-local. Graceful quit flushes mGBA;
forced termination cannot do that. Keep using in-game saves.

## Controls and display

Keyboard: arrows, Z=A, X=B, Enter=Start, Backspace=Select, Q/E=L/R,
Tab=companion, F=fast-forward, Escape=game menu. In the companion use
D-pad to focus, A to choose, B to go back and L/R to change tabs.
Save/load/quit confirmations are cancel-first. Fast-forward has no audio.

SDL GameController mappings take priority. The right face button acts as game
A; the bottom button acts as game B. Unmapped devices are ignored unless you
explicitly set `POCKET_RAW_TRIMUI=1`, which enables the existing TrimUI raw
button layout. Connect the controller before starting. Other controllers can
supply SDL_GAMECONTROLLERCONFIG; they are not claimed hardware support.

`POCKET_FULLSCREEN=1` uses SDL's current display mode; no Brick panel size is
assumed. Gameplay uses centered whole-pixel scaling, and the companion uses
its existing 512x384 logical canvas. The 3DS screen-mode setting is hidden by
an optional host capability; fast-forward and state settings stay available.
Audio-device failure is logged and leaves silent gameplay available.

The existing portable core provides the supported retail English Gen 3
companion. GB/GBC games, including Crystal and Polished Crystal, are emulation
only in this foundation. Do not mistake the downstream Polished companion or
Emerald Summary enhancements for features included here. Unsupported games
keep the existing core's unsupported-companion message.

## TrimUI cross-build and launcher

The checksum-pinned toolchain is AArch64 GCC 8.3 with the TrimUI SDK sysroot.
It is a build target, not an assertion of firmware/device compatibility.

```
python3 linux/setup_trimui_sdk.py /absolute/path/trimui-sdk
export NEXTUI_TOOLCHAIN=/absolute/path/trimui-sdk/cross/x86_64-aarch64-nextui-linux-gnu
cmake -S linux -B linux/build-arm -DCMAKE_TOOLCHAIN_FILE=trimui-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build linux/build-arm -j2
python3 linux/package.py linux/build-arm/pokedaisy-linux /new/package-output
```

The package uses firmware-provided SDL2 and C libraries, with no replacement
system libraries or root operations. The optional launch.sh sets fullscreen
and the explicit raw TrimUI fallback, passes quoted ROM arguments, and keeps
a bounded log. Put the extracted package in a separately named NextUI Tools
pak if your firmware supports that launcher convention. Direct command-line
launch works without NextUI; the binary never invokes firmware power helpers.

Earlier development builds were reported running by the user on the Brick
and by Lidor on the Smart Pro. That does not validate this newly separated
foundation on either device. Recheck controls, readable display, audio,
in-game saves, state load, exit and firmware power behavior on both devices.

## Tests and CI artifacts

CTest runs save mapping/error-injection tests and the actual SDL/mGBA host
on original GB/GBA diagnostic programs. Runtime tests cover companion pause,
menu/back, controller isolation, save/state roundtrips and a battery marker.
The diagnostics contain no commercial ROM or game data. The shared host-display
capability has a separate `make -C 3ds test-host-display` regression test.

The workflow builds/tests x86_64 and AArch64 under QEMU and uploads the executable,
license notices and source revision. The ARM SDK's offscreen SDL software driver
is used under QEMU. This is not physical hardware or remote CI validation by
itself. Private integration can use `linux-runtime-test ROM DATA CAPTURES`, with
an isolated copy of a compatible raw battery save. No private ROM, save, capture
or extracted artwork is included in commits or install artifacts.

GPL-3.0, preserving Lidor Itzhari's attribution. Existing dependency/font notices
are in NOTICE and the packaged licenses. No new third-party library is added.
