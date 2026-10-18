package com.pokedaisy.app

import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/** Where a game's save is: its own folder, else the first searched folder holding one, else a new one in the first. */
class SavesLocationTest {
    @get:Rule
    val tmp = TemporaryFolder()

    private val rom get() = File(tmp.root, "FireRed.gba")
    private fun dir(name: String) = tmp.newFolder(name)

    @Test fun newSaveGoesToTheSavesFolder() {
        val saves = dir("saves")
        assertEquals(File(saves, "FireRed.sav"), SavesLocation.saveIn(null, listOf(saves, dir("mGBA")), rom))
    }

    @Test fun anotherFoldersSaveIsUsedWhereItIs() {
        val saves = dir("saves")
        val retroArch = dir("mGBA").also { File(it, "FireRed.srm").writeText("x") }
        assertEquals(File(retroArch, "FireRed.srm"), SavesLocation.saveIn(null, listOf(saves, retroArch), rom))
    }

    @Test fun theSavesFolderWinsWhenBothHaveOne() {
        val saves = dir("saves").also { File(it, "FireRed.sav").writeText("a") }
        val retroArch = dir("mGBA").also { File(it, "FireRed.srm").writeText("b") }
        assertEquals(File(saves, "FireRed.sav"), SavesLocation.saveIn(null, listOf(saves, retroArch), rom))
    }

    @Test fun aGamesOwnFolderComesFirst() {
        val saves = dir("saves").also { File(it, "FireRed.sav").writeText("a") }
        val own = dir("own")
        assertEquals(File(own, "FireRed.sav"), SavesLocation.saveIn(own, listOf(saves), rom))
    }

    @Test fun onlyTheFolderItselfIsSearched() {
        val saves = dir("saves")
        File(saves, ".stversions").mkdirs()
        File(saves, ".stversions/FireRed~20261001.sav").writeText("old")
        assertEquals(File(saves, "FireRed.sav"), SavesLocation.saveIn(null, listOf(saves), rom))
    }

    @Test fun aDsGamesSaveIsBesideItsRomLikeMelonDs() {
        val roms = dir("nds")
        val ds = File(roms, "Platinum.nds")
        val saves = dir("ds-saves")
        // A new save goes beside the ROM; one there wins over the saves folder's.
        assertEquals(File(roms, "Platinum.sav"), SavesLocation.saveIn(null, SavesLocation.dirsFor(ds, true, listOf(saves)), ds))
        File(saves, "Platinum.sav").writeText("app")
        File(roms, "Platinum.sav").writeText("melonDS")
        assertEquals(File(roms, "Platinum.sav"), SavesLocation.saveIn(null, SavesLocation.dirsFor(ds, true, listOf(saves)), ds))
        // A save the app made elsewhere before still loads while there's none beside the ROM.
        File(roms, "Platinum.sav").delete()
        assertEquals(File(saves, "Platinum.sav"), SavesLocation.saveIn(null, SavesLocation.dirsFor(ds, true, listOf(saves)), ds))
        // A GBA game is unchanged.
        assertEquals(listOf(saves), SavesLocation.dirsFor(rom, false, listOf(saves)))
    }
}
