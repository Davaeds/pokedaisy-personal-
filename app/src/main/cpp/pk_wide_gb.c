// PokeDaisy — EXPAND on the Game Boy (pk_wide_gb.c): Pokémon Yellow's overworld rebuilt past its 160 x 144 screen.
//
// pk_wide.c hands every call over to this file while a Game Boy game is configured (pk_wide_gb_configure). The GBA's
// rebuild reads a map of metatiles; a Gen 1 game keeps less, and all of it at hand:
//
// - wOverworldMap: the map's blocks inside a 3-block border, which holds the connected maps' edges and, elsewhere,
//   the map's background block (wMapBackgroundTile);
// - the tileset's blockset in ROM (wTilesetBank:wTilesetBlocksPtr): 4 x 4 tile ids a block;
// - the tiles in VRAM, and the Color palettes as mGBA's renderer has them (a tile id's palette is learned from the
//   BG map's attributes wherever that tile is on screen);
// - past the border, the connected maps go on (their blocks from the ROM, through MapHeaderBanks / Pointers), and
//   past those the background block.
//
// Where the screen sits in that world isn't taken from RAM (it's a frame ahead of the picture, and mid-step the
// scroll and the block pointer move at different times): the visible BG map's tile ids are matched against the
// world around RAM's guess, and the fine scroll is SCX / SCY as drawn. The match is the check too: a frame that
// isn't the field (a menu screen, a battle, a transition's wipe) doesn't match, and the strips fade into a glow
// spread from the frame's edges, as on the GBA. Text boxes are font tiles (0x60 and up) and don't count either way.
//
// Objects: what OAM holds (the player, Pikachu, NPCs on screen - one stepping off the edge carries on into the
// strips), plus the NPCs the game hides because they're off screen (image index $FF; Gen 1 freezes them there),
// drawn standing where they stand from wSpriteStateData1 / 2 - unless the game hides them for good (a toggleable
// object switched off: an item taken, a Pokémon caught).

#include "pk_wide.h"

#include <stdbool.h>
#include <string.h>

#include <mgba/core/core.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/io.h>
#include <mgba/internal/gb/renderers/software.h>

#define GBW PK_WIDE_GB_W
#define GBH PK_WIDE_GB_H

/** A game's addresses (WRAM unless noted). */
enum {
    GB_SPRITES1,          // wSpriteStateData1: 16 x 16 bytes (picture id, image index $FF = hidden, screen Y / X, facing)
    GB_SPRITES2,          // wSpriteStateData2: map Y / X + 4 at +4 / +5, grass priority +7, VRAM slot +0xE
    GB_OVERWORLD,         // wOverworldMap: (width + 6) x (height + 6) blocks
    GB_VIEW_PTR,          // wCurrentTileBlockMapViewPointer: the screen's top-left block, in wOverworldMap
    GB_YCOORD,            // wYCoord (wXCoord follows)
    GB_YBLOCK,            // wYBlockCoord (wXBlockCoord follows): which half of that block
    GB_TILESET,           // wCurMapTileset
    GB_MAP_HEIGHT,        // wCurMapHeight (wCurMapWidth follows)
    GB_CONNECTIONS,       // wCurMapConnections (bit 3 north .. 0 east); the four 11-byte headers follow, north first
    GB_TILESET_BANK,      // wTilesetBank (wTilesetBlocksPtr follows)
    GB_BG_BLOCK,          // wMapBackgroundTile
    GB_IN_BATTLE,         // wIsInBattle
    GB_FONT_LOADED,       // wFontLoaded: bit 0 while a text box (or the START menu) is up
    GB_TOGGLE_FLAGS,      // wToggleableObjectFlags (a bit per global object: set = hidden)
    GB_TOGGLE_LIST,       // wToggleableObjectList: (sprite slot, global object) pairs, $FF-terminated
    GB_MAP_HEADER_PTRS,   // MapHeaderPointers (ROM, linear offset)
    GB_MAP_HEADER_BANKS,  // MapHeaderBanks (ROM, linear offset)
    GB_START_ITEM,        // wBattleAndStartSavedMenuItem: the START menu item picked last (0 POKéDEX .. 5 OPTION, one less
                          // before the Pokédex)
    GB_EVENT_FLAGS,       // wEventFlags (EVENT_GOT_POKEDEX is flag 37)
    GB_CONFIG
};

static const struct { uint32_t crc; uint32_t a[GB_CONFIG]; } GB_ROMS[] = {
    // Pokémon Yellow (USA, Europe): pret/pokeyellow's symbols (its build is the cart byte for byte).
    { 0x7D527D62u, { 0xC100, 0xC200, 0xC6E8, 0xD35E, 0xD360, 0xD362, 0xD366, 0xD367, 0xD36F, 0xD52A, 0xD3AC, 0xD056,
                     0xCFC3, 0xD5A5, 0xD5CD, 0x3F * 0x4000 + 0x01F2, 0x3F * 0x4000 + 0x03E4, 0xCC2D, 0xD746 } },
};

#define GB_OW_MAX 1300           // wOverworldMap's size
#define GB_MAP_TILES 0x60        // tile ids below this are the map's; the font (text boxes) starts here
#define GB_MIN_SCORE 900         // per mille of the screen's map tiles the world must match
#define GB_MIN_MAP_TILES 120     // ... and at least this many of its 360 tiles are map tiles (else it's a menu screen)
#define GB_OVERLAY_TILES 4       // fewer, all where the field last was: a box over a blanked field (SAVE)
#define GB_START_OPTION 5        // the START menu's OPTION row (counted with POKéDEX)
#define GB_START_TILE_L 0x79     // the START menu's box: '┌' at screen tile (10, 0), '┐' at (19, 0)
#define GB_START_TILE_R 0x7B
#define GB_MIX_STEP 16           // the strips <-> the glow crossfade over 16 frames
#define GB_FADE_LEVEL 12         // a frame this dark (or this near white) everywhere is a fade
#define GB_SEARCH 4              // tiles either way of RAM's guess the screen is looked for
#define GB_DOOR_FRAMES 30        // a screen of map tiles that doesn't match, this soon after the field: a map change

