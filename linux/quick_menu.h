/* The QUICK MENU (the Y button): the app's QuickMenu.kt in its default RING style and GLASS look, drawn into
 * a canvas whose see-through pixels carry their alpha (the host lays it over the game). GPL-3.0. */
#ifndef POCKET_QUICK_MENU_H
#define POCKET_QUICK_MENU_H
#include "pd_canvas.h"
#include <stdbool.h>

enum quick_action { QUICK_WILD, QUICK_SAVE_STATE, QUICK_LOAD_STATE, QUICK_FF, QUICK_COMPANION, QUICK_COUNT };

struct quick_item {
  enum quick_action action;
  bool off;          // WILD ENCOUNTERS switched off: an OFF tag (QuickTile's)
  bool lit;          // FF running
  const char *why;   // why it can't run now (dimmed), or NULL
};

/* What a canvas pixel holds where nothing was drawn (fully see-through). */
#define QUICK_CLEAR 0x00FF00FFu

/* Draws the ring (n tiles, sel highlighted) and the middle's hint over QUICK_CLEAR; returns false if it
 * doesn't fit the canvas. */
bool quick_draw(struct pd_canvas *c, const struct quick_item *items, int n, int sel);
/* The tile a D-pad press (dx, dy: -1 / 0 / 1) moves to from sel: the nearest one that way round the ring. */
int quick_step(int n, int sel, int dx, int dy, int w, int h);
const char *quick_label(enum quick_action a);
/* After drawing: every pixel opaque but the clear ones and the glass's own alpha. */
void quick_finish(struct pd_canvas *c);
#endif
