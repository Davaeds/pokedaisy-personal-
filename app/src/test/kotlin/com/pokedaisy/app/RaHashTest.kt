package com.pokedaisy.app

import com.pokedaisy.app.companion.data.RETAIL_ROM_DIR
import org.junit.Assert.assertEquals
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File

/**
 * The hash RetroAchievements identifies a ROM by (GitHub #231: DS games stuck on LOOKING UP THIS ROM): a DS cart's is
 * rcheevos' own (header + ARM9 / ARM7 code + icon block), which the server knows as Platinum's set 11732 and HeartGold /
 * SoulSilver's 7212 (its `r=gameid` answers, 2026-10-21). Skips without the ROMs (`-DromDir=`'s ../nds/).
 */
class RaHashTest {
    private val nds = File(File(RETAIL_ROM_DIR).parentFile, "nds")

    private fun check(name: String, hash: String) {
        val rom = File(nds, name)
        assumeTrue("no $rom", rom.isFile)
        assertEquals(18, RomIdentity.raConsole(rom))
        assertEquals(hash, RomIdentity.raHash(rom))
    }

    @Test fun platinum() = check("Pokemon - Platinum Version (USA) (Rev 1).nds", "4432098e35a63d0f2ff7bdbc7a848e3c")

    @Test fun heartGold() = check("Pokemon - HeartGold Version (USA).nds", "7a78d767cbcfce6262434d25aacd39bf")

    @Test fun soulSilver() = check("Pokemon - SoulSilver Version (USA).nds", "dfce09d0d107abfd669156f6e867578f")

    @Test fun gbaIsTheFilesMd5() {
        val rom = File(RETAIL_ROM_DIR, "Pokemon - Emerald Version (USA, Europe).gba")
        assumeTrue("no $rom", rom.isFile)
        assertEquals(5, RomIdentity.raConsole(rom))
        assertEquals(RomIdentity.md5(rom), RomIdentity.raHash(rom))
    }
}