static const uint32_t* gbCfg;
static struct GB* gbB;
static struct GBVideoSoftwareRenderer* gbR;

static inline int gbClamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint8_t gbRam(uint32_t a) {
    if (a >= 0xC000 && a < 0xD000) return gbB->memory.wram[a - 0xC000];
    if (a >= 0xD000 && a < 0xE000) return gbB->memory.wramBank[a - 0xD000];
    return 0;
}
static uint16_t gbRam16(uint32_t a) { return (uint16_t) (gbRam(a) | gbRam(a + 1) << 8); }
static void gbRamCopy(uint8_t* dst, uint32_t a, int n) {
    for (int i = 0; i < n; i++) dst[i] = gbRam(a + (uint32_t) i);
}

/** ROM bytes at a linear offset, or NULL past its end. */
static const uint8_t* gbRom(uint32_t at, uint32_t n) {
    if (!gbB->memory.rom || at + n > gbB->memory.romSize || at + n < at) return NULL;
    return gbB->memory.rom + at;
}
static uint32_t gbLinear(int bank, uint16_t addr) {
    return addr < 0x4000 ? addr : (uint32_t) bank * 0x4000 + (addr - 0x4000u);
}

// --- the world ---------------------------------------------------------------------------------------

struct GbConn {
    bool on;
    int offX, offY;      // the connected map's block (0, 0) is wOverworldMap's block (offX, offY)
    int w, h;
    uint32_t blocks;     // its blocks (ROM, linear)
};

struct GbSnap {
    bool ok;
    uint8_t ow[GB_OW_MAX];
    int w, h;            // the map in blocks; wOverworldMap is (w + 6) x (h + 6)
    int guessX, guessY;  // RAM's screen top-left, in world tiles
    int px, py;          // the player's step (wXCoord / wYCoord)
    uint32_t blockset;   // ROM, linear
    int bgBlock, tileset;
    struct GbConn conn[4];   // north, south, west, east
    uint8_t spr1[256], spr2[256];
    uint8_t toggleFlags[32], toggleList[40];
    bool battle, text;
    int startItem;       // the START menu item picked last, counted with the POKéDEX row (5 = OPTION)
};

static struct GbSnap gbSnaps[2];
static int gbPrev = -1;
// The last snapshot that matched the screen, and where: walking across a map connection, RAM holds the new map a few
// frames before its border has the old view in it - the screen still shows the old one, which this one has.
static struct GbSnap gbGood;
static int gbGoodTx, gbGoodTy;
static bool gbFromStart;   // the START menu was the last thing up on the field (cleared by the plain field, a battle)

static void gbTake(struct GbSnap* s) {
    const uint32_t* A = gbCfg;
    s->ok = false;
    int h = gbRam(A[GB_MAP_HEIGHT]), w = gbRam(A[GB_MAP_HEIGHT] + 1);
    if (!w || !h || (w + 6) * (h + 6) > GB_OW_MAX) return;
    s->w = w;
    s->h = h;
    gbRamCopy(s->ow, A[GB_OVERWORLD], (w + 6) * (h + 6));
    int rel = (int) gbRam16(A[GB_VIEW_PTR]) - (int) A[GB_OVERWORLD];
    if (rel < 0 || rel >= (w + 6) * (h + 6)) rel = 0;
    s->guessX = rel % (w + 6) * 4 + (gbRam(A[GB_YBLOCK] + 1) ? 2 : 0);
    s->guessY = rel / (w + 6) * 4 + (gbRam(A[GB_YBLOCK]) ? 2 : 0);
    s->py = gbRam(A[GB_YCOORD]);
    s->px = gbRam(A[GB_YCOORD] + 1);
    s->blockset = gbLinear(gbRam(A[GB_TILESET_BANK]), gbRam16(A[GB_TILESET_BANK] + 1));
    s->bgBlock = gbRam(A[GB_BG_BLOCK]);
    s->tileset = gbRam(A[GB_TILESET]);
    int flags = gbRam(A[GB_CONNECTIONS]);
    for (int d = 0; d < 4; d++) {
        struct GbConn* c = &s->conn[d];
        c->on = false;
        if (!(flags & (8 >> d))) continue;
        uint32_t hdr = A[GB_CONNECTIONS] + 1 + 11 * (uint32_t) d;
        int id = gbRam(hdr);
        const uint8_t* bank = gbRom(A[GB_MAP_HEADER_BANKS] + (uint32_t) id, 1);
        const uint8_t* ptr = gbRom(A[GB_MAP_HEADER_PTRS] + 2 * (uint32_t) id, 2);
        if (!bank || !ptr) continue;
        const uint8_t* mh = gbRom(gbLinear(bank[0], (uint16_t) (ptr[0] | ptr[1] << 8)), 5);
        // Only a map drawn with the same tileset: another tileset's tiles aren't in VRAM.
        if (!mh || mh[0] != s->tileset || !mh[1] || !mh[2]) continue;
        int ch = mh[1], cw = mh[2];
        uint16_t blocks = (uint16_t) (mh[3] | mh[4] << 8);
        int src = (int) gbRam16(hdr + 1) - (int) blocks;   // the strip's first block, in the connected map
        int dst = (int) gbRam16(hdr + 3) - (int) A[GB_OVERWORLD];   // ... and where it went in wOverworldMap
        if (dst < 0 || dst >= (w + 6) * (h + 6)) continue;
        int srcRow = src >= 0 ? src / cw : -((-src + cw - 1) / cw);
        c->offX = dst % (w + 6) - (src - srcRow * cw);
        c->offY = dst / (w + 6) - srcRow;
        c->w = cw;
        c->h = ch;
        c->blocks = gbLinear(bank[0], blocks);
        c->on = gbRom(c->blocks, (uint32_t) (cw * ch)) != NULL;
    }
    gbRamCopy(s->spr1, A[GB_SPRITES1], 256);
    gbRamCopy(s->spr2, A[GB_SPRITES2], 256);
    gbRamCopy(s->toggleFlags, A[GB_TOGGLE_FLAGS], 32);
    gbRamCopy(s->toggleList, A[GB_TOGGLE_LIST], 40);
    s->battle = gbRam(A[GB_IN_BATTLE]) != 0;
    s->text = gbRam(A[GB_FONT_LOADED]) & 1;
    bool gotDex = (gbRam(A[GB_EVENT_FLAGS] + 37 / 8) >> (37 % 8)) & 1;
    s->startItem = gbRam(A[GB_START_ITEM]) + (gotDex ? 0 : 1);
    s->ok = gbRom(s->blockset, 256 * 16) != NULL;
}

