// See pk_nds_field.h. Addresses from pret/pokeplatinum's .xMAP (it builds the retail ROM byte for byte), struct
// offsets from its headers, both checked on headless captures (nds_dump's fieldinfo: the field, a text box, the X
// menu, the party screen, a battle).

#include "pk_nds_field.h"

#include <string.h>

#include <NDS.h>

static const PkNdsFieldGame PLATINUM_US_REV1 = {
    "Platinum (USA, rev 1)",
    0x021C07DC,   // sFieldSystem
    0x020EA358,   // gBattleApplicationTemplate
    0x00, 0x10, 0x3C, 0x68,
    0x1C,
    // MapHeaderData: 4 counts + 4 pointers, tmpEventsBuf[512], initScripts[64], then WildEncounters: grass rate first,
    // surf's after 12 grass slots and the swarm / time / radar / form / dual-slot tables (checked on Route 201: 30, 0)
    0x14, 0x920, 0x920 + 0xCC,
    0x1C,
    0x021D0F69,   // FieldMap_Main (overlay 5)
    // sSoundSystem (0x02101DF8) + 0xBCD0C: found by Sound_Impl_GetCurrentBGM's answer (Twinleaf 0x409, Route 201 0x41A),
    // the only u16 there holding both
    0x021BEB04,
    0x021CAF64,   // sSeqPlayer (libnnssnd's player.c)
    0,
    1,
};

// HeartGold / SoulSilver (USA): pret/pokeheartgold's .xMAP (the same in both games' builds, compared) and field_system.h, checked on
// the player's saves headless. FieldSystem: processManager 0, taskman 0x10, mapEvents 0x14 (MapEvents' wildEncounters at
// 0x920: u8 walking rate, then u8 surfing), location 0x20, playerAvatar 0x40 (PlayerAvatar.state at 0x18), runningFieldMap
// 0x6C. The field map's main (pk_nds_call's hook) is ov01_02206378's: the same word in both games.
static const PkNdsFieldGame HEARTGOLD_US = {
    "HeartGold / SoulSilver (USA)",
    0x021D4158,   // sFieldSysPtr
    0x020FA484,   // gOverlayTemplate_Battle
    0x00, 0x10, 0x40, 0x6C,
    0x18,
    0x14, 0x920, 0x921,
    0x20,
    0x021E5BE5,   // ov01_02206378's main (the field map's OverlayManagerTemplate)
    0x021D0554,   // sSoundWork (0x02111958) + 0xBEBFC: currentSeqNo, GF_GetCurrentPlayingBGM's answer
    0x021DF984,   // sSeqPlayer
    1,
    2,
    0x30,         // BattleSystem.ctx
    0x08,         // BattleContext.command (5 at the command menus, 22 running a move's script: headless dumps)
};

// WILD ENCOUNTERS OFF's hold: the MapHeaderData whose rates it zeroed, for which map, and their values before.
static uint32_t heldData, heldMap, heldGrass, heldSurf;
static int holding;

// PLAYER_AVATAR_* (constants/player_avatar.h)
enum { AVATAR_WALKING = 0, AVATAR_CYCLING = 1, AVATAR_SURFING = 2 };

const PkNdsFieldGame* pk_nds_field_game(const char code[4], int revision)
{
    if (memcmp(code, "CPUE", 4) == 0 && revision == 1) return &PLATINUM_US_REV1;
    if ((memcmp(code, "IPKE", 4) == 0 || memcmp(code, "IPGE", 4) == 0) && revision == 0) return &HEARTGOLD_US;
    return 0;
}

static int inMainRam(uint32_t p) { return p >= 0x02000000u && p < 0x02400000u; }

// An encounter rate: a u32 (Platinum) or a u8 (HeartGold / SoulSilver), read and written through the aligned word.
static uint32_t rateRead(const PkNdsFieldGame* g, uint32_t (*read32)(uint32_t), uint32_t a)
{
    if (!g->rateBytes) return read32(a);
    return (read32(a & ~3u) >> ((a & 3) * 8)) & 0xFF;
}

static void rateWrite(const PkNdsFieldGame* g, uint32_t (*read32)(uint32_t), void (*write32)(uint32_t, uint32_t),
                      uint32_t a, uint32_t v)
{
    if (!g->rateBytes) { write32(a, v); return; }
    int shift = (a & 3) * 8;
    uint32_t w = read32(a & ~3u);
    write32(a & ~3u, (w & ~(0xFFu << shift)) | ((v & 0xFF) << shift));
}

static void releaseWild(const PkNdsFieldGame* g, uint32_t (*read32)(uint32_t), void (*write32)(uint32_t, uint32_t),
                        uint32_t data, uint32_t map)
{
    // Only the struct it changed, still holding the same map's data: a map change reloaded both rates already.
    if (holding && data == heldData && map == heldMap) {
        if (rateRead(g, read32, heldData + g->offGrassRate) == 0) rateWrite(g, read32, write32, heldData + g->offGrassRate, heldGrass);
        if (rateRead(g, read32, heldData + g->offSurfRate) == 0) rateWrite(g, read32, write32, heldData + g->offSurfRate, heldSurf);
    }
    holding = 0;
}

