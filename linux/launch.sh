#!/bin/sh
# Optional TrimUI launcher. No firmware power, CPU, library or save migration.
set -eu
PAK_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
cd "$PAK_DIR"
export POCKET_FULLSCREEN=1
export POCKET_RAW_TRIMUI="${POCKET_RAW_TRIMUI:-1}"
mkdir -p roms saves states
if [ -f launch.log ] && [ "$(wc -c < launch.log)" -gt 1048576 ]; then
  mv -f launch.log launch.previous.log
fi
exec ./pokedaisy-linux --data "$PAK_DIR" "$@" >> "$PAK_DIR/launch.log" 2>&1
