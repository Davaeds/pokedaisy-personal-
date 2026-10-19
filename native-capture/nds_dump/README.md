# nds_dump: headless melonDS for DS fixtures

`mgba_dump`'s twin for the DS games (see docs/DEVELOPMENT.md's "Nintendo DS"). It links
melonDS's core library from `third_party/melonds` (our patch applied at configure time) and the app's own DS sources
(`app/src/main/cpp/nds/`: the field read, the function caller, EXPAND, the STEADY music copy), so what it checks is the
code the app runs. No Android, no Docker: it builds on the Mac with CMake and zlib.

```bash
git submodule update --init third_party/melonds
cmake -S native-capture/nds_dump -B native-capture/nds_dump/build
cmake --build native-capture/nds_dump/build -j
native-capture/nds_dump/build/nds_dump ROM.nds SAVE.sav [--jit] < script.txt
```

It boots with melonDS's FreeBIOS and generated firmware (no BIOS or firmware files), reads `SAVE.sav` if it exists and
**writes every save the game makes back to it**: always give it a copy, never the player's save. `--jit` runs the app's
JIT settings (no fast memory); without it, the interpreter.

## Script commands

One per line from stdin; `#` starts a comment. Numbers take `0x` hex. Keys are `A B SELECT START RIGHT LEFT UP DOWN R L
X Y`, joined with commas (`A,UP`), or `NONE`.

| Command | What it does |
| --- | --- |
| `wait N` | Runs N frames with the held keys. |
| `keys K` | Holds K from now on (`keys NONE` lets go). |
| `press K` | K for one frame, then one frame without. |
| `hold K N` | K for N frames, then one without. |
| `touch X Y [N]` | The stylus at (X, Y) on the bottom screen for N frames (default 2), then up. |
| `shot FILE` | Both screens, top over bottom, as a 256x384 PNG. |
| `shots PREFIX EVERY COUNT` | COUNT shots EVERY frames apart, `PREFIX_0000.png` ... |
| `dump DIR` | `mainram.bin` (4 MB at 0x02000000), `swram.bin`, `dtcm.bin`, `itcm.bin` into DIR. |
| `savestate FILE` / `loadstate FILE` | A melonDS state. |
| `savefile FILE` | The cart's save memory as it is now. |
| `peek8/16/32 ADDR`, `poke8/16/32 ADDR V` | The ARM9's bus. |
| `fieldinfo` | The field read (`pk_nds_field_state`) and the pointers behind it. |
| `trace N` | N frames, each one's field bits, master brightness, DISPCNT and BG3's tiles. |
| `battlemenu N` | The battle controller's command (`pk_nds_battle_command`) each frame for N frames, as it changes. |
| `wide MX [MY]` | EXPAND with MX DS pixels left and right, MY above and below (0 = off). |
| `wideshot FILE`, `wideshots PREFIX EVERY COUNT` | The last wide frame(s) as PNG. |
| `widecheck [N]` | N frames, each checked where the slices overlap the game's own frame (must be 0 differing). |
| `widecount` | Polygons in each slice this frame. |
| `bgdump BG FILE` | Engine A's text BG whole, as the hardware reads it (clear = magenta). |
| `skipdraw N` | Fast-forward's frame skip: N = 1 skips drawing, as the app does on a frame nobody sees. |
| `bench N` | N frames flat out, then ms a frame. |
| `call FN [A0..A3]` | The game's function FN (Thumb: odd) from the field's main loop next frame; prints r0. |
| `action N [A0..A3]` | One of `pk_nds_call`'s jobs (1 = PC, 2 = FLY's checks, 3 = FLY) and its results. |
| `script FILE` / `scriptgo FILE` | A field script (the game's bytecode) run as the PC job runs its own; `scriptgo` returns once it has started. |
| `itcm32 ADDR V` | A word into the hook's spare ITCM (an argument a call points at). |
| `wildoff 1/0`, `battles` | WILD ENCOUNTERS OFF / back on, and the battles started since. |
| `music SECONDS FILE.wav` | STEADY's copy (`pk_nds_music`) begun from the game as it is, run alone; its audio, its cost a frame and the longest stall (`MUSIC_DEBUG=1` prints the ARM7 / IPC state). |
| `livewav SECONDS FILE.wav` | The game's own audio over that many frames. |
| `frame`, `echo TEXT` | The frame count; a line to stdout. |

## Fixtures

`app/src/test/resources/fixtures/platinum`, `platinum_battle` and `heartgold` were made with it; each `README.txt` says
from which ROM, save and boot sequence. Their `mainram.bin` keeps only the ranges the readers use (zeros elsewhere), so
the files stay small in git.
