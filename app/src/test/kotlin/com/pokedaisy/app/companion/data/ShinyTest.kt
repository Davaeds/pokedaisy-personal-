package com.pokedaisy.app.companion.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * SoulGold's party menu draws shiny Pokémon in their shiny icon palette (GitHub #228). Its rule, found headless on v1.1.4
 * and v1.2 by setting the lead's personality and opening the party menu: shiny while the shiny value is below 256.
 */
class ShinyTest {
    private val lead = 0x02039024 - 0x02000000   // NATIVE_SOULGOLD_V1_2's playerParty, in the fixture's EWRAM

    private fun cyndaquil(value: Int?): Mon {
        val ram = File("src/test/resources/fixtures/soulgold_v12/ewram.bin").readBytes()
        if (value != null) {
            val otId = u32(ram, lead + 4)
            val pid = otId xor value.toLong()
            for (i in 0 until 4) ram[lead + i] = (pid ushr (8 * i)).toByte()
        }
        return decodePartyMon(ram, lead, SOULGOLD_PARTY_MON)!!
    }

    private fun u32(b: ByteArray, o: Int) =
        (0 until 4).fold(0L) { acc, i -> acc or ((b[o + i].toLong() and 0xFF) shl (8 * i)) }

    @Test fun theSavesLeadIsNotShiny() {
        val m = cyndaquil(null)
        assertEquals(0x1CF6, shinyValue(m.personality, u32(File("src/test/resources/fixtures/soulgold_v12/ewram.bin").readBytes(), lead + 4)))
        assertFalse(m.shiny)
    }

    @Test fun belowTwoFiftySixIsShiny() {
        assertTrue(cyndaquil(0).shiny)
        assertTrue(cyndaquil(255).shiny)
        assertFalse(cyndaquil(256).shiny)
    }

    @Test fun vanillaLayoutsNeverReadIt() {
        val m = decodeNative("firered_vanilla", NATIVE_FIRERED_REV1)
        assertTrue(m.party.none { it.shiny })
    }
}
