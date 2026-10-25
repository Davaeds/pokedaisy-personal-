/* Native single-screen host for PokeDaisy. GPL-3.0, see ../NOTICE. */
#include "../3ds/desktop/pd_png.h"
#include "pd_menu.h"
#include "pd_romart.h"
#include "pd_settings.h"
#include "pd_ui.h"
#include "pd_ui_internal.h"
#include "host_io.h"
#include "../app/src/main/cpp/pk_wide.h"
#include <SDL.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <mgba-util/vfs.h>
#include <mgba/core/blip_buf.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define W 512
#define H 384
static uint32_t pixels[W * H];
static color_t video[256 * 256];
static struct pd_canvas canvas;
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture, *gameTexture;
static uint32_t gamePixels[256 * 256];
static SDL_Rect lastGameRect;
/* EXPAND (the app's pk_wide.c): the overworld rebuilt past the game's edges
 * into a window the screen's shape, at the scale the game's own picture gets
 * (ZOOM OUT: one whole scale less), so the original pixels stay where they were. */
static SDL_Texture *wideTexture;
static uint32_t wideSource[240 * 160], wideFrame[PK_WIDE_MAX_VIEW_W * PK_WIDE_MAX_VIEW_H],
    widePixels[PK_WIDE_MAX_VIEW_W * PK_WIDE_MAX_VIEW_H];
static bool wideSupported, wideReady;
static unsigned wideW, wideH;
static SDL_AudioDeviceID audio;
static volatile sig_atomic_t running = 1;
static bool muted;
static int appError;
#ifdef POCKET_TEST
static unsigned audioNonzero;
#endif
static const char *root = ".";
static char message[128];
static unsigned messageUntil;
enum {
  A,
  B,
  SELECT,
  START,
  RIGHT,
  LEFT,
  UP,
  DOWN,
  R,
  L,
  X,
  Y,
  L2,
  R2,
  MENU,
  NKEYS
};
static SDL_GameController *controller;
static bool rawTrimui;
static unsigned held, pressed;
static unsigned repeatAt[NKEYS];
static void stop(int sig) {
  (void)sig;
  running = 0;
}
static void notice(const char *s) {
  snprintf(message, sizeof message, "%s", s);
  messageUntil = SDL_GetTicks() + 2500;
  fprintf(stderr, "%s\n", s);
}
/* mGBA logs every DMA and BIOS call at DEBUG; keep warnings and errors only, so
 * launch.sh's log stays readable. */