/** The block at wOverworldMap's (bx, by) - anywhere: past the array, a connected map on that side, else the
 * background block. */
static int gbBlock(const struct GbSnap* s, int bx, int by) {
    int W = s->w + 6, H = s->h + 6;
    if (bx >= 0 && by >= 0 && bx < W && by < H) return s->ow[by * W + bx];
    const bool side[4] = { by < 3, by >= s->h + 3, bx < 3, bx >= s->w + 3 };
    for (int d = 0; d < 4; d++) {
        const struct GbConn* c = &s->conn[d];
        if (!c->on || !side[d]) continue;
        int cx = bx - c->offX, cy = by - c->offY;
        if (cx < 0 || cy < 0 || cx >= c->w || cy >= c->h) continue;
        const uint8_t* b = gbRom(c->blocks + (uint32_t) (cy * c->w + cx), 1);
        if (b) return b[0];
    }
    return s->bgBlock;
}

/** The tile id at world tile (tx, ty) - (0, 0) is wOverworldMap's top-left block's. */
static int gbTile(const struct GbSnap* s, int tx, int ty) {
    int blk = gbBlock(s, tx >> 2, ty >> 2);
    const uint8_t* b = gbRom(s->blockset + (uint32_t) blk * 16, 16);
    return b ? b[(ty & 3) * 4 + (tx & 3)] : 0;
}

// --- the screen ----------------------------------------------------------------------------------------

static uint8_t gbLcdc, gbScx, gbScy;   // as the frame was drawn
static bool gbCgb;
// What the screen shows: the BG map scrolled by SCX / SCY - or, while a text box or the START menu is up, the window
// over the whole screen, unscrolled (Gen 1 copies the screen with the box on it into the window's map and shows that).
static int gbMapBase;                // that map in VRAM (0x1800 / 0x1C00)
static int gbSrcX, gbSrcY;           // its scroll
static uint8_t gbTileAttr[256];      // the BG attributes each map tile id was last seen with on screen
static uint8_t gbAttrSeen[256];
static uint32_t gbAttrBlockset;      // ... for this blockset (another tileset forgets them)
static uint8_t gbAttrDefault;

/** The screen's tile (column i, row j of the map's tiles in view) and its attributes. */
static inline int gbScreenTile(int i, int j, uint8_t* attr) {
    int at = gbMapBase + (((gbSrcY >> 3) + j) & 31) * 32 + (((gbSrcX >> 3) + i) & 31);
    if (attr) *attr = gbCgb ? gbB->video.vram[0x2000 + at] : 0;
    return gbB->video.vram[at];
}

static uint8_t gbScreen[18][20];     // this frame's screen tiles (gbScreenTile), read once
static int gbScreenMap;              // ... how many are map tiles

/** Every map tile on screen is the world's at (tx, ty) - however few. */
static bool gbExact(const struct GbSnap* s, int tx, int ty) {
    for (int j = 0; j < 18; j++)
        for (int i = 0; i < 20; i++)
            if (gbScreen[j][i] < GB_MAP_TILES && gbTile(s, tx + i, ty + j) != gbScreen[j][i]) return false;
    return true;
}

static void gbReadScreen(void) {
    gbScreenMap = 0;
    for (int j = 0; j < 18; j++)
        for (int i = 0; i < 20; i++) {
            gbScreen[j][i] = (uint8_t) gbScreenTile(i, j, NULL);
            gbScreenMap += gbScreen[j][i] < GB_MAP_TILES;
        }
}

/** Per mille of the screen's map tiles world tile (tx, ty) onwards matches, or -1 with too few map tiles. With
 * `quick`, gives up (0) as soon as it can't reach GB_MIN_SCORE. */
static int gbScore(const struct GbSnap* s, int tx, int ty, bool quick) {
    if (gbScreenMap < GB_MIN_MAP_TILES) return -1;
    int miss = 0, allowed = gbScreenMap * (1000 - GB_MIN_SCORE) / 1000;
    for (int j = 0; j < 18; j++)
        for (int i = 0; i < 20; i++) {
            int t = gbScreen[j][i];
            if (t >= GB_MAP_TILES || gbTile(s, tx + i, ty + j) == t) continue;
            if (++miss > allowed && quick) return 0;
        }
    return (gbScreenMap - miss) * 1000 / gbScreenMap;
}

