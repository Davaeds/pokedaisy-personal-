package com.pokedaisy.app.companion.data

// Phase 2 of gamedata/: the records Pokedex.kt, GuideRom.kt, TrainerCard.kt and NativeReader.kt wrote by
// hand before they moved to gamedata/gen3/records/ (GameDataRecordsGen.kt), word for word but renamed, for
// GameDataMigrationTest. Goes with that test.

private fun vanillaDexFlags(seen1: Long, seen2: Long) = DexFlags(DexFlagBlock.SAVE_BLOCK_2, 0x5C, 0x28, 52, listOf(seen1, seen2))

val LEGACY_FIRERED_DEX_FLAGS = vanillaDexFlags(0x5F8, 0x3A18)

val LEGACY_EMERALD_DEX_FLAGS = vanillaDexFlags(0x988, 0x3B24)

val LEGACY_POKEDEX_FIRERED_REV1 = PokedexTables(
    entries = 0x0844E8B0L,
    frontPics = 0x0823511CL,
    palettes = 0x0823737CL,
    speciesInfo = 0x082547F4L,
    speciesToNational = 0x0825205EL,
    abilityNames = 0x0824FCB0L,
    footprints = 0x0843FB20L,
    evolutions = 0x082597C4L,
    tmhmLearnsets = 0x08252C38L,
    tmhmMoves = 0x0845A604L,
)

val LEGACY_POKEDEX_LEAFGREEN_REV0 = PokedexTables(
    entries = 0x0844E270L,
    frontPics = 0x08235088L,
    palettes = 0x082372E8L,
    speciesInfo = 0x08254760L,
    speciesToNational = 0x08251FCAL,
    abilityNames = 0x0824FC1CL,
    footprints = 0x0843F8ECL,
    evolutions = 0x08259734L,
    tmhmLearnsets = 0x08252BA4L,
    tmhmMoves = 0x08459FC4L,
)

val LEGACY_POKEDEX_LEAFGREEN_REV1 = PokedexTables(
    entries = 0x0844E2E0L,
    frontPics = 0x082350F8L,
    palettes = 0x08237358L,
    speciesInfo = 0x082547D0L,
    speciesToNational = 0x0825203AL,
    abilityNames = 0x0824FC8CL,
    footprints = 0x0843F95CL,
    evolutions = 0x082597A4L,
    tmhmLearnsets = 0x08252C14L,
    tmhmMoves = 0x0845A034L,
)

val LEGACY_POKEDEX_EMERALD = PokedexTables(
    entries = 0x0856B5B0L,
    frontPics = 0x0830A18CL,
    palettes = 0x08303678L,
    speciesInfo = 0x083203CCL,
    speciesToNational = 0x0831DC82L,
    abilityNames = 0x0831B6DBL,
    footprints = 0x0856E694L,
    entryStride = 0x20,
    regionalCount = 202, // HOENN_DEX_COUNT (Jirachi and Deoxys included)
    regionName = "HOENN",
    regionalOrder = 0x0831DFB8L,
    flags = LEGACY_EMERALD_DEX_FLAGS,
    nationalMagicOff = 2,
    nationalMagic = 0xDA,
    evolutions = 0x0832531CL,
    tmhmLearnsets = 0x0831E898L,
    tmhmMoves = 0x08615B94L,
)

val LEGACY_GUIDE_TABLES_FIRERED_REV1 = GuideTables(
    trainers = 0x0823EB38L,
    learnsets = 0x0825D824L,
    wildHeaders = 0x083C9D28L,
    mapGroups = 0x08352718L,
)

val LEGACY_GUIDE_TABLES_LEAFGREEN_REV0 = GuideTables(
    trainers = 0x0823EAA4L,
    learnsets = 0x0825D794L,
    wildHeaders = 0x083C9AF4L,
    guide = GuideId.LEAFGREEN,
    mapGroups = 0x08352688L,
)

val LEGACY_GUIDE_TABLES_LEAFGREEN_REV1 = GuideTables(
    trainers = 0x0823EB14L,
    learnsets = 0x0825D804L,
    wildHeaders = 0x083C9B64L,
    guide = GuideId.LEAFGREEN,
    mapGroups = 0x083526F8L,
)

val LEGACY_GUIDE_TABLES_EMERALD = GuideTables(
    trainers = 0x08310030L,
    learnsets = 0x0832937CL,
    wildHeaders = 0x08552D48L,
    sb1FlagsOff = 0x1270,
    sb1VarsOff = 0x139C,
    flagBytes = 0x12C,
    probeTrainer = 265,
    probeName = "ROXANNE",
    guide = GuideId.EMERALD,
    mapGroups = 0x08486578L,
)

val LEGACY_TRAINER_CARD_FIRERED = TrainerCardSave(
    style = CardStyle.KANTO,
    gameStatsOff = 0x1200, flagsOff = 0xEE0, varsOff = 0x1000,
    badgeFlag = 0x820, pokedexFlag = 0x829,
    nationalMagicOff = 0x18 + 3, nationalMagic = 0xB9, nationalVar = 0x404E, nationalVarValue = 0x6258, nationalFlag = 0x840,
    dexFlags = LEGACY_FIRERED_DEX_FLAGS,
    regionalDex = IntArray(151) { it + 1 },
    stickerVar = 0x4049,
)

val LEGACY_TRAINER_CARD_EMERALD = TrainerCardSave(
    style = CardStyle.HOENN,
    gameStatsOff = 0x159C, flagsOff = 0x1270, varsOff = 0x139C,
    badgeFlag = 0x867, pokedexFlag = 0x861,
    nationalMagicOff = 0x18 + 2, nationalMagic = 0xDA, nationalVar = 0x4046, nationalVarValue = 0x302, nationalFlag = 0x896,
    dexFlags = LEGACY_EMERALD_DEX_FLAGS,
    regionalDex = HOENN_TO_NATIONAL,
    frontierSymbolFlag = 0x8C4,
    museumWinnersOff = 0x2E90 + 8 * 32,
    frontierBpOff = 0xEBA,
)

val LEGACY_FIRERED_BAG_POCKET_ORDER = listOf(POCKET_ITEMS, POCKET_KEY_ITEMS, POCKET_POKE_BALLS, POCKET_TM_HM, POCKET_BERRIES)

val LEGACY_EMERALD_BAG_POCKET_ORDER = listOf(POCKET_ITEMS, POCKET_POKE_BALLS, POCKET_TM_HM, POCKET_BERRIES, POCKET_KEY_ITEMS)