int pk_nds_battle_command(const PkNdsFieldGame* g, uint32_t (*read32)(uint32_t))
{
    if (!g || !g->offBattleContext) return -1;
    uint32_t fs = read32(g->fieldSystemPtr);
    if (!inMainRam(fs)) return -1;
    uint32_t pm = read32(fs + g->offProcessManager);
    if (!inMainRam(pm)) return -1;
    uint32_t child = read32(pm + 4);
    if (!inMainRam(child) || read32(child) != read32(g->battleTemplate)) return -1;
    // The battle's OverlayManager: its data (+0x1C) is the BattleSystem.
    uint32_t bs = read32(child + 0x1C);
    if (!inMainRam(bs)) return -1;
    uint32_t ctx = read32(bs + g->offBattleContext);
    if (!inMainRam(ctx)) return -1;
    uint32_t c = read32(ctx + g->offCommand);
    return c < 64 ? (int) c : -1;
}

int pk_nds_field_state(const PkNdsFieldGame* g, uint32_t (*read32)(uint32_t), void (*write32)(uint32_t, uint32_t),
                       int wildOff)
{
    if (!g) return 0;
    int bits = PK_NDS_FIELD_KNOWN;
    uint32_t fs = read32(g->fieldSystemPtr);
    if (!inMainRam(fs)) return bits;   // title screen, the intro: no overworld at all
    uint32_t pm = read32(fs + g->offProcessManager);
    if (!inMainRam(pm)) return bits;
    uint32_t parent = read32(pm), child = read32(pm + 4), pause = read32(pm + 8);
    uint32_t task = read32(fs + g->offTask);
    int running = read32(fs + g->offRunningFieldMap) != 0;
    if (inMainRam(child)) {
        // The battle's child process starts from gBattleApplicationTemplate (its template is copied in whole).
        bits |= read32(child) == read32(g->battleTemplate) ? PK_NDS_FIELD_BATTLE : PK_NDS_FIELD_MENU;
        return bits | PK_NDS_FIELD_LOCKED;
    }
    if (inMainRam(parent) && running) bits |= PK_NDS_FIELD_SCREEN;
    // The field's own input runs only with no task and no pause (field_system.c's HandleFieldInput): a script, a text
    // box, a warp, the X menu all hold a task.
    if (task || pause || !running) {
        bits |= PK_NDS_FIELD_LOCKED;
        if (task && running) bits |= PK_NDS_FIELD_TEXT;
    }
    // WILD ENCOUNTERS: the current map's rates, held at 0 while it's off.
    uint32_t data = read32(fs + g->offMapHeaderData);
    uint32_t loc = read32(fs + g->offLocation);
    uint32_t map = inMainRam(loc) ? read32(loc) : 0xFFFFFFFFu;
    if (write32 && inMainRam(data) && inMainRam(loc)) {
        if (holding && (data != heldData || map != heldMap)) holding = 0;   // a new map: its own rates were loaded
        if (wildOff) {
            uint32_t grass = rateRead(g, read32, data + g->offGrassRate), surf = rateRead(g, read32, data + g->offSurfRate);
            if (!holding) {
                heldData = data;
                heldMap = map;
                heldGrass = grass;
                heldSurf = surf;
                holding = 1;
            }
            if (grass) rateWrite(g, read32, write32, data + g->offGrassRate, 0);
            if (surf) rateWrite(g, read32, write32, data + g->offSurfRate, 0);
        } else if (holding) {
            releaseWild(g, read32, write32, data, map);
        }
        if (holding) bits |= PK_NDS_FIELD_WILD_HELD;
    }
    uint32_t avatar = read32(fs + g->offPlayerAvatar);
    if (inMainRam(avatar)) {
        uint32_t state = read32(avatar + g->offAvatarState);
        bits |= PK_NDS_FIELD_AVATAR;
        if (state == AVATAR_CYCLING) bits |= PK_NDS_FIELD_BIKE;
        if (state == AVATAR_SURFING) bits |= PK_NDS_FIELD_SURF;
    }
    return bits;
}

int pk_nds_bg3_tiles(melonDS::NDS* nds)
{
    uint32_t disp = nds->GPU.GPU2D_A.DispCnt;
    if ((disp & 7) > 1 || !(disp & (1 << 11))) return 0;   // BG3 off, or not a text BG in this mode
    uint16_t cnt = nds->GPU.GPU2D_A.BGCnt[3];
    uint32_t base = 0x06000000u + ((disp >> 27) & 7) * 0x10000 + ((cnt >> 8) & 0x1F) * 0x800;
    int n = 0;
    for (uint32_t i = 0; i < 32 * 24; i++)
        if (nds->ARM9Read16(base + i * 2) & 0x3FF) n++;
    return n;
}
