package com.pokedaisy.app.companion.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Phase 1 of gamedata/: the NativeConfigs generated from gamedata/gen3/ (NativeConfigsGen.kt) are the
 * ones NativeReader.kt used to write by hand (LegacyNativeConfigs.kt), value for value. Every config of
 * both files is found by reflection, private ones included, so a config dropped or added on one side
 * fails too; the language ports (gamedata/gen3/ports/) against the RETAIL_PORTS port_retail.py used to
 * write (LegacyRetailPorts.kt). This test and the Legacy*.kt files go once the gate has passed.
 */
class GameDataMigrationTest {
    private fun configs(fileClass: String, prefix: String): Map<String, NativeConfig> =
        Class.forName("com.pokedaisy.app.companion.data.$fileClass").declaredFields
            .filter { it.type == NativeConfig::class.java && it.name.startsWith(prefix) }
            .associate { f ->
                f.isAccessible = true
                f.name.removePrefix(prefix) to f.get(null) as NativeConfig
            }

    private val generated = configs("NativeConfigsGenKt", "NATIVE_")
    private val legacy = configs("LegacyNativeConfigsKt", "LEGACY_NATIVE_")

    @Test
    fun `every hand-written config has a generated one and no more`() {
        assertEquals(68, legacy.size)
        assertEquals(legacy.keys.sorted(), generated.keys.sorted())
    }

    @Test
    fun `every generated config equals the hand-written one`() {
        val differ = legacy.keys.sorted().filter { legacy[it] != generated[it] }.map { name ->
            val old = legacy.getValue(name).toString().removeSurrounding("NativeConfig(", ")").split(", ")
            val new = generated.getValue(name).toString().removeSurrounding("NativeConfig(", ")").split(", ")
            "$name: ${(old - new.toSet()).joinToString()} -> ${(new - old.toSet()).joinToString()}"
        }
        assertTrue("generated configs that differ:\n" + differ.joinToString("\n"), differ.isEmpty())
    }

    @Test
    fun `every language port equals the one port_retail wrote in Kotlin`() {
        assertEquals(34, LEGACY_RETAIL_PORTS.size)
        assertEquals(LEGACY_RETAIL_PORTS.keys.sorted(), RETAIL_PORTS.keys.sorted())
        val differ = LEGACY_RETAIL_PORTS.keys.sorted().filter { LEGACY_RETAIL_PORTS.getValue(it)() != RETAIL_PORTS.getValue(it)() }
        assertTrue("generated ports that differ: $differ", differ.isEmpty())
    }

    @Test
    fun `a copied config is still its own object`() {
        // fireRedHackFor tells Ghost Grey v1.0.1 from v1.0.2+ by identity, not by value.
        assertEquals(NATIVE_GHOST_GREY, NATIVE_GHOST_GREY_V1_0_1)
        assertNotSame(NATIVE_GHOST_GREY, NATIVE_GHOST_GREY_V1_0_1)
    }
}