static void quiet_log(struct mLogger *l, int category, enum mLogLevel level,
                      const char *format, va_list args) {
  (void)l;
  if (!(level & (mLOG_FATAL | mLOG_ERROR | mLOG_WARN)))
    return;
  fprintf(stderr, "%s: ", mLogCategoryName(category));
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
}
static struct mLogger quietLogger = {.log = quiet_log};
static void post_audio(struct mAVStream *s, blip_t *l, blip_t *r) {
  (void)s;
  int16_t out[2048];
  int n = blip_samples_avail(l);
  if (n > 1024)
    n = 1024;
  blip_read_samples(l, out, n, true);
  blip_read_samples(r, out + 1, n, true);
#ifdef POCKET_TEST
  for (int i = 0; i < n * 2; i++)
    if (out[i])
      audioNonzero++;
#endif
  if (audio && !muted && SDL_GetQueuedAudioSize(audio) < 32768)
    SDL_QueueAudio(audio, out, n * 4);
}
static bool read_memory(void *ctx, uint32_t a, void *out, size_t n) {
  struct mCore *c = ctx;
  unsigned char *p = out;
  if (c->platform(c) == mPLATFORM_GBA && a >= 0x08000000 && a < 0x0e000000) {
    size_t size = 0;
    const unsigned char *r = c->getMemoryBlock(c, 0x08, &size);
    size_t start = (a - 0x08000000) & 0x01ffffff;
    if (!r || start > size || n > size - start)
      return false;
    memcpy(p, r + start, n);
    return true;
  }
  for (size_t i = 0; i < n; i++)
    p[i] = c->busRead8(c, a + (uint32_t)i);
  return true;
}
static int keyboard(SDL_Keycode k) {
  switch (k) {
  case SDLK_z:
    return A;
  case SDLK_x:
    return B;
  case SDLK_BACKSPACE:
    return SELECT;
  case SDLK_RETURN:
    return START;
  case SDLK_RIGHT:
    return RIGHT;
  case SDLK_LEFT:
    return LEFT;
  case SDLK_UP:
    return UP;
  case SDLK_DOWN:
    return DOWN;
  case SDLK_e:
    return R;
  case SDLK_q:
    return L;
  case SDLK_TAB:
    return X;
  case SDLK_f:
    return Y;
  case SDLK_1:
    return L2;
  case SDLK_2:
    return R2;
  case SDLK_ESCAPE:
    return MENU;
  default:
    return -1;
  }
}
static int controller_button(int button) {
  switch(button) {
    case SDL_CONTROLLER_BUTTON_A:return B;
    case SDL_CONTROLLER_BUTTON_B:return A;
    case SDL_CONTROLLER_BUTTON_X:return Y;
    case SDL_CONTROLLER_BUTTON_Y:return X;
    case SDL_CONTROLLER_BUTTON_BACK:return SELECT;
    case SDL_CONTROLLER_BUTTON_START:return START;
    case SDL_CONTROLLER_BUTTON_GUIDE:return MENU;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:return L;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:return R;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:return UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:return DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:return LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:return RIGHT;
    default:return -1;
  }
}
static int joy(int n) {
  static const int map[] = {B, A, Y, X, L, R, SELECT, START, MENU, -1, L2, R2};
  return n >= 0 && n < 12 ? map[n] : -1;
}
static void key(int k, bool down) {
  if (k < 0)
    return;
  unsigned bit = 1u << k;
  if (down) {
    if (!(held & bit)) {
      pressed |= bit;
      repeatAt[k] = SDL_GetTicks() + 400;
    }
    held |= bit;
  } else
    held &= ~bit;
}
static void events(void) {
  pressed = 0;
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT)
      running = 0;
    if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
      if (!e.key.repeat)
        key(keyboard(e.key.keysym.sym), e.type == SDL_KEYDOWN);
    }
    if(e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP)
      key(controller_button(e.cbutton.button),e.type == SDL_CONTROLLERBUTTONDOWN);
    if(e.type == SDL_CONTROLLERAXISMOTION) {
      if(e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT)key(L2,e.caxis.value>16000);
      if(e.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT)key(R2,e.caxis.value>16000);
      if(e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTX) {key(LEFT,e.caxis.value < -16000);key(RIGHT,e.caxis.value > 16000);}
      if(e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTY) {key(UP,e.caxis.value < -16000);key(DOWN,e.caxis.value > 16000);}
    }
    if (!controller && rawTrimui && (e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP))
      key(joy(e.jbutton.button), e.type == SDL_JOYBUTTONDOWN);
    if (!controller && rawTrimui && e.type == SDL_JOYHATMOTION) {
      key(UP, e.jhat.value & SDL_HAT_UP);
      key(DOWN, e.jhat.value & SDL_HAT_DOWN);
      key(LEFT, e.jhat.value & SDL_HAT_LEFT);
      key(RIGHT, e.jhat.value & SDL_HAT_RIGHT);
    }
    if (!controller && rawTrimui && e.type == SDL_JOYAXISMOTION) {
      if (e.jaxis.axis == 2)
        key(L2, e.jaxis.value > 16000);
      if (e.jaxis.axis == 5)
        key(R2, e.jaxis.value > 16000);
    }
    if (e.type == SDL_WINDOWEVENT &&
        e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
      held = 0;
  }
}
static void repeat_directions(void) {
  for (int k = RIGHT; k <= DOWN; k++)
    if ((held & (1u << k)) && SDL_GetTicks() >= repeatAt[k]) {
      pressed |= 1u << k;
      repeatAt[k] = SDL_GetTicks() + 90;
    }
}
static bool down(int k) { return (pressed & (1u << k)) != 0; }
static void present(void) {
  SDL_RenderSetLogicalSize(renderer, W, H);
  if (SDL_GetTicks() < messageUntil) {
    pd_fill(&canvas, 4, 334, 504, 24, 0x181818);
    pd_text_fit(&canvas, 10, 338, 492, message, 0xffffff, PD_NO_SHADOW);
  }
  if (SDL_UpdateTexture(texture, NULL, pixels, W * 4) < 0) {
    notice(SDL_GetError());
    return;
  }
  SDL_RenderClear(renderer);
  if (SDL_RenderCopy(renderer, texture, NULL, NULL) < 0) {
    notice(SDL_GetError());
    return;
  }
  SDL_RenderPresent(renderer);
}
/* The EXPAND window for an ow x oh screen and a fw x fh game: the screen's
 * size over the game's whole scale (one less for ZOOM OUT), within pk_wide's
 * largest view, never smaller than the game, with even margins so the game's
 * own pixels sit exactly in the middle. */
