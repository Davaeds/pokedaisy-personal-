---
title: Linux handheld foundation
summary: Build and try the initial Linux host with physical controls and the existing portable companion.
section: start
order: 90
keywords: [linux, handheld, trimui, brick, smart pro, sdl]
---

You can build the initial Linux host from source. It runs Game Boy, Game Boy
Color and Game Boy Advance games with your own ROMs and saves. Its companion
uses the existing portable core for supported retail English Gen 3 games.
Other games have emulation only in this foundation.

Follow `linux/README.md` in the source repository for dependencies, native and
TrimUI builds, controls, save paths and test commands. Use a separate data
folder while testing. Existing emulator saves are never imported automatically.

Press the companion button to pause the game and open its companion. Use the
D-pad to focus an option, A to choose it and B to go back. The shoulder buttons
change tabs. Your game menu has cancel-first save, load and quit confirmations.

This initial host does not include expanded overworlds, the downstream Polished
Crystal companion, enhanced summaries or firmware power handling. Earlier
builds ran on the Brick and Smart Pro, but this separated foundation still
needs fresh device testing. Other Linux handhelds are not verified.
