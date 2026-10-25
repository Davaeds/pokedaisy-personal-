# Linux integration handoff

## Merge order

1. `upstream/linux-display-capability-v1`: optional host capability to omit the
   3DS-only SCREEN row. Null/zero hosts retain the original UI. Shared files:
   pd_ui.h, pd_ui_settings.c, a focused test and its Makefile target.
2. `upstream/linux-foundation-v1`: this folder, Linux CI, README/manual link.
   This branch is based on item 1. Merge the capability first; then this PR's
   main comparison loses that prerequisite automatically. No other shared
   core or Android/emulator source changes are needed.

No force-push or rewrite of existing branches is required. The old four-part
shared prerequisite stack is not required for this first foundation.

## Data refactor

At the audit, main is 371d34f (1.3.2); dev is 4252e18. The gamedata README on dev
says phase 1 generates Kotlin NativeConfigs, with 3DS C generation next.
This host directly consumes the existing 3DS core. It adds no data parser,
address/offset list, Kotlin-derived duplicate or competing generator.
Lidor can move that core to generated C once the schema/output contract is
ready; no Linux-specific decoding needs reconciling. We tested applying the
small shared prerequisite and host on top of dev separately from main.

## Preserved downstream work

The complete working development history remains on
keeganwhitacre/pokedaisy-brick `feature/summary-exp-items` at 1f3e1a1.
`stable/brick-phase22` at 3e6cb54 preserves the prior Brick baseline.
Old `upstream/linux-*` branches remain untouched as reference extractions.
Local backup refs also preserve the starting checkout and uncommitted work.

The foundation reuses the early SDL host (e472c4f) and the later tested save
wrapper, reducing dependencies instead of importing the entire feature port.
Later independent contributions can restore the Polished adapter/presentation,
expanded rendering/profile interface, enhanced GBA summaries, and firmware
lifecycle integrations after review of their data ownership. Version-specific
battle/action constants and the Polished symbol-derived expansion profile must
be reconciled with the refactor before claiming a single data source.

## Maintainer coordination

Main is the default target unless Lidor prefers dev. The host is compatible
with both audited trees; it does not merge unrelated dev changes into main.
Agree which firmware/input stack Lidor used on the Smart Pro before adding a
firmware power backend or publishing a device-support claim. This does not
block the generic SDL host. Pause downstream feature work while Lidor adapts
this foundation to the game-data refactor.

## Recorded local validation

Native and AArch64/QEMU Release builds pass. CTest passes save I/O failure
coverage, real diagnostic GB/GBA host execution and architecture-specific
package inspection. Package checks cover the executable, licenses, provenance,
no game files and refusal to overwrite an earlier package. The shared display
capability test passes; all 80 default preview PNGs match pristine main byte
for byte. Documentation/search tests and the 29-page website build pass.
The supplied private Emerald save boots on native and ARM, opens the existing
Party/Summary UI, backs out and reloads a state. Its source ROM/save files are
unchanged and are not committed. Crystal/Polished have no companion adapter
in this first branch; GBC validation here uses the original diagnostic, not a
retail Crystal ROM or physical device.

A disposable merge with upstream/dev applies without conflicts. Its native
build/tests and the existing gamedata check pass (102 configs). This is not a
claim that later, unpublished refactor phases have already been integrated.
Remote Actions results must be checked on the PR; local passes do not imply
those runs have completed or been approved.

## Files per review

The shared prerequisite contains only `3ds/core/pd_ui.h`,
`3ds/core/pd_ui_settings.c`, `3ds/test/host_display_test.c` and `3ds/Makefile`.
The host contribution contains `.github/workflows/linux.yml`, the root README
link, `docs/manual/linux.md`, and `linux/`: CMakeLists.txt, main.c,
host_io.c/.h, launch.sh, trimui-toolchain.cmake, setup_trimui_sdk.py,
make_diagnostic.py, test_host_io.c, test_runtime.c, test_package.py,
package.py, README.md, HANDOFF.md and .gitignore.
There are no changes to pk_wide, pk_savevf, game snapshots, game configs,
Android sources, mGBA pin or the shared companion's presentation.
