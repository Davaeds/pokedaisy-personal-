package com.pokedaisy.app.companion.data

import java.io.File

/**
 * MENU SOUNDS (SETTINGS > GAMEPLAY): the game's menu beep off, everything else heard as before.
 *
 * Moving a menu's cursor and picking from it both play SE_SELECT, song 5 in every Gen 3 song table (the click
 * [com.pokedaisy.app.GameClickSound] renders). A game's own SOUND option silences it only by silencing the cries too, so
 * this points song 5's `gSongTable` entry ([M4aSongs.songTable], 8-byte `{SongHeader*, u16 ms, u16 me}` entries) at a
 * song with no tracks: the game still asks for the beep, `m4aSongNumStart` starts nothing. Cries never go through the
 * table (m4aMPlayCry), so they stay.
 *
 * The silent song is the table's own: song 0 (MUS_DUMMY, the songs the games start to stop the music) or the first
 * entry whose header has no tracks. Patched through [com.pokedaisy.app.MgbaCore.pkRomPatch] like REUSABLE TMS: only where
 * the ROM holds the old pointer, the file's bytes kept for the Poller's hash and RetroAchievements'.
 */
object MenuSounds {
    /** The patch: [entry] (gSongTable + 5 * 8) holds [beep] now and gets [silent]. */
    class Patch(val entry: Long, val beep: ByteArray, val silent: ByteArray)

    /** SE_SELECT's place in the song table. */
    const val SE_SELECT = 5

    private const val ROM = 0x08000000L
    private const val MAX_SCAN = 1024
    private const val MAX_TRACKS = 16

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

    fun find(rom: ByteArray): Patch? {
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
        return Patch(songs.songTable + SE_SELECT * 8, le32(beep), le32(silent))
    }

    private fun le32(v: Long) = ByteArray(4) { ((v shr (8 * it)) and 0xFF).toByte() }
    private fun u16(b: ByteArray, i: Int): Int = (b[i].toInt() and 0xFF) or ((b[i + 1].toInt() and 0xFF) shl 8)
    private fun u32(b: ByteArray, i: Int): Long =
        (u16(b, i).toLong()) or (u16(b, i + 2).toLong() shl 16)
}
