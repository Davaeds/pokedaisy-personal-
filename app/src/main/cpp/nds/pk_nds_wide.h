// EXPAND and ZOOM for a DS game's overworld: its 3D drawn past the top screen's edges, every way.
//
// How: every polygon the game submits (melonDS's GPU3D WideSubmitHook) is also clipped again, shifted in clip space by
// about a screen, into polygon lists of our own - one per slice round the screen: left, right, above, below and the
// four corners - swapped with the game's at VBlank (WideSwapHook). A second, unthreaded software renderer draws each
// slice after the frame, only the rows and columns the window shown needs, and the wide frame is the game's own
// 256 x 192, untouched, with the slices round it. The game's polygon RAM, counters and picture are never touched: the
// middle is the real frame, pixel for pixel.
//
// Shared by the app (pk_nds.cpp) and the headless tool (native-capture/nds_dump's wide / wideshot / widecheck).
#pragma once

#include <cstdint>

namespace melonDS { class NDS; }

/** The widest margins, in DS pixels: left / right and above / below. A slice is a screen less its overlap with the
 * screen, so up to 252 / 188 (less SPARE); the 3x zoom step needs 192 / 84 on the Thor (640 x 360), 242 on a 20:9 phone. */
constexpr int PK_NDS_WIDE_MAX_MARGIN_X = 248;
constexpr int PK_NDS_WIDE_MAX_MARGIN_Y = 96;
constexpr int PK_NDS_WIDE_MAX_W = 256 + 2 * PK_NDS_WIDE_MAX_MARGIN_X;
constexpr int PK_NDS_WIDE_MAX_H = 192 + 2 * PK_NDS_WIDE_MAX_MARGIN_Y;

/** Hooks [nds]'s 3D engine ([on] false: the hooks removed). Between frames. */
void pk_nds_wide_set(melonDS::NDS* nds, bool on);

/** [battle]: a battle on the top screen - its margins are its background, BG3 (512 pixels wide: the game's own picture
 * goes on past the screen, both platforms included), scrolled as the screen is; clear there, the backdrop.
 *
 * After each frame: composes the wide frame from the game's top screen ([topArgb]: melonDS's 0xAARRGGBB, 256 x 192)
 * and the slices drawn after the last frame, then draws the slices for the next one. [draw] false skips the composing
 * (a frame no one sees), [drawNext] false the slices (the next frame won't be seen either: fast-forward shows one frame
 * of each group, and the slices were two thirds of a frame's cost); [margins] false leaves them black (off the
 * overworld: a battle, a menu). Returns whether a wide frame was composed. */
bool pk_nds_wide_frame(melonDS::NDS* nds, const uint32_t* topArgb, bool draw, bool margins = true, bool drawNext = true,
    bool battle = false);

/** How many pixels past each edge the window shown needs (left, right, above, below; clamped to the largest
 * margins): the slices draw only those, the rest of the frame stays black. Between frames. */
void pk_nds_wide_need(int left, int right, int top, int bottom);

/** The last wide frame's window [x, x + w) x [y, y + h) in the game frame's pixels (its top-left (0, 0)) into [out]
 * (w x h, RGBA words: 0xAABBGGRR, the screen's upload format): black past what was composed. */
void pk_nds_wide_view(uint32_t* out, int x, int y, int w, int h);

/** What the last wide frame covers, in the game frame's pixels (for nds_dump's wideshot). */
void pk_nds_wide_rect(int* x, int* y, int* w, int* h);

/** Checks the slices against the game's frame where they overlap (the screen's outer rows and columns, past its and
 * the slices' outlines): pixels compared, those that differ and those that differ by more than a texel's colour (a
 * polygon cut at other planes samples a texture a texel off here and there), over the 3D layer's pixels in [topArgb]. For
 * nds_dump's widecheck: call after pk_nds_wide_frame, before the next frame (it compares against the slices drawn for
 * the frame shown, kept from before that call). */
void pk_nds_wide_check(melonDS::NDS* nds, const uint32_t* topArgb, int* compared, int* differ, int* far);

/** Engine A's text BG [bg] at BG pixel ([bx], [by]) - the scroll already added, wrapped to the BG's size - as the
 * hardware reads it (its map and tiles in VRAM, 16 / 256 colours, flips, the standard palette): 0x8000 | BGR555, or 0
 * where clear. For a battle's wide frame and nds_dump's bgdump. */
uint16_t pk_nds_bg_pixel(melonDS::NDS* nds, int bg, int bx, int by);

/** How many polygons each slice got last frame (8: above-left, above, above-right, left, right, below-left, below,
 * below-right): a debugging read. */
void pk_nds_wide_counts(int out[8]);