static void wide_window(int ow, int oh, unsigned fw, unsigned fh, int aspect,
                        unsigned *w, unsigned *h) {
  int scale = ow / (int)fw < oh / (int)fh ? ow / (int)fw : oh / (int)fh;
  if (aspect == PD_ASPECT_ZOOM_OUT)
    scale--;
  if (scale < 1)
    scale = 1;
  unsigned x = (unsigned)ow / scale, y = (unsigned)oh / scale;
  x = x > PK_WIDE_MAX_VIEW_W ? PK_WIDE_MAX_VIEW_W : x < fw ? fw : x;
  y = y > PK_WIDE_MAX_VIEW_H ? PK_WIDE_MAX_VIEW_H : y < fh ? fh : y;
  *w = x - ((x - fw) & 1);
  *h = y - ((y - fh) & 1);
}
/* After every frame while EXPAND can run: a frame nobody sees (fast-forward)
 * only keeps pk_wide's RAM history. wideReady = this frame shows the window:
 * the plain overworld (text boxes too with AUTO ZOOM IN off), its edges all
 * map; anything else (battles, menus, transitions) shows the game's picture. */
static void wide_tick(struct mCore *c, bool skip, const struct pd_settings *s) {
  wideReady = false;
  if (!wideSupported)
    return;
  unsigned fw = (unsigned)pk_wide_frame_w(), fh = (unsigned)pk_wide_frame_h();
  for (unsigned y = 0; y < fh; y++)
    memcpy(wideSource + y * fw, video + y * 256, fw * sizeof *wideSource);
  if (skip || s->aspect == PD_ASPECT_ORIGINAL) {
    pk_wide_skip(c, wideSource);
    return;
  }
  int ow = W, oh = H;
  if (SDL_GetRendererOutputSize(renderer, &ow, &oh) < 0 || ow <= 0 || oh <= 0)
    ow = W, oh = H;
  wide_window(ow, oh, fw, fh, s->aspect, &wideW, &wideH);
  struct PkWideView v = {((int)fw - (int)wideW) / 2, ((int)fh - (int)wideH) / 2,
                         (int)wideW, (int)wideH};
  int mix = pk_wide_view(c, wideSource, wideFrame, v, NULL);
  int scene = pk_wide_scene();
  wideReady = mix == 256 && pk_wide_last_mode() == PK_WIDE_MODE_MAP &&
              !pk_wide_fault() &&
              (scene == PK_WIDE_SCENE_FIELD ||
               (!s->autoZoom && scene == PK_WIDE_SCENE_TEXT));
}
// Gameplay scales in physical panel pixels; the companion keeps its 512x384
// grid.
static void present_game(unsigned gw, unsigned gh) {
  int ow, oh;
  if (SDL_GetRendererOutputSize(renderer, &ow, &oh) < 0 || ow <= 0 || oh <= 0)
    return;
  SDL_RenderSetLogicalSize(renderer, ow, oh);
  int scale = ow / (int)gw;
  if (oh / (int)gh < scale)
    scale = oh / (int)gh;
  if (scale < 1)
    scale = 1;
  lastGameRect =
      (SDL_Rect){(ow - (int)gw * scale) / 2, (oh - (int)gh * scale) / 2,
                 (int)gw * scale, (int)gh * scale};
  for (unsigned y = 0; y < gh; y++)
    for (unsigned x = 0; x < gw; x++) {
      uint32_t native = video[y * 256 + x];
      gamePixels[y * 256 + x] =
          ((native & 255) << 16) | (native & 0xff00) | ((native >> 16) & 255);
    }
  if (SDL_UpdateTexture(gameTexture, NULL, gamePixels, 256 * 4) < 0) {
    notice(SDL_GetError());
    return;
  }
  SDL_Texture *drawn = gameTexture;
  if (wideReady) {
    // The same scale: the window's middle lands on the game's own picture.
    gw = wideW;
    gh = wideH;
    scale = ow / (int)gw < oh / (int)gh ? ow / (int)gw : oh / (int)gh;
    if (scale < 1)
      scale = 1;
    lastGameRect = (SDL_Rect){(ow - (int)gw * scale) / 2, (oh - (int)gh * scale) / 2,
                              (int)gw * scale, (int)gh * scale};
    for (unsigned i = 0; i < gw * gh; i++) {
      uint32_t n = wideFrame[i];
      widePixels[i] = ((n & 255) << 16) | (n & 0xff00) | ((n >> 16) & 255);
    }
    if (SDL_UpdateTexture(wideTexture, &(SDL_Rect){0, 0, (int)gw, (int)gh},
                          widePixels, (int)gw * 4) < 0) {
      notice(SDL_GetError());
      return;
    }
    drawn = wideTexture;
  }
  SDL_RenderClear(renderer);
  SDL_Rect src = {0, 0, (int)gw, (int)gh};
  if (SDL_RenderCopy(renderer, drawn, &src, &lastGameRect) < 0) {
    notice(SDL_GetError());
    return;
  }
  SDL_RenderPresent(renderer);
}
static bool capture_frame(const char *path, unsigned *lit) {
  int ow, oh;
  if (SDL_GetRendererOutputSize(renderer, &ow, &oh) < 0 || ow <= 0 || oh <= 0 ||
      ow > 8192 || oh > 8192)
    return false;
  uint32_t *shot = malloc((size_t)ow * oh * 4);
  if (!shot)
    return false;
  bool ok = SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGB888, shot,
                                 ow * 4) == 0;
  if (ok && lit) {
    *lit = 0;
    for (int i = 0; i < ow * oh; i++)
      if (shot[i] & 0xffffff)
        (*lit)++;
  }
  if (ok && path)
    ok = pd_png_write(path, ow, oh, shot);
  free(shot);
  return ok;
}
static bool state(struct mCore *c, const char *path, bool save) {
  char tmp[1200];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  struct VFile *f = VFileOpen(save ? tmp : path,
                              save ? O_CREAT | O_TRUNC | O_RDWR : O_RDONLY);
  if (!f)
    return false;
  bool ok = save ? mCoreSaveStateNamed(c, f, SAVESTATE_RTC)
                 : mCoreLoadStateNamed(c, f, SAVESTATE_RTC);
  if(!f->close(f))ok=false;
  if (ok && !save) {
    pk_wide_reset(); // EXPAND's RAM history is from before the jump
    wideReady = false;
  }
  if (save) {
    if (ok)
      ok = rename(tmp, path) == 0;
    else
      unlink(tmp);
  }
  return ok;
}
static void focus_move(struct pd_ui *u, int *focus, int direction) {
  if (!u->hitCount) {
    *focus = -1;
    return;
  }
  if (*focus < 0 || *focus >= u->hitCount) {
    *focus = 0;
    return;
  }
  const struct pd_hit *h = &u->hits[*focus];
  int best = -1, score = 1000000;
  for (int i = 0; i < u->hitCount; i++) {
    if (i == *focus)
      continue;
    struct pd_hit *t = &u->hits[i];
    if (t->id == HIT_SCRIM || t->id == HIT_BAG_LIST || t->id == HIT_DEX_LIST ||
        t->id == HIT_GUIDE_LIST)
      continue;
    int dx = (t->x + t->w / 2) - (h->x + h->w / 2),
        dy = (t->y + t->h / 2) - (h->y + h->h / 2);
    int along = direction == LEFT    ? -dx
                : direction == RIGHT ? dx
                : direction == UP    ? -dy
                                     : dy;
    int across = direction == LEFT || direction == RIGHT ? abs(dy) : abs(dx);
    if (along > 0 && along + across * 4 < score) {
      best = i;
      score = along + across * 4;
    }
  }
  if (best >= 0)
    *focus = best;
  else if (direction == UP || direction == DOWN) {
    int d = direction == DOWN ? 36 : -36;
    if (u->overlay == PD_OVERLAY_PLACES)
      u->placesScroll += d;
    else if (u->tab == PD_TAB_BAG)
      u->bagScroll += d;
    else if (u->tab == PD_TAB_DEX)
      u->dexScroll += d;
    else if (u->tab == PD_TAB_GUIDE)
      u->guideScroll += d;
  }
}
static void focus_draw(struct pd_ui *u, int focus) {
  if (focus < 0 || focus >= u->hitCount)
    return;
  struct pd_hit *h = &u->hits[focus];
  pd_fill(&canvas, h->x, h->y, h->w, 2, 0xff7331);
  pd_fill(&canvas, h->x, h->y + h->h - 2, h->w, 2, 0xff7331);
  pd_fill(&canvas, h->x, h->y, 2, h->h, 0xff7331);
  pd_fill(&canvas, h->x + h->w - 2, h->y, 2, h->h, 0xff7331);
}
#ifdef POCKET_TEST
static void test_frame(struct mCore *, bool, int, struct pd_ui *,
                       struct pd_snapshot *);