/** Learns each map tile id's attributes (palette, flips, bank) from the screen. */
static void gbLearnAttrs(const struct GbSnap* s, int tx, int ty) {
    if (s->blockset != gbAttrBlockset) {
        memset(gbAttrSeen, 0, sizeof(gbAttrSeen));
        gbAttrBlockset = s->blockset;
    }
    int votes[8] = { 0 };
    for (int j = 0; j < 18; j++)
        for (int i = 0; i < 20; i++) {
            uint8_t a;
            int t = gbScreenTile(i, j, &a);
            if (t >= GB_MAP_TILES || gbTile(s, tx + i, ty + j) != t) continue;
            gbTileAttr[t] = a;
            gbAttrSeen[t] = 1;
            votes[a & 7]++;
        }
    int best = 0;
    for (int p = 1; p < 8; p++) if (votes[p] > votes[best]) best = p;
    gbAttrDefault = (uint8_t) best;
}

static inline uint32_t gbOpaque(uint32_t c, uint32_t alpha) { return (c & 0x00FFFFFFu) | alpha; }

static inline uint32_t gbBgColor(uint8_t attr, int idx, uint32_t alpha) {
    int i = gbCgb ? (attr & 7) * 4 + idx : idx;
    return gbOpaque(gbR->palette[gbR->lookup[i]], alpha);
}

static inline uint32_t gbObjColor(uint8_t attr, int idx, uint32_t alpha) {
    int i = 0x20 + (gbCgb ? (attr & 7) * 4 : ((attr >> 4) & 1) * 4) + idx;
    return gbOpaque(gbR->palette[gbR->lookup[i]], alpha);
}

/** Colour index (0..3) of a tile's pixel; `base` is the tile's offset in VRAM. */
static inline int gbTilePixel(uint32_t base, int x, int y) {
    uint8_t lo = gbB->video.vram[base + (uint32_t) y * 2], hi = gbB->video.vram[base + (uint32_t) y * 2 + 1];
    int bit = 7 - x;
    return ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
}

static inline uint32_t gbBgTileBase(int t, uint8_t attr) {
    uint32_t base = (gbLcdc & 0x10) ? (uint32_t) t * 16 : (uint32_t) (0x1000 + (int8_t) t * 16);
    return base + ((gbCgb && (attr & 8)) ? 0x2000u : 0u);
}

// --- palette writes ------------------------------------------------------------------------------------
//
// The strips are drawn in the palettes as the frame ends. A game that writes one while the screen is being drawn (a door's
// fade, the colours of the next map as a connection is crossed) shows the lines above the write in the old colours - for
// a frame the strips can't match them, and the headless check doesn't count such a frame (pk_wide_gb_last_torn).

static void (*gbOrigWritePalette)(struct GBVideoRenderer*, int, uint16_t);
static struct GBVideoRenderer* gbHooked;
static struct GB* gbHookGb;
static bool gbPalTorn, gbLastPalTorn;

static void gbHookWritePalette(struct GBVideoRenderer* r, int index, uint16_t value) {
    if (gbHookGb && gbHookGb->video.ly < GBH && (gbHookGb->memory.io[GB_REG_LCDC] & 0x80)) gbPalTorn = true;
    gbOrigWritePalette(r, index, value);
}

static void gbHookPalettes(struct GB* gb) {
    struct GBVideoRenderer* r = gb->video.renderer;
    gbHookGb = gb;
    if (!r || r == gbHooked || r->writePalette == gbHookWritePalette) return;
    gbOrigWritePalette = r->writePalette;
    r->writePalette = gbHookWritePalette;
    gbHooked = r;
}

int pk_wide_gb_last_torn(void) { return gbLastPalTorn; }

// --- objects -------------------------------------------------------------------------------------------

struct GbObj { int x, y, tile, attr, under; };   // screen pixels; under: the lower half hides in grass

#define GB_MAX_OBJS 104
static struct GbObj gbObjs[GB_MAX_OBJS];
static int gbNObjs;

/** OAM as drawn, past the screen's edges too: an NPC walking off the right edge keeps its objects at x 168-176 for the
 * step (the hardware shows none of them; the strip shows all of them), one walking off the left or top edge wraps to
 * the byte's top. Gen 1 marks an unused entry by y $A0 (PrepareOAMData's clear), so that y is skipped - and an
 * all-zero entry (nothing written yet). */
static void gbObjsFromOam(void) {
    gbNObjs = 0;
    if (!(gbLcdc & 0x02)) return;
    bool tall = gbLcdc & 0x04;
    for (int i = 0; i < 40 && gbNObjs < GB_MAX_OBJS; i++) {
        const uint8_t* o = &gbB->video.oam.raw[i * 4];
        if (o[0] == 0xA0 || (o[0] == 0 && o[1] == 0)) continue;
        int x = o[1] - 8, y = o[0] - 16;
        if (x >= 200) x -= 256;   // wrapped: left of the screen
        if (y >= 200) y -= 256;   // above it
        int tile = tall ? o[2] & ~1 : o[2];
        gbObjs[gbNObjs++] = (struct GbObj) { x, y, tile, o[3], -1 };
        if (tall) gbObjs[gbNObjs++] = (struct GbObj) { x, y + 8, tile | 1, o[3] | 0x100, -1 };
    }
}

/** Is sprite slot n a toggleable object switched off (IsObjectHidden)? */
static bool gbToggledOff(const struct GbSnap* s, int n) {
    for (int i = 0; i + 1 < (int) sizeof(s->toggleList) && s->toggleList[i] != 0xFF; i += 2) {
        if (s->toggleList[i] != n) continue;
        int g = s->toggleList[i + 1];
        return (s->toggleFlags[(g >> 3) & 31] >> (g & 7)) & 1;
    }
    return false;
}

/** Every NPC OAM doesn't show, standing where it stands ((ox, oy) is the screen's world pixel): the ones the game hides
 * for being off screen, and one stepping on or off it while OAM, a frame behind RAM, doesn't have it yet. Going by the
 * image index alone ($FF = hidden) made such an NPC vanish for two frames at every edge it crossed: a flicker. */
