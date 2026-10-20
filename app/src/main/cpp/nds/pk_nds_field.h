// The DS games' field read (pk_wide.c's pk_field_state for a Gen 4 game): what the overworld is doing this frame, from
// the game's FieldSystem (pret/pokeplatinum's field_system.h). Portable - the app (pk_nds.cpp) and the headless tool
// (native-capture/nds_dump's fieldinfo) read memory through the callback.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Where one DS game keeps its field state. */
typedef struct PkNdsFieldGame {
    const char* name;
    uint32_t fieldSystemPtr;   // sFieldSystem: the FieldSystem, NULL outside the overworld's process
    uint32_t battleTemplate;   // gBattleApplicationTemplate: the battle's child process copies its init function
    int offProcessManager;     // FieldSystem.processManager: FieldProcessManager { parent, child, pause, kill }
    int offTask;               // FieldSystem.task: a field task (a script, a warp, a menu's own task), NULL when free
    int offPlayerAvatar;       // FieldSystem.playerAvatar
    int offRunningFieldMap;    // FieldSystem.runningFieldMap: the field map is up and drawing
    int offAvatarState;        // PlayerAvatar.playerAvatarState: PLAYER_AVATAR_WALKING / _CYCLING / _SURFING / ...
    int offMapHeaderData;      // FieldSystem.mapHeaderData: the map's events and its WildEncounters, loaded on entering it
    int offGrassRate;          // MapHeaderData.wildEncounters.grassEncounters.encounterRate (0 = no random encounter)
    int offSurfRate;           // ... .surfEncounters.encounterRate
    int offLocation;           // FieldSystem.location: Location { mapHeaderID, ... }
    uint32_t fieldMapMain;     // gFieldMapTemplate.main (Thumb): the field map's per-frame function, where pk_nds_call hooks in
    uint32_t currentBgm;       // sSoundSystem.currentBGM (u16): the song playing (STEADY FF music starts again on a new one)
    uint32_t seqPlayers;       // NitroSystem's sSeqPlayer[16] (NNSSndSeqPlayer, 0x44 bytes): a volume fade running (STEADY waits it out)
    int rateBytes;             // the encounter rates are u8s (HeartGold / SoulSilver's EncounterData), not Platinum's u32s
    int callSet;               // pk_nds_call's functions for this game: 0 none, 1 Platinum's, 2 HeartGold / SoulSilver's
    int offBattleContext;      // BattleSystem.ctx (the battle process's data is the BattleSystem), 0 = unknown
    int offCommand;            // BattleContext.command: the battle controller's step (constants/battle.h's CONTROLLER_COMMAND_*)
} PkNdsFieldGame;

// pk_wide.h's PK_FIELD_* bits, which the engine already reads, plus two the DS sets:
#define PK_NDS_FIELD_KNOWN 1
#define PK_NDS_FIELD_LOCKED 2
#define PK_NDS_FIELD_BATTLE 4
#define PK_NDS_FIELD_TEXT 8
#define PK_NDS_FIELD_BIKE 16
#define PK_NDS_FIELD_WILD_HELD 32
#define PK_NDS_FIELD_SURF 64
#define PK_NDS_FIELD_AVATAR 128
/** The overworld itself is on screen (the field map process runs, no child process over it). */
#define PK_NDS_FIELD_SCREEN 256
/** A menu screen (the party, the bag, the Pokédex, ...) runs over the field as its child process. */
#define PK_NDS_FIELD_MENU 512
/** A battle waits for the player's command (pk_nds_battle_command is PK_NDS_BATTLE_SELECTING): SMART FF's 1x. */
#define PK_NDS_FIELD_BATTLE_INPUT 1024

#ifdef __cplusplus
}
namespace melonDS { class NDS; }
/** How many of the top screen's BG3 map entries show a tile (tile number not 0) in its 24 visible rows: Platinum's
 * field text box, sign and yes / no windows (field_message.c: BG_LAYER_MAIN_3). PK_NDS_FIELD_TEXT needs some: a field
 * task alone (a warp, a door) isn't a text box. */
int pk_nds_bg3_tiles(melonDS::NDS* nds);
extern "C" {
#endif

/** The table entry for this cart (its header's game code and revision), or NULL. */
const PkNdsFieldGame* pk_nds_field_game(const char code[4], int revision);

/** The battle controller's step (BattleContext.command: CONTROLLER_COMMAND_*) while a battle runs, else -1. */
int pk_nds_battle_command(const PkNdsFieldGame* game, uint32_t (*read32)(uint32_t addr));

/** CONTROLLER_COMMAND_SELECTION_SCREEN_INPUT: the battle waits for the player's command (FIGHT / BAG / POKéMON / RUN, a
 * move, a target - and the bag or party opened from there). */
#define PK_NDS_BATTLE_SELECTING 5

/** The cart's own grass / surf rates by map header id: [map, grass, surf] triples (n triples; the app's generated tables,
 * Gen4World.wildRates). WILD ENCOUNTERS back on puts a map's rates back from here when they're still 0 - a hold this
 * process no longer remembers, from a resume state taken while it was off. Emu thread. */
void pk_nds_field_set_rates(const int32_t* triples, int n);

/** This frame's bits for [game], reading main RAM through [read32] (0 = unreadable). With [wildOff], WILD ENCOUNTERS
 * OFF: while the field is free, the current map's grass and surf encounter rates are held at 0 through [write32] (the
 * game never rolls a random encounter; fishing and scripted battles are untouched) - PK_NDS_FIELD_WILD_HELD while
 * they are; off again, the rates go back (a new map loads its own anyway). */
int pk_nds_field_state(const PkNdsFieldGame* game, uint32_t (*read32)(uint32_t addr),
                       void (*write32)(uint32_t addr, uint32_t value), int wildOff);

#ifdef __cplusplus
}
#endif