#endif
static void run_game(const char *path, int maxFrames, const char *shot) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    appError = 1;
    notice("Cannot open ROM");
    return;
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  rewind(f);
  if (len < 0x150 || len > 40 * 1024 * 1024) {
    fclose(f);
    appError = 1;
    notice("Invalid ROM size");
    return;
  }
  unsigned char *romdata = malloc((size_t)len);
  if (!romdata || fread(romdata, 1, len, f) != (size_t)len) {
    free(romdata);
    fclose(f);
    appError = 1;
    notice("Cannot read ROM");
    return;
  }
  fclose(f);
  struct VFile *vf = VFileOpen(path, O_RDONLY);
  struct mCore *c = vf ? mCoreFindVF(vf) : NULL;
  if (!c || !c->init(c)) {
    if (vf)
      vf->close(vf);
    free(romdata);
    appError = 1;
    notice("Unsupported ROM");
    return;
  }
  mCoreInitConfig(c, NULL);
  if (c->platform(c) == mPLATFORM_GB) {
    // Match upstream's Android GB setup, including SGB-enhanced cartridges.
    mCoreConfigSetIntValue(&c->config, "sgb.borders", 0);
    c->reloadConfigOption(c, "sgb.borders", &c->config);
  }
  c->setVideoBuffer(c, video, 256);
  c->setAudioBufferSize(c, 512);
  if (!c->loadROM(c, vf)) {
    vf->close(vf);
    mCoreConfigDeinit(&c->config);
    c->deinit(c);
    free(romdata);
    appError = 1;
    notice("ROM load failed");
    return;
  }
  char sav[1024], ss[1024];
  const char *name = strrchr(path, '/');
  name = name ? name + 1 : path;
  snprintf(sav, sizeof sav, "%s/saves/%s.sav", root, name);
  snprintf(ss, sizeof ss, "%s/states/%s.ss", root, name);
  struct pocket_save_status saveStatus = {0};
  if (!pocket_save_backup(sav, time(NULL))) {
    appError = 1;
    notice("Save backup failed; game not opened");
    goto cleanup;
  }
  struct VFile *sv = pocket_save_wrap(VFileOpen(sav, O_CREAT | O_RDWR), &saveStatus);
  if (!sv || !c->loadSave(c, sv)) {
    if (sv)
      sv->close(sv);
    appError = 1;
    notice("Save file unavailable; game not opened");
    goto cleanup;
  }
  struct mAVStream stream = {.postAudioBuffer = post_audio};
  c->setAVStream(c, &stream);
  for (int i = 0; i < 2; i++)
    blip_set_rates(c->getAudioChannel(c, i), c->frequency(c), 32768);
  c->reset(c);
  uint32_t crc = 0;
  c->checksum(c, &crc, mCHECKSUM_CRC32);
  wideSupported = pk_wide_configure(crc) != 0;
  wideReady = false;
  unsigned gw, gh;
  c->desiredVideoDimensions(c, &gw, &gh);
  struct pd_game game;
  pd_game_detect(&game, romdata, (size_t)len);
  game.rom = romdata;
  game.romSize = (size_t)len;
  if (c->platform(c) == mPLATFORM_GBA)
    pd_romart_scan(&game);
  char config[1024];
  snprintf(config, sizeof config, "%s/settings.ini", root);
  struct pd_settings settings;
  pd_settings_load(&settings, config);
  struct pd_ui ui;
  pd_ui_init(&ui, &settings);
  struct pd_snapshot snap = {0};
  struct pd_host_info host = {.hideScreenMode = true, .hasExpand = true,
                              .expandGame = wideSupported, .count = 1, .labels = {"HOST"}};
  snprintf(host.values[0], 40, "PokeDaisy Linux");
  const char *opts[] = {"Resume game",
                        "Party",
                        "Save state",
                        "Load state",
                        "Toggle fast-forward",
                        "Quit to game list",
                        "About"};
  const char *confirmOpts[] = {"Cancel", "Confirm"};
  struct pd_menu gameMenu, confirmMenu;
  pd_menu_init(&gameMenu, "GAME", opts, 7);
  pd_menu_init(&confirmMenu, "CONFIRM", confirmOpts, 2);
  int menuOpen = 0;
  bool companion = false, ff = false, leave = false;
  int focus = 0, frames = 0;
  uint64_t due = SDL_GetPerformanceCounter();
  double hz = (double)c->frequency(c) / c->frameCycles(c);
  while (running && !leave) {
    if(saveStatus.failed) { appError=1; notice("Save I/O failed; stopping game"); break; }
    events();
    repeat_directions();
    if (down(X)) {
      menuOpen = 0;
      companion = !companion;
      c->setKeys(c, 0);
      if (audio)
        SDL_ClearQueuedAudio(audio);
    }
    if (down(MENU) || ((down(START) || down(SELECT)) &&
                       (held & (1u << START)) && (held & (1u << SELECT)))) {
      menuOpen = menuOpen ? 0 : 1;
      companion = true;
      c->setKeys(c, 0);
      if (audio)
        SDL_ClearQueuedAudio(audio);
    }
    if (down(Y) && !companion)
      ff = !ff;
    if (companion) {
      if (menuOpen) {
        if (menuOpen == 4) {
          if (down(B) || down(A))
            menuOpen = 1;
        } else {
          struct pd_menu *pm = menuOpen == 1 ? &gameMenu : &confirmMenu;
          if (down(UP))
            pd_menu_move(pm, -1);
          if (down(DOWN))
            pd_menu_move(pm, 1);
          if (down(B)) {
            if (menuOpen == 1) {
              menuOpen = 0;
              companion = false;
            } else
              menuOpen = 1;
          } else if (down(A)) {
            if (menuOpen == 1) {
              switch (gameMenu.selected) {
              case 0:
                menuOpen = 0;
                companion = false;
                break;
              case 1:
                menuOpen = 0;
                ui.tab = PD_TAB_PARTY;
                break;
              case 2:
                menuOpen = 5;
                confirmMenu.title = "Overwrite save state?";
                confirmMenu.selected = 0;
                break;
              case 3:
                menuOpen = 2;
                confirmMenu.title = "Load state? Current progress will rewind.";
                confirmMenu.selected = 0;
                break;
              case 4:
                ff = !ff;
                notice(ff ? "Fast-forward on" : "Fast-forward off");
                break;
              case 5:
                menuOpen = 3;
                confirmMenu.title = "Quit? Save in-game first.";
                confirmMenu.selected = 0;
                break;
              case 6:
                menuOpen = 4;
                break;
              }
            } else {
              if (confirmMenu.selected == 1) {
                if (menuOpen == 3)
                  leave = true;
                else if (menuOpen == 5)
                  notice(state(c, ss, true) ? "State saved"
                                            : "State save failed");
                else
                  notice(state(c, ss, false) ? "State loaded"
                                             : "State load failed");
              }
              menuOpen = 1;
            }
          }
        }
      } else {
        if (down(B) && !pd_ui_back(&ui))
          companion = false;
        if (down(L) || down(R)) {
          pd_ui_next_tab(&ui, down(R) ? 1 : -1);
          focus = 0;
        }
        for (int k = RIGHT; k <= DOWN; k++)
          if (down(k))
            focus_move(&ui, &focus, k);
        if (down(A) && focus >= 0 && focus < ui.hitCount) {
          struct pd_hit hit = ui.hits[focus];
          pd_ui_touch(&ui, hit.x + hit.w / 2, hit.y + hit.h / 2, true);
          pd_ui_touch(&ui, hit.x + hit.w / 2, hit.y + hit.h / 2, false);
          focus = 0;
        }
        switch (pd_ui_take_action(&ui)) {
        case PD_ACTION_LEAVE_GAME:
          leave = true;
          break;
        case PD_ACTION_TOGGLE_FF:
          ff = !ff;
          break;
        case PD_ACTION_SAVE_STATE:
          notice(state(c, ss, true) ? "State saved" : "State save failed");
          break;
        case PD_ACTION_LOAD_STATE:
          notice(state(c, ss, false) ? "State loaded" : "State load failed");
          break;
        case PD_ACTION_SETTINGS_CHANGED:
          pd_settings_save(&settings, config);
          break;
        default:
          break;
        }
      }
      if (down(L2)) {
        menuOpen = 5;
        confirmMenu.title = "Overwrite save state?";
        confirmMenu.selected = 0;
      }
      if (down(R2)) {
        menuOpen = 2;
        confirmMenu.title = "Load state? Current progress will rewind.";
        confirmMenu.selected = 0;
      }
    } else {
      muted = ff;
      c->setKeys(c, held & 1023);
      int count = ff ? settings.ffSpeed : 1;
      for (int i = 0; i < count; i++) {
        c->runFrame(c);
        wide_tick(c, i < count - 1, &settings);
      }
    }
    if(!companion || !snap.valid)pd_snapshot_read(&snap, &game, read_memory, c);
    if (companion) {
      if (menuOpen) {
        if (menuOpen == 4) {
          pd_splash_draw(&canvas, "Based on PokeDaisy by Lidor Itzhari",
                         "https://github.com/lidor30/pokedaisy",
                         "GPL-3.0 | mGBA MPL-2.0 | B: Back");
        } else
          pd_menu_draw(menuOpen == 1 ? &gameMenu : &confirmMenu, &canvas);
      } else {
        pd_ui_update(&ui, &snap);
        host.ffOn = ff;
        host.hasState = access(ss, F_OK) == 0;
        pd_ui_draw(&ui, &canvas, &game, &snap, &host);
        focus_draw(&ui, focus);
      }
      present();
    } else {
      present_game(gw, gh);
    }
#ifdef POCKET_TEST
    test_frame(c, companion, frames, &ui, &snap);
#endif
    frames++;
    if (maxFrames && frames >= maxFrames) {
      if (shot) {
        unsigned lit = 0;
        if (!capture_frame(shot, &lit))
          notice("Capture failed");
        fprintf(stderr,
                "Capture frame %u: %ux%u game, rect %d,%d %dx%d, lit=%u\n",
                c->frameCounter(c), gw, gh, lastGameRect.x, lastGameRect.y,
                lastGameRect.w, lastGameRect.h, lit);
      }
      break;
    }
    due += (uint64_t)(SDL_GetPerformanceFrequency() / hz);
    uint64_t now = SDL_GetPerformanceCounter();
    if (now < due)
      SDL_Delay((Uint32)((due - now) * 1000 / SDL_GetPerformanceFrequency()));
    else if (now - due > SDL_GetPerformanceFrequency() / 5)
      due = now;
  }
  if (audio)
    SDL_ClearQueuedAudio(audio);