static void gbObjsOffScreen(const struct GbSnap* s, int ox, int oy) {
    // SpriteFacingAndAnimationTable's standing frames: y, x, tile, x-flip.
    static const int8_t FACE[4][4][4] = {
        { { 0, 0, 0, 0 }, { 0, 8, 1, 0 }, { 8, 0, 2, 0 }, { 8, 8, 3, 0 } },       // down
        { { 0, 0, 4, 0 }, { 0, 8, 5, 0 }, { 8, 0, 6, 0 }, { 8, 8, 7, 0 } },       // up
        { { 0, 0, 8, 0 }, { 0, 8, 9, 0 }, { 8, 0, 10, 0 }, { 8, 8, 11, 0 } },     // left
        { { 0, 8, 8, 1 }, { 0, 0, 9, 1 }, { 8, 8, 10, 1 }, { 8, 0, 11, 1 } },     // right
    };
    // A battle starting (its flashes and wipe hide the field's sprites) or the sprite layer off: none of them.
    if (s->battle || !(gbLcdc & 0x02)) return;
    const int fromOam = gbNObjs;
    for (int n = 1; n < 15; n++) {   // not the player (0) nor Pikachu (15)
        const uint8_t* a = &s->spr1[n * 16];
        const uint8_t* b = &s->spr2[n * 16];
        if (!a[0] || !b[0xE]) continue;
        int mx = b[5] - 4, my = b[4] - 4;
        bool onScreen = my >= s->py - 4 && my <= s->py + 4 && mx >= s->px - 4 && mx <= s->px + 5;
        // On screen and hidden under a text box: the game's call. (Hidden on screen otherwise is RAM a step ahead of
        // the picture: the player has moved, the NPC's image index catches up a frame later.)
        if (onScreen && a[2] == 0xFF && s->text) continue;
        if (gbToggledOff(s, n)) continue;
        int sx0 = 32 + 16 * b[5] - ox, sy0 = 32 + 16 * b[4] - 4 - oy;
        // OAM already has it - a whole 16 x 16 (its four objects) less than a step from where it stands, walking: that
        // one is drawn. (Any object that near would also have matched the next NPC along, 16 px over.)
        bool shown = false;
        for (int k = 0; k < fromOam && !shown; k++) {
            int x = gbObjs[k].x, y = gbObjs[k].y;
            if (x <= sx0 - 16 || x >= sx0 + 16 || y <= sy0 - 16 || y >= sy0 + 16) continue;
            int quads = 0;
            for (int q = 0; q < fromOam; q++)
                quads |= (gbObjs[q].x == x ? 1 : gbObjs[q].x == x + 8 ? 2 : 0) << (gbObjs[q].y == y ? 0 : gbObjs[q].y == y + 8 ? 2 : 4);
            shown = (quads & 0xF) == 0xF;
        }
        if (shown) continue;
        int slot = b[0xE] - 1;
        int face = (a[9] >> 2) & 3, base = slot * 12;
        if (slot >= 10) { face = 0; base = slot == 11 ? 0x7C : 120; }   // one-frame sprites (item balls, boulders)
        int sx = sx0, sy = sy0;
        for (int k = 0; k < 4 && gbNObjs < GB_MAX_OBJS; k++) {
            const int8_t* f = FACE[face][k];
            int attr = (f[3] ? 0x20 : 0) | ((k >= 2 && (b[7] & 0x80)) ? 0x80 : 0);
            gbObjs[gbNObjs++] = (struct GbObj) { sx + f[1], sy + f[0], base + f[2], attr, -1 };
        }
    }
}

// --- drawing -------------------------------------------------------------------------------------------

#define GB_BUF (PK_WIDE_MAX_VIEW_W * PK_WIDE_MAX_VIEW_H)
static uint32_t gbRaw[GB_BUF];       // the last scene that checked out, for the window gbRawView
static uint8_t gbBgIdx[GB_BUF];      // its BG colour index per pixel (| 0x80: the BG tile has priority), for objects
static uint8_t gbObjMask[GB_BUF];    // an object already took this pixel
static struct PkWideView gbRawView;

/** The world from screen pixel (x0, y0) for w x h into dst (stride `stride`), then the objects over it. With
 * `screen` (the middle, for the headless check), the screen's own font tiles go over the world, as the PPU draws them:
 * a text box, the START menu. */
