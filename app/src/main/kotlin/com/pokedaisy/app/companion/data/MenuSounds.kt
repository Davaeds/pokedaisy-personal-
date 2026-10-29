package com.pokedaisy.app.companion.data

import java.io.File

/**
 * MENU SOUNDS (SETTINGS > GAMEPLAY): the game's menu beep as it is, another of the game's own sounds in its place
 * ([Choice]), or off; everything else heard as before.
 *
 * Moving a menu's cursor and picking from it both play SE_SELECT, song 5 in every Gen 3 Pokémon song table (the click
 * [com.pokedaisy.app.GameClickSound] renders). A game's own SOUND option silences it only by silencing the cries too, so
 * this points song 5's `gSongTable` entry ([M4aSongs.songTable], 8-byte `{SongHeader*, u16 ms, u16 me}` entries) at a
 * song with no tracks: the game still asks for the beep, `m4aSongNumStart` starts nothing. Cries never go through the
 * table (m4aMPlayCry), so they stay.
 *
 * The silent song is the table's own: song 0 (MUS_DUMMY, the songs the games start to stop the music) or the first
 * entry whose header has no tracks. Patched through [com.pokedaisy.app.MgbaCore.pkRomPatch] like REUSABLE TMS: only where
 * the ROM holds the old pointer, the file's bytes kept for the Poller's hash and RetroAchievements'.
 *
 * Another sound goes in the same way, its header in song 5's entry (still played on the beep's own player), a little
 * quieter than the beep (the player's call): its tracks' VOL commands scaled so the loudest is [OTHER_VOLUME] of the
 * beep's loudest. Those bytes are that sound's own, so it's as much quieter wherever else the game plays it.
 */
object MenuSounds {
    /** MENU SOUNDS' choices (Prefs.menuSound by name): the beep, a few of the Gen 3 song tables' other sound effects
     * (pret's SE_CLICK, SE_PIN, SE_NOTE_C / _E / _G), or none. */
    enum class Choice(val label: String, val song: Int) {
        ON("ON", SE_SELECT), CLICK("CLICK", 30), PIN("PIN", 21), NOTE_C("NOTE C", 55), NOTE_E("NOTE E", 57),
        NOTE_G("NOTE G", 59), OFF("OFF", -1),
    }

    /** A VOL command's value byte at [addr]: [from] in the ROM, [to] while the sound stands in for the beep. */
    class Vol(val addr: Long, val from: Byte, val to: Byte)

    /** Another sound for the beep: its song header (the pointer for song 5's entry) and its volume bytes turned down. */
    class Other(val header: ByteArray, val vols: List<Vol>)

    /** The patch: [entry] (gSongTable + 5 * 8) holds [beep] now and gets [silent], or one of [others]' headers. */
    class Patch(val entry: Long, val beep: ByteArray, val silent: ByteArray, val others: Map<Choice, Other> = emptyMap())

    /** SE_SELECT's place in the song table. */
    const val SE_SELECT = 5

    private const val ROM = 0x08000000L
    private const val MAX_SCAN = 1024
    private const val MAX_TRACKS = 16
    private const val MAX_TRACK_BYTES = 1024
    private const val MAX_VOLS = 32          // the option patches' 64 slots, shared
    const val OTHER_VOLUME = 0.8f

    private val cache = HashMap<String, Patch?>()

    /** The patch for [rom], or null where the table or a silent song isn't found. Reads the file: off the UI thread;
     * cached per file. */
    fun find(rom: File): Patch? {
        val key = "${rom.path}:${rom.length()}:${rom.lastModified()}"
        synchronized(cache) { if (cache.containsKey(key)) return cache[key] }
        val found = runCatching { find(rom.readBytes()) }.getOrNull()
        synchronized(cache) { cache[key] = found }
        return found
    }

    /** Only the Gen 3 Pokémon engines (by the header's game code: FireRed / LeafGreen / Emerald / Ruby / Sapphire and
     * the hacks on them, which keep the code, plus Ultra Violet's PUVV): other GBA games use the same sound engine, but
     * their song 5 can be any sound. */
    fun isPokemonCode(code: String): Boolean = listOf("BPR", "BPG", "BPE", "AXV", "AXP", "PUV").any { code.startsWith(it) }

