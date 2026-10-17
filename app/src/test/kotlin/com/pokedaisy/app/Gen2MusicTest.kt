package com.pokedaisy.app

import com.pokedaisy.app.companion.data.GEN2_MUSIC_CRYSTAL_REV1
import com.pokedaisy.app.companion.data.Gen1Music
import com.pokedaisy.app.companion.data.MemoryReader
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class Gen2MusicTest {
    /** Crystal's audio WRAM, C100-C2FF. */
    private class FakeWram : MemoryReader {
        val ram = ByteArray(0x200)
        override fun readCoreMemory(addr: Long, size: Int) = ram.copyOfRange((addr - 0xC100).toInt(), (addr - 0xC100).toInt() + size)
        fun channel(n: Int, id: Int, on: Boolean) {
            val b = 1 + 0x32 * n
            ram[b] = id.toByte()
            ram[b + 3] = if (on) 1 else 0
        }
    }

    @Test fun theSongIsWhatTheChannelsPlayNotWMusicId() {
        // The Thor's state after a save loaded on ROUTE 29: wMusicID still 0x08, the channels playing 0x34.
        val r = FakeWram()
        r.ram[0x19D] = 0x08
        for (n in 0 until 4) r.channel(n, 0x34, on = true)
        assertEquals(Gen1Music.key(0, 0x34), Gen1Music.current(r, GEN2_MUSIC_CRYSTAL_REV1))
        // Channel 1 off (a song without it): the first one that's on.
        r.channel(0, 0x11, on = false)
        assertEquals(Gen1Music.key(0, 0x34), Gen1Music.current(r, GEN2_MUSIC_CRYSTAL_REV1))
    }

    @Test fun silenceIsNoSong() {
        val r = FakeWram()
        r.channel(0, 0x34, on = false)
        assertNull(Gen1Music.current(r, GEN2_MUSIC_CRYSTAL_REV1))
    }
}
