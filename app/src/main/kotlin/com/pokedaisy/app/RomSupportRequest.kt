package com.pokedaisy.app

import java.io.File
import java.net.URLEncoder

/**
 * ASK FOR SUPPORT: the website's game request form (pokedaisy.com/feedback), its fields filled through the
 * URL's #hash like the website's ROM check does (RomCheck.astro's formLink) - the file's name, game code,
 * size and SHA-1. Only those, and a hash never reaches the server: the page posts it to GitHub's Game
 * requests discussions when the player sends it (or links there for a GitHub account).
 */
object RomSupportRequest {
    private const val FORM = "https://pokedaisy.com/feedback/"

    /** The form's address for [rom] (an archive: the ROM inside). Reads the whole ROM for its SHA-1, so off the UI thread. */
    fun url(rom: File, sha1: String? = RomIdentity.sha1(rom)): String {
        val name = RomArchive.baseName(rom)
        val code = RomIdentity.gameCode(rom)?.takeIf { c -> c.all { it in 'A'..'Z' || it in '0'..'9' } }
        val fields = linkedMapOf(
            "game" to name,
            "game_code" to when {
                RomIdentity.isGameBoy(rom) -> "Game Boy"
                code != null -> "$code (rev ${RomIdentity.revision(rom) ?: 0})"
                else -> ""
            },
            "size" to (runCatching { RomArchive.romSize(rom) }.getOrNull() ?: rom.length()).toString(),
            "sha1" to (sha1 ?: ""),
        )
        return FORM + "#" + fields.entries.joinToString("&") { (k, v) -> "$k=${enc(v)}" }
    }

    private fun enc(s: String) = URLEncoder.encode(s, "UTF-8").replace("+", "%20")
}