cleanup:
  pk_wide_configure(0);
  wideSupported = wideReady = false;
  c->unloadROM(c);
  if(saveStatus.failed)appError=1;
  mCoreConfigDeinit(&c->config);
  c->deinit(c);
  free(romdata);
  held = pressed = 0;
}
static int cmp(const void *a, const void *b) {
  return strcasecmp(*(char *const *)a, *(char *const *)b);
}
int main(int argc, char **argv) {
  const char *rom = NULL, *shot = NULL;
  int frames = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--data") && i + 1 < argc)
      root = argv[++i];
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
      frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
      shot = argv[++i];
    else
      rom = argv[i];
  }
  mLogSetDefaultLogger(&quietLogger);
  signal(SIGINT, stop);
  signal(SIGTERM, stop);
  mkdir(root, 0755);
  char path[1024];
  const char *dirs[] = {"roms", "saves", "states"};
  for (int i = 0; i < 3; i++) {
    snprintf(path, sizeof path, "%s/%s", root, dirs[i]);
    mkdir(path, 0755);
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER)) {
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_DisplayMode display = {0};
  bool fullscreen = getenv("POCKET_FULLSCREEN") != NULL;
  if(fullscreen && SDL_GetCurrentDisplayMode(0,&display))fullscreen=false;
  // POCKET_WINDOW=WxH: a window the size of a device's panel, for trying a
  // layout on a desktop (1280x720 Smart Pro, 640x480, 720x720 ...).
  int winW = W, winH = H;
  const char *size = getenv("POCKET_WINDOW");
  if (size && (sscanf(size, "%dx%d", &winW, &winH) != 2 || winW < 160 ||
               winH < 144 || winW > 8192 || winH > 8192))
    winW = W, winH = H;
  window = SDL_CreateWindow("PokeDaisy Linux", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, fullscreen?display.w:winW, fullscreen?display.h:winH,
      fullscreen?SDL_WINDOW_FULLSCREEN:SDL_WINDOW_RESIZABLE);
  renderer = window ? SDL_CreateRenderer(window, -1, 0) : NULL;
  if (!renderer) {
    fprintf(stderr, "Video: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_RendererInfo rendererInfo;
  if (!SDL_GetRendererInfo(renderer, &rendererInfo))
    fprintf(stderr, "Video backend: %s; opaque RGB888 canvas %dx%d\n",
            rendererInfo.name, W, H);
  SDL_RenderSetLogicalSize(renderer, W, H);
  SDL_ShowCursor(SDL_DISABLE);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888,
                              SDL_TEXTUREACCESS_STREAMING, W, H);
  gameTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888,
                                  SDL_TEXTUREACCESS_STREAMING, 256, 256);
  wideTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888,
                                  SDL_TEXTUREACCESS_STREAMING, PK_WIDE_MAX_VIEW_W,
                                  PK_WIDE_MAX_VIEW_H);
  if (!texture || !gameTexture || !wideTexture) {
    fprintf(stderr, "Texture: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
  SDL_SetTextureBlendMode(gameTexture, SDL_BLENDMODE_NONE);
  SDL_SetTextureBlendMode(wideTexture, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  pd_canvas_init(&canvas, W, H, pixels);
  SDL_AudioSpec want = {0};
  want.freq = 32768;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 512;
  audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
  if (audio)
    SDL_PauseAudioDevice(audio, 0);
  else
    fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError());
  rawTrimui=getenv("POCKET_RAW_TRIMUI") && !strcmp(getenv("POCKET_RAW_TRIMUI"),"1");
  for(int i=0;i<SDL_NumJoysticks() && !controller;i++)
    if(SDL_IsGameController(i))controller=SDL_GameControllerOpen(i);
  SDL_Joystick *js = !controller && rawTrimui && SDL_NumJoysticks() ? SDL_JoystickOpen(0) : NULL;
  if (rom)
    run_game(rom, frames, shot);
  else {
    char *names[256];
    int count = 0;
    snprintf(path, sizeof path, "%s/roms", root);
    DIR *d = opendir(path);
    struct dirent *e;
    while (d && (e = readdir(d)) && count < 256) {
      char *ext = strrchr(e->d_name, '.');
      if (ext && (!strcasecmp(ext, ".gba") || !strcasecmp(ext, ".gbc") ||
                  !strcasecmp(ext, ".gb")))
        names[count++] = strdup(e->d_name);
    }
    if (d)
      closedir(d);
    qsort(names, count, sizeof(*names), cmp);
    struct pd_menu menu;
    pd_menu_init(&menu, "PokeDaisy Linux", (const char *const *)names, count);
    int ticks = 0;
    while (running) {
      events();
      repeat_directions();
      if (down(B) || down(MENU))
        break;
      if (down(UP))
        pd_menu_move(&menu, -1);
      if (down(DOWN))
        pd_menu_move(&menu, 1);
      if (down(A) && count) {
        snprintf(path, sizeof path, "%s/roms/%s", root, names[menu.selected]);
        run_game(path, 0, NULL);
      }
      pd_menu_draw(&menu, &canvas);
      present();
      if (frames && ++ticks >= frames) {
        if (shot)
          capture_frame(shot, NULL);
        break;
      }
      SDL_Delay(16);
    }
    for (int i = 0; i < count; i++)
      free(names[i]);
  }
  if (js)
    SDL_JoystickClose(js);
  if(controller) {SDL_GameControllerClose(controller);controller=NULL;}
  if (audio)
    SDL_CloseAudioDevice(audio);
  SDL_DestroyTexture(texture);
  SDL_DestroyTexture(gameTexture);
  SDL_DestroyTexture(wideTexture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return appError;
}