static void gbDraw(const struct GbSnap* s, int ox, int oy, int x0, int y0, int w, int h, uint32_t* dst, int stride,
                   uint32_t alpha, bool screen) {
    for (int j = 0; j < h; j++) {
        int wy = oy + y0 + j, ty = wy >> 3, fy = wy & 7;
        int lastTx = 0x7FFFFFFF, t = 0;
        uint8_t attr = 0;
        uint32_t base = 0;
        for (int i = 0; i < w; i++) {
            int wx = ox + x0 + i, tx = wx >> 3;
            if (tx != lastTx) {
                lastTx = tx;
                t = gbTile(s, tx, ty);
                attr = gbAttrSeen[t] ? gbTileAttr[t] : gbAttrDefault;
                base = gbBgTileBase(t, attr);
            }
            int px = (attr & 0x20) ? 7 - (wx & 7) : wx & 7, py = (attr & 0x40) ? 7 - fy : fy;
            int idx;
            uint8_t a = attr;
            int sx = x0 + i, sy = y0 + j;
            int st = -1;
            if (screen && sx >= 0 && sx < GBW && sy >= 0 && sy < GBH) {
                int at = gbMapBase + (((gbSrcY + sy) >> 3) & 31) * 32 + (((gbSrcX + sx) >> 3) & 31);
                st = gbB->video.vram[at];
                if (st >= GB_MAP_TILES) {
                    a = gbCgb ? gbB->video.vram[0x2000 + at] : 0;
                    int fx = (gbSrcX + sx) & 7, fyy = (gbSrcY + sy) & 7;
                    idx = gbTilePixel(gbBgTileBase(st, a), (a & 0x20) ? 7 - fx : fx, (a & 0x40) ? 7 - fyy : fyy);
                } else {
                    st = -1;
                }
            }
            if (st < 0) idx = gbTilePixel(base, px, py);
            dst[j * stride + i] = gbBgColor(a, idx, alpha);
            gbBgIdx[j * w + i] = (uint8_t) (idx | (a & 0x80));
            gbObjMask[j * w + i] = 0;
        }
    }
    // Objects, the first (lowest OAM index) on top as on a Color; one behind the BG (its priority bit, or the BG
    // tile's) shows only over BG colour 0. LCDC bit 0 off on a Color lets every object over the BG.
    for (int k = 0; k < gbNObjs; k++) {
        const struct GbObj* o = &gbObjs[k];
        if (o->x >= x0 + w || o->x + 8 <= x0 || o->y >= y0 + h || o->y + 8 <= y0) continue;
        uint32_t tb = (uint32_t) (o->tile & 0xFF) * 16 + ((gbCgb && (o->attr & 8)) ? 0x2000u : 0u);
        for (int yy = 0; yy < 8; yy++) {
            int j = o->y + yy - y0;
            if (j < 0 || j >= h) continue;
            // An 8x16 object's halves swap with a vertical flip.
            int row = (o->attr & 0x40) ? 7 - yy : yy;
            uint32_t tbase = tb;
            if ((gbLcdc & 0x04) && (o->attr & 0x40)) tbase ^= 16;
            for (int xx = 0; xx < 8; xx++) {
                int i = o->x + xx - x0;
                if (i < 0 || i >= w || gbObjMask[j * w + i]) continue;
                int idx = gbTilePixel(tbase, (o->attr & 0x20) ? 7 - xx : xx, row);
                if (!idx) continue;
                gbObjMask[j * w + i] = 1;
                uint8_t bg = gbBgIdx[j * w + i];
                bool behind = (o->attr & 0x80) || (gbCgb && (bg & 0x80));
                if (behind && (bg & 3) && (!gbCgb || (gbLcdc & 0x01))) continue;
                dst[j * stride + i] = gbObjColor((uint8_t) o->attr, idx, alpha);
            }
        }
    }
}

// --- off the field: a bar in the screen's own background ------------------------------------------------------
//
// A menu screen, a battle, the title: nothing to rebuild past the edges, and Gen 1 draws these on a plain background, so
// the strips are that background, flat - the colour most of the frame has (white on Yellow's menus, battles and trainer
// card, black in a fade to black). The GBA's glow spread from the edges looked smeared against them. OPTION, opened from
// the START menu, keeps the field's strips (the user's call, like SAVE's prompt).

static uint32_t gbFill = 0xFFFFFFu;

/** The colour most of the frame has (sampled every 4 px): the screen's background - white on Yellow's menus, battles and
 * trainer card (whose red border took the edges), black in a fade to black. */
static uint32_t gbBackground(const uint32_t* frame) {
    uint32_t col[16];
    int n[16], kinds = 0;
    for (int y = 2; y < GBH; y += 4)
        for (int x = 2; x < GBW; x += 4) {
            uint32_t c = frame[y * GBW + x] & 0x00FFFFFFu;
            int i = 0;
            while (i < kinds && col[i] != c) i++;
            if (i == kinds) {
                if (kinds == 16) continue;
                col[kinds] = c;
                n[kinds++] = 0;
            }
            n[i]++;
        }
    int best = 0;
    for (int i = 1; i < kinds; i++) if (n[i] > n[best]) best = i;
    return kinds ? col[best] : 0xFFFFFFu;
}

static inline uint32_t gbLerp(uint32_t a, uint32_t b, int t) {
    if (t <= 0) return a;
    if (t >= 256) return b;
    uint32_t rb = (((a & 0xFF00FF) * (uint32_t) (256 - t) + (b & 0xFF00FF) * (uint32_t) t) >> 8) & 0xFF00FF;
    uint32_t g = (((a & 0x00FF00) * (uint32_t) (256 - t) + (b & 0x00FF00) * (uint32_t) t) >> 8) & 0x00FF00;
    return (a & 0xFF000000) | rb | g;
}

static bool gbFaded(const uint32_t* frame) {
    int lo = 255, hi = 0;
    for (int y = 2; y < GBH; y += 6)
        for (int x = 3; x < GBW; x += 6) {
            uint32_t p = frame[y * GBW + x];
            int r = p & 0xFF, g = (p >> 8) & 0xFF, b = (p >> 16) & 0xFF;
            int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
            if (mx > hi) hi = mx;
            if (mn < lo) lo = mn;
            if (hi > GB_FADE_LEVEL && lo < 255 - GB_FADE_LEVEL) return false;
        }
    return hi <= GB_FADE_LEVEL || lo >= 255 - GB_FADE_LEVEL;
}

// --- the frame -----------------------------------------------------------------------------------------

static int gbSinceField = 1000;   // frames since the field (or a whole black / white frame) last showed
static int gbMix, gbLastScore = -1, gbLastMode = PK_WIDE_MODE_NONE, gbLastClose = 1, gbLastScene = PK_WIDE_SCENE_OFF;

int pk_wide_gb_knows(uint32_t crc) {
    for (size_t i = 0; i < sizeof(GB_ROMS) / sizeof(GB_ROMS[0]); i++)
        if (GB_ROMS[i].crc == crc) return 1;
    return 0;
}