    fun find(rom: ByteArray): Patch? {
        if (rom.size < 0xB0) return null
        if (!isPokemonCode(String(rom, 0xAC, 4, Charsets.US_ASCII))) return null
        val songs = M4aSongs.locate(rom) ?: return null
        val table = (songs.songTable - ROM).toInt()
        fun headerOf(n: Int): Long? {
            val at = table + n * 8
            if (at < 0 || at + 8 > rom.size) return null
            return u32(rom, at).takeIf { it in ROM until ROM + rom.size }
        }
        fun tracks(header: Long): Int = rom[(header - ROM).toInt()].toInt() and 0xFF
        val beep = headerOf(SE_SELECT) ?: return null
        // A real sound effect: some tracks, on a player other than the music's (ms 1+).
        if (tracks(beep) !in 1..MAX_TRACKS) return null
        if (u16(rom, table + SE_SELECT * 8 + 4) == 0) return null
        val silent = (listOf(0) + (1 until MAX_SCAN)).firstNotNullOfOrNull { n ->
            if (n == SE_SELECT) null else headerOf(n)?.takeIf { tracks(it) == 0 }
        } ?: return null
        val beepVols = volumes(rom, beep) ?: emptyList()
        val beepLoud = beepVols.maxOfOrNull { it.second } ?: 127   // no VOL: the player's full 127
        val others = LinkedHashMap<Choice, Other>()
        for (c in Choice.entries) {
            if (c.song < 0 || c.song == SE_SELECT) continue
            val header = headerOf(c.song) ?: continue
            if (tracks(header) !in 1..MAX_TRACKS || u16(rom, table + c.song * 8 + 4) == 0) continue
            val vols = volumes(rom, header)
            if (vols.isNullOrEmpty() || vols.size > MAX_VOLS) continue   // unreadable, or nothing to turn down
            val loud = vols.maxOf { it.second }
            val target = beepLoud * OTHER_VOLUME
            others[c] = Other(le32(header), vols.map { (at, v) ->
                Vol(ROM + at, v.toByte(), Math.round(v * target / loud).coerceIn(1, 127).toByte())
            })
        }
        return Patch(songs.songTable + SE_SELECT * 8, le32(beep), le32(silent), others)
    }

    /** Every VOL command's value in the song at [header] (file offset, value), its tracks read as the m4a engine reads
     * them (m4a_1.s's ply_* commands, running status included) up to FINE / GOTO; null where a track doesn't read. */
    private fun volumes(rom: ByteArray, header: Long): List<Pair<Int, Int>>? {
        val h = (header - ROM).toInt()
        val n = rom[h].toInt() and 0xFF
        val out = ArrayList<Pair<Int, Int>>()
        for (t in 0 until n) {
            if (h + 8 + 4 * t + 4 > rom.size) return null
            val start = u32(rom, h + 8 + 4 * t)
            if (start !in ROM until ROM + rom.size) return null
            var i = (start - ROM).toInt()
            val end = minOf(rom.size, i + MAX_TRACK_BYTES)
            var last = 0
            fun b(k: Int) = rom[k].toInt() and 0xFF
            fun args(k: Int, max: Int): Int { var j = 0; while (j < max && k + j < end && b(k + j) < 0x80) j++; return j }
            var done = false
            while (!done) {
                if (i >= end) return null
                val c = b(i)
                when {
                    c < 0x80 -> when {   // running status: the last command again
                        last == 0xBE -> { out += i to c; i++ }
                        last in 0xBA..0xC8 -> i++
                        last >= 0xCF -> i += args(i, 3)
                        else -> return null
                    }
                    c <= 0xB0 -> i++                                   // wait
                    c == 0xB1 || c == 0xB2 -> done = true             // FINE, GOTO
                    c == 0xB3 -> i += 5                                // PATT (its pattern not followed)
                    c == 0xB4 -> i++                                   // PEND
                    c == 0xB5 -> i += 6                                // REPT
                    c == 0xB9 -> i += 4                                // MEMACC
                    c in 0xBA..0xC8 -> {                               // one argument; VOL is 0xBE
                        if (i + 1 >= end || b(i + 1) >= 0x80) return null
                        if (c == 0xBE) out += (i + 1) to b(i + 1)
                        last = c
                        i += 2
                    }
                    c == 0xCD -> i += 3                                // XCMD
                    c == 0xCE -> { last = c; i += 1 + args(i + 1, 1) } // EOT
                    c >= 0xCF -> { last = c; i += 1 + args(i + 1, 3) } // TIE / notes: key, velocity, gate
                    else -> return null
                }
            }
        }
        return out
    }

    private fun le32(v: Long) = ByteArray(4) { ((v shr (8 * it)) and 0xFF).toByte() }
    private fun u16(b: ByteArray, i: Int): Int = (b[i].toInt() and 0xFF) or ((b[i + 1].toInt() and 0xFF) shl 8)
    private fun u32(b: ByteArray, i: Int): Long =
        (u16(b, i).toLong()) or (u16(b, i + 2).toLong() shl 16)
}
