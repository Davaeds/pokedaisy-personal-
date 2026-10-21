package com.pokedaisy.app

import android.util.Log
import android.view.KeyEvent
import java.io.File
import java.util.Properties

/**
 * Remappable mapping of physical keys/buttons to GBA buttons, from
 * `files/controls.properties`. One or more key names per GBA button
 * (comma-separated; any of them triggers it). The D-pad and analog stick stay
 * fixed — only the face/shoulder/menu buttons are here.
 */
object GbaControls {

    /** X / Y: the DS games' X (menu) and Y (registered item); in the other games a second START / SELECT (they
     * were START 2 / SELECT 2, still their names in the file: [prop]). On the field of a two-screen device X takes
     * the buttons to the companion instead (PokeDaisyActivity's bottom focus). TURBO A / B (issue #32, unbound by
     * default) press A / B on and off while held - [TURBO] marks them in [load]'s map, [GbaInput] pulses them. */
    enum class Btn(val bit: Int, val label: String) {
        A(MgbaCore.Key.A, "A"), B(MgbaCore.Key.B, "B"),
        L(MgbaCore.Key.L, "L"), R(MgbaCore.Key.R, "R"),
        START(MgbaCore.Key.START, "START"), SELECT(MgbaCore.Key.SELECT, "SELECT"),
        // On a DS game they're its own X / Y ([DS_X] / [DS_Y]: GbaInput swaps them in).
        X(MgbaCore.Key.START or DS_X, "X"), Y(MgbaCore.Key.SELECT or DS_Y, "Y"),
        TURBO_A(MgbaCore.Key.A or TURBO, "TURBO A"), TURBO_B(MgbaCore.Key.B or TURBO, "TURBO B");

        val prop get() = when (this) { X -> "start2"; Y -> "select2"; else -> name.lowercase() }
        /** Its row's name in GAME BUTTONS: the GBA's own buttons say so, X / Y are the DS's. */
        val title get() = if (this == X || this == Y) label else "GBA $label"
        /** A line under its row: what X / Y press in the games that have no X / Y (English; translated where drawn). */
        /** X's NEW tag: on the field of a two-screen device it takes the buttons to the companion. */
        val badge: String? get() = if (this == X) com.pokedaisy.app.companion.NewFeatures.badge(com.pokedaisy.app.companion.NewFeatures.X_COMPANION) else null
        val subtitle: String? get() = when (this) {
            X -> "DS games' X. Others: START, or the companion on the field"
            Y -> "DS games' Y. Others: SELECT"
            else -> null
        }
        /** TURBO A / B start unbound and can go back to it; every other button always keeps a key. */
        val canClear get() = this == TURBO_A || this == TURBO_B
    }

    private val DEFAULTS = linkedMapOf(
        "a" to "BUTTON_A, Z",
        "b" to "BUTTON_B, X",
        "l" to "BUTTON_L1, A",
        "r" to "BUTTON_R1, S",
        "start" to "BUTTON_START, ENTER",
        "select" to "BUTTON_SELECT, BACKSLASH, SHIFT_RIGHT",
        // Face buttons go by Android's names (Xbox positions) on every device, even where
        // the printed labels differ (AYN, Retroid: BUTTON_A is the bottom one); GAME BUTTONS remaps.
        "start2" to "BUTTON_X",
        "select2" to "BUTTON_Y",
        "turbo_a" to "",
        "turbo_b" to "",
    )

    /** Above every GBA key bit: a [load] value with it is a turbo button for the bits below. */
    const val TURBO = 1 shl 16

    /** X / Y's mark: on a DS game ([GbaInput.ds]) the key presses the DS's X / Y instead. */
    const val DS_X = 1 shl 17
    const val DS_Y = 1 shl 18

    private fun file(dir: File) = File(dir, "controls.properties")

    private fun readProps(dir: File): Properties {
        val p = Properties()
        val f = file(dir)
        if (f.isFile) runCatching { f.inputStream().use(p::load) }
        var changed = false
        for ((k, v) in DEFAULTS) if (!p.containsKey(k)) { p.setProperty(k, v); changed = true }
        if (changed) runCatching {
            f.outputStream().use { p.store(it, "PokeDaisy GBA button map — Android key names, comma-separated") }
        }
        return p
    }

    /** keycode -> GBA key bit ([TURBO] set for TURBO A / B). A key bound twice takes the later button. */
    fun load(dir: File): Map<Int, Int> {
        val props = readProps(dir)
        val out = HashMap<Int, Int>()
        for (btn in Btn.entries) {
            props.getProperty(btn.prop).orEmpty().split(',').forEach { n ->
                resolveKey(n.trim())?.let { out[it] = btn.bit }
            }
        }
        return out
    }

    fun rawBindings(dir: File): Map<Btn, List<String>> {
        val props = readProps(dir)
        return Btn.entries.associateWith {
            props.getProperty(it.prop).orEmpty().split(',').map { s -> s.trim() }.filter { s -> s.isNotEmpty() }
        }
    }

    /** [btn] is [keyCode] now - and that key leaves any other button it was on (else the later button kept it). */
    fun setBinding(dir: File, btn: Btn, keyCode: Int) {
        val props = readProps(dir)
        val name = Hotkeys.keyName(keyCode)
        for (other in Btn.entries) if (other != btn) {
            val keys = props.getProperty(other.prop).orEmpty().split(',').map { it.trim() }.filter { it.isNotEmpty() }
            if (name in keys) props.setProperty(other.prop, (keys - name).joinToString(", "))
        }
        props.setProperty(btn.prop, name)
        runCatching { file(dir).outputStream().use { props.store(it, "PokeDaisy GBA button map") } }
    }

    /** Leaves [btn] unbound - TURBO A / B only ([Btn.canClear]). */
    fun clearBinding(dir: File, btn: Btn) {
        if (!btn.canClear) return
        val props = readProps(dir)
        props.setProperty(btn.prop, "")
        runCatching { file(dir).outputStream().use { props.store(it, "PokeDaisy GBA button map") } }
    }

    private fun resolveKey(name: String): Int? {
        if (name.isEmpty()) return null
        return runCatching { KeyEvent::class.java.getField("KEYCODE_$name").getInt(null) }
            .onFailure { Log.w("pokedaisy", "unknown control key: $name") }.getOrNull()
    }
}