int pk_wide_gb_configure(uint32_t crc) {
    gbCfg = NULL;
    for (size_t i = 0; i < sizeof(GB_ROMS) / sizeof(GB_ROMS[0]); i++)
        if (crc && GB_ROMS[i].crc == crc) gbCfg = GB_ROMS[i].a;
    gbAttrBlockset = 0;
    pk_wide_gb_reset();
    return gbCfg != NULL;
}

void pk_wide_gb_reset(void) {
    gbPrev = -1;
    gbSnaps[0].ok = gbSnaps[1].ok = gbGood.ok = false;
    gbMix = 0;
    gbRawView.w = 0;
    gbLastScore = -1;
    gbLastMode = PK_WIDE_MODE_NONE;
    gbLastClose = 1;
    gbLastScene = PK_WIDE_SCENE_OFF;
    gbSinceField = 1000;
    gbFromStart = false;
}

int pk_wide_gb_scene(void) { return gbLastScene; }
int pk_wide_gb_wants_close(void) { return gbLastClose; }
int pk_wide_gb_last_mode(void) { return gbLastMode; }
int pk_wide_gb_last_score(void) { return gbLastScore; }

static inline bool gbOnFrame(int x, int y) { return x >= 0 && x < GBW && y >= 0 && y < GBH; }

