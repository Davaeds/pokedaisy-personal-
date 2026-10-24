package com.pokedaisy.app.companion

/**
 * WHAT'S NEW: the few lines a player cares about in a release, which the Library shows once after an update
 * ([com.pokedaisy.app.companion.ui.WhatsNewWindow]). Written with each release, beside `make bump` (docs/RELEASING.md):
 * short, the highlights only - RELEASE PAGE opens the GitHub release for the rest. English, like the manual; the window's
 * own words (title, section titles, buttons) are translated. WhatsNewTest fails while the app's version has no notes.
 */
object WhatsNew {
    class Section(
        /** Drawn through tr(): NEW or FIXES. */
        val title: String,
        val items: List<String>,
    )

    class Notes(val version: String, val sections: List<Section>)

    const val NEW = "NEW"
    const val FIXES = "FIXES"

    /** Newest first. */
    val RELEASES: List<Notes> = listOf(
        Notes(
            "1.3.2",
            listOf(
                Section(
                    NEW,
                    listOf(
                        "Phones held upright: the game on top, the companion in the middle and the touch controls at the bottom.",
                        "DS games: X and Y on the touch controls, and the buttons in the bottom corners when you hold one screen sideways.",
                        "A COMPANION hotkey opens the companion on one screen.",
                        "The party shows nicknames in the Game Boy games, R.O.W.E. and Elite Redux too.",
                    ),
                ),
                Section(
                    FIXES,
                    listOf(
                        "GBA games no longer crash when you close them.",
                        "Gold and Silver are listed under GBC.",
                        "SoulGold: shiny party icons, and NEXT BOSS follows the Elite Four.",
                        "The library no longer freezes while it reads new games.",
                    ),
                ),
            ),
        ),
    )

    fun notesFor(version: String): Notes? = RELEASES.firstOrNull { it.version == version }

    /**
     * [current]'s notes when they're due: the app was updated (a fresh install has nothing to tell) to a version the
     * player hasn't seen them for ([lastSeen]: Prefs.whatsNewSeen), and that version has notes.
     */
    fun due(current: String, lastSeen: String?, updated: Boolean): Notes? =
        if (updated && lastSeen != current) notesFor(current) else null
}