int pk_wide_gb_view(struct mCore* core, const uint32_t* frame, uint32_t* out, struct PkWideView v, uint32_t* centre) {
    if (!core || core->platform(core) != mPLATFORM_GB || !gbCfg) {
        if (out) {
            uint32_t alpha = frame[0] & 0xFF000000u;
            for (int j = 0; j < v.h; j++)
                for (int i = 0; i < v.w; i++) {
                    int X = v.x + i, Y = v.y + j;
                    out[j * v.w + i] = gbOnFrame(X, Y) ? frame[Y * GBW + X] : alpha;
                }
        }
        gbLastClose = 1;
        return 0;
    }
    gbB = (struct GB*) core->board;
    gbR = (struct GBVideoSoftwareRenderer*) gbB->video.renderer;
    gbHookPalettes(gbB);
    gbLastPalTorn = gbPalTorn;   // written while the frame just finished was drawn
    gbPalTorn = false;
    int cur = gbPrev == 0 ? 1 : 0;
    gbTake(&gbSnaps[cur]);
    if (!out) {   // pk_wide_skip: only the RAM after this frame, for the next one drawn
        gbPrev = cur;
        return gbMix;
    }
    if (v.w > PK_WIDE_MAX_VIEW_W) v.w = PK_WIDE_MAX_VIEW_W;
    if (v.h > PK_WIDE_MAX_VIEW_H) v.h = PK_WIDE_MAX_VIEW_H;
    if (v.w < 1 || v.h < 1) return 0;
    uint32_t alpha = frame[0] & 0xFF000000u;

    gbLcdc = gbB->memory.io[GB_REG_LCDC];
    gbScx = gbB->memory.io[GB_REG_SCX];
    gbScy = gbB->memory.io[GB_REG_SCY];
    gbCgb = gbR->model >= GB_MODEL_CGB;
    int wy = gbB->memory.io[GB_REG_WY], wx = gbB->memory.io[GB_REG_WX];
    bool window = (gbLcdc & 0x20) && wy < GBH && wx < GBW + 7;
    bool fullWindow = window && wy == 0 && wx == 7;
    gbMapBase = fullWindow ? ((gbLcdc & 0x40) ? 0x1C00 : 0x1800) : (gbLcdc & 0x08) ? 0x1C00 : 0x1800;
    gbSrcX = fullWindow ? 0 : gbScx;
    gbSrcY = fullWindow ? 0 : gbScy;

    // Where the screen is: the snapshot the frame was drawn from (the one before, else this one) and the world tile
    // around RAM's guess whose tiles match the screen's BG map.
    int best = -1, bestTx = 0, bestTy = 0, bestSnap = -1;
    gbScreenMap = 0;
    if ((gbLcdc & 0x80) && (!window || fullWindow)) {
        gbReadScreen();
        const int order[2] = { gbPrev, cur };
        for (int k = 0; k < 2 && best < 1000; k++) {
            int si = order[k];
            if (si < 0 || !gbSnaps[si].ok) continue;
            const struct GbSnap* s = &gbSnaps[si];
            // Nearest first: on a tie (a field of grass), the guess wins.
            for (int r = 0; r <= GB_SEARCH && best < 1000; r++)
                for (int dy = -r; dy <= r; dy++)
                    for (int dx = -r; dx <= r; dx++) {
                        if ((dx < 0 ? -dx : dx) != r && (dy < 0 ? -dy : dy) != r) continue;
                        int sc = gbScore(s, s->guessX + dx, s->guessY + dy, false);
                        if (sc > best) { best = sc; bestTx = s->guessX + dx; bestTy = s->guessY + dy; bestSnap = si; }
                    }
        }
        // Not near the guess: the last snapshot that matched, around where it did.
        if (best >= 0 && best < GB_MIN_SCORE && gbGood.ok) {
            for (int dy = -GB_SEARCH; dy <= GB_SEARCH; dy++)
                for (int dx = -GB_SEARCH; dx <= GB_SEARCH; dx++) {
                    int sc = gbScore(&gbGood, gbGoodTx + dx, gbGoodTy + dy, true);
                    if (sc > best) { best = sc; bestTx = gbGoodTx + dx; bestTy = gbGoodTy + dy; bestSnap = 2; }
                }
        }
        // Still nothing, but the screen is the field: RAM is somewhere else for a moment - look everywhere in the loaded
        // world, giving up on each spot as soon as it can't match (the best spot wins, not the first).
        bool wide = best >= 0 && best < GB_MIN_SCORE;
        for (int k = 0; k < 2 && wide && best < 1000; k++) {
            int si = order[k];
            if (si < 0 || !gbSnaps[si].ok) continue;
            const struct GbSnap* s = &gbSnaps[si];
            for (int ty = -18; ty <= (s->h + 6) * 4 && best < 1000; ty++)
                for (int tx = -20; tx <= (s->w + 6) * 4 && best < 1000; tx++) {
                    int sc = gbScore(s, tx, ty, true);
                    if (sc > best) { best = sc; bestTx = tx; bestTy = ty; bestSnap = si; }
                }
        }
        // A box over a blanked field - SAVE's prompt clears the screen behind its boxes but for a corner of the map: too
        // few map tiles to place the screen by, but every one of them is where the field last was, so it's still the
        // field (the strips keep the map). A menu screen has none of those tiles, or not there.
        if (best < 0 && gbScreenMap >= GB_OVERLAY_TILES && gbGood.ok && !gbSnaps[cur].battle && gbExact(&gbGood, gbGoodTx, gbGoodTy)) {
            best = 1000;
            bestTx = gbGoodTx;
            bestTy = gbGoodTy;
            bestSnap = 2;
        }
        // OPTION from the START menu: a screen of its own, but the field's strips stay (the user's call) - the field where
        // it last was. The START menu has to have been what was up (its box at the top right), so a later screen with
        // the same row remembered (a naming screen after a gift) doesn't pass for it.
        if (best < GB_MIN_SCORE && gbFromStart && gbGood.ok && !gbSnaps[cur].battle && gbSnaps[cur].ok &&
            gbSnaps[cur].startItem == GB_START_OPTION) {
            best = 1000;
            bestTx = gbGoodTx;
            bestTy = gbGoodTy;
            bestSnap = 2;
        }
    }
    gbLastScore = best;
    bool valid = best >= GB_MIN_SCORE;
    const struct GbSnap* now = &gbSnaps[cur];
    bool battle = now->ok && now->battle;
    bool faded = !valid && gbFaded(frame);
    // A door: between the fade and the new map, VRAM is rewritten a third a frame (the BG transfer) and matches nothing
    // for a few frames - still the field's business (ZOOM holds), not a menu screen to pull in for.
    bool fade = faded || (!valid && best >= 0 && !battle && gbSinceField <= GB_DOOR_FRAMES);
    gbSinceField = valid || faded ? 0 : gbSinceField + 1;
    gbLastMode = valid ? PK_WIDE_MODE_MAP : PK_WIDE_MODE_NONE;
    if (battle) gbFromStart = false;
    else if (valid && bestSnap != 2 && gbScreenMap > 0) {
        const struct GbSnap* m = &gbSnaps[bestSnap];
        bool startBox = gbScreen[0][10] == GB_START_TILE_L && gbScreen[0][19] == GB_START_TILE_R;
        gbFromStart = m->text && startBox ? true : m->text ? gbFromStart : false;
    }
    // A wild battle's flashes and wipe run on the field with wIsInBattle already set: the pull-in starts with them.
    const struct GbSnap* match = !valid ? NULL : bestSnap == 2 ? &gbGood : &gbSnaps[bestSnap];
    gbLastScene = fade ? PK_WIDE_SCENE_FADE
                : battle ? (valid ? PK_WIDE_SCENE_BATTLE_START : PK_WIDE_SCENE_OFF)
                : !valid ? PK_WIDE_SCENE_MENU
                : match->text ? PK_WIDE_SCENE_TEXT : PK_WIDE_SCENE_FIELD;
    gbLastClose = gbLastScene != PK_WIDE_SCENE_FIELD;

    if (valid) {
        const struct GbSnap* s = match;
        if (s != &gbGood) {
            gbGood = *s;
            gbGoodTx = bestTx;
            gbGoodTy = bestTy;
        }
        int ox = bestTx * 8 + (gbSrcX & 7), oy = bestTy * 8 + (gbSrcY & 7);
        gbLearnAttrs(s, bestTx, bestTy);
        gbObjsFromOam();
        gbObjsOffScreen(s, ox, oy);
        gbDraw(s, ox, oy, v.x, v.y, v.w, v.h, gbRaw, v.w, alpha, false);
        gbRawView = v;
        if (centre) {
            int n = gbNObjs;
            gbObjsFromOam();   // the middle as the hardware draws it: OAM's objects alone
            gbDraw(s, ox, oy, 0, 0, GBW, GBH, centre, GBW, alpha, true);
            gbNObjs = n;
        }
        gbMix = gbMix + GB_MIX_STEP > 256 ? 256 : gbMix + GB_MIX_STEP;
    } else {
        gbMix = gbMix - GB_MIX_STEP < 0 ? 0 : gbMix - GB_MIX_STEP;   // the last scene, frozen, dissolves into the glow
    }
    if (!gbRawView.w) gbMix = 0;
    if (!valid) gbFill = gbBackground(frame);   // fading in again from a menu: from its bar
    uint32_t fill = gbFill | alpha;

    for (int j = 0; j < v.h; j++) {
        int Y = v.y + j;
        uint32_t* row = out + j * v.w;
        int ry = Y - gbRawView.y;
        bool rawRow = ry >= 0 && ry < gbRawView.h;
        for (int i = 0; i < v.w; i++) {
            int X = v.x + i;
            if (gbOnFrame(X, Y)) {
                int n = (GBW - X) < (v.w - i) ? GBW - X : v.w - i;
                memcpy(row + i, frame + Y * GBW + X, (size_t) n * sizeof(uint32_t));
                i += n - 1;
                continue;
            }
            int rx = X - gbRawView.x, m = gbMix;
            uint32_t scene = 0;
            if (rawRow && rx >= 0 && rx < gbRawView.w) scene = gbRaw[ry * gbRawView.w + rx];
            else m = 0;
            row[i] = m >= 256 ? scene : gbLerp(fill, scene, m);
        }
    }
    gbPrev = cur;
    return gbMix;
}
