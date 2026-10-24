package com.pokedaisy.app

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import kotlin.math.abs

/**
 * Translates Android key/motion events into a live GBA key bitmask. Face /
 * shoulder / menu buttons are remappable via [setControls] (GbaControls); the
 * D-pad and analog stick are fixed.
 */
class GbaInput {
    @Volatile
    var mask: Int = 0
        private set

    private var buttonBits = 0   // from key events: the bits of every key in heldKeys
    private var turboBits = 0    // TURBO A / B held: pulsed by the engine ([turboMask])
    private val turboKeys = HashMap<Int, Int>()
    // Per key, not per bit: START and X are two keys for one bit, and
    // letting go of one mustn't release the other.
    private val heldKeys = HashMap<Int, Int>()
    private var hatBits = 0      // from D-pad HAT / analog axes
    private var touchBits = 0    // from the on-screen TouchControlsView
    private var scriptBits = 0   // synthetic presses (BattleInputController)
    private var scriptExclusive = false

    @Volatile
    private var controls: Map<Int, Int> = DEFAULT_CONTROLS

    fun setControls(map: Map<Int, Int>) { controls = map }

    /** A DS game runs: X / Y ([GbaControls.DS_X] / [GbaControls.DS_Y]) press its X / Y. */
    @Volatile var ds = false

    /** @return true if the event was consumed. */
    fun onKey(keyCode: Int, down: Boolean): Boolean {
        val bit = keyToBit(keyCode) ?: return false
        if (bit and GbaControls.TURBO != 0) {
            if (down) turboKeys[keyCode] = bit and GbaControls.TURBO.inv() else turboKeys.remove(keyCode)
            turboBits = turboKeys.values.fold(0) { acc, b -> acc or b }
            return true
        }
        if (down) heldKeys[keyCode] = bit else heldKeys.remove(keyCode)
        buttonBits = heldKeys.values.fold(0) { acc, b -> acc or b }
        recompute()
        return true
    }

    /** The GBA button [keyCode] is bound to (D-pad, or the player's GAME BUTTONS; a TURBO key counts as its button),
     * null = none. The companion's window reads keys through this, so it follows the player's bindings. */
    fun buttonFor(keyCode: Int): Int? = keyToBit(keyCode)?.let { it and GbaControls.TURBO.inv() }

    /** [keyCode] is bound to X (GAME BUTTONS), whatever it presses in the game running. */
    fun isX(keyCode: Int): Boolean = (controls[keyCode] ?: 0) and GbaControls.DS_X != 0

    /** Lets go of every key the player holds (not a script's): the companion's window took the buttons, or a rebind
     *  the keys' releases. */
    fun releaseAll() {
        heldKeys.clear()
        turboKeys.clear()
        buttonBits = 0
        turboBits = 0
        hatBits = 0
        touchBits = 0
        touchTurboBits = 0
        recompute()
    }

    /** The TURBO A / B buttons held (keys or the touch controls'), which the engine presses every other 2 frames;
     * none while a script steers. */
    val turboMask: Int get() = if (scriptExclusive) 0 else turboBits or touchTurboBits

    @Volatile private var touchTurboBits = 0

    /** The touch controls' GBA buttons held ([bits]) and their TURBO A / B ([turbo], pulsed like [turboMask]). */
    fun setTouchBits(bits: Int, turbo: Int = 0) {
        touchBits = forCore(bits)   // the touch X / Y: the DS's own X / Y, else START / SELECT
        touchTurboBits = turbo
        recompute()
    }

    /**
     * Synthetic presses from the companion's battle controls. While
     * [exclusive] (an automated sequence is running) the player's own keys -
     * buttons, D-pad, touch controls - are ignored, so they can't knock the
     * sequence off course; they count again the moment it ends.
     */
    fun setScript(bits: Int, exclusive: Boolean) {
        scriptBits = bits
        scriptExclusive = exclusive
        recompute()
    }

    /** One-off presses for the game (the QUICK MENU's SELECT for a bike or a rod): pressed on top of everything else,
     * apart from [setScript]'s channel, which the battle controls send every frame (they'd wipe it). */
    @Volatile private var pressBits = 0

    fun setPress(bits: Int) {
        pressBits = bits
        recompute()
    }

    fun onMotion(event: MotionEvent): Boolean {
        hatBits = motionBits(event) ?: return false
        recompute()
        return true
    }

    private fun recompute() {
        mask = (if (scriptExclusive) scriptBits else buttonBits or hatBits or touchBits or scriptBits) or pressBits
    }

    private fun keyToBit(keyCode: Int): Int? = when (keyCode) {
        KeyEvent.KEYCODE_DPAD_UP -> MgbaCore.Key.UP
        KeyEvent.KEYCODE_DPAD_DOWN -> MgbaCore.Key.DOWN
        KeyEvent.KEYCODE_DPAD_LEFT -> MgbaCore.Key.LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT -> MgbaCore.Key.RIGHT
        // Some controllers (8BitDo, GameSir in a few modes) send a diagonal as one key.
        KeyEvent.KEYCODE_DPAD_UP_LEFT -> MgbaCore.Key.UP or MgbaCore.Key.LEFT
        KeyEvent.KEYCODE_DPAD_UP_RIGHT -> MgbaCore.Key.UP or MgbaCore.Key.RIGHT
        KeyEvent.KEYCODE_DPAD_DOWN_LEFT -> MgbaCore.Key.DOWN or MgbaCore.Key.LEFT
        KeyEvent.KEYCODE_DPAD_DOWN_RIGHT -> MgbaCore.Key.DOWN or MgbaCore.Key.RIGHT
        else -> controls[keyCode]?.let(::forCore)   // remappable face/shoulder/menu buttons
    }

    /** A binding's bits for the game running: X / Y are X / Y on a DS, else START / SELECT. */
    private fun forCore(bits: Int): Int {
        val marks = GbaControls.DS_X or GbaControls.DS_Y
        if (bits and marks == 0) return bits
        if (!ds) return bits and marks.inv()
        var out = bits and marks.inv() and (MgbaCore.Key.START or MgbaCore.Key.SELECT).inv()
        if (bits and GbaControls.DS_X != 0) out = out or MgbaCore.Key.X
        if (bits and GbaControls.DS_Y != 0) out = out or MgbaCore.Key.Y
        return out
    }

    companion object {
        private const val DEAD = 0.5f

        /** The D-pad directions a joystick event holds (its HAT, else the left stick); null = not a joystick's. */
        fun motionBits(event: MotionEvent): Int? {
            if (event.source and InputDevice.SOURCE_JOYSTICK != InputDevice.SOURCE_JOYSTICK) return null
            val hx = event.getAxisValue(MotionEvent.AXIS_HAT_X)
            val hy = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
            val x = if (abs(hx) > 0.5f) hx else event.getAxisValue(MotionEvent.AXIS_X)
            val y = if (abs(hy) > 0.5f) hy else event.getAxisValue(MotionEvent.AXIS_Y)
            var bits = 0
            if (x < -DEAD) bits = bits or MgbaCore.Key.LEFT
            if (x > DEAD) bits = bits or MgbaCore.Key.RIGHT
            if (y < -DEAD) bits = bits or MgbaCore.Key.UP
            if (y > DEAD) bits = bits or MgbaCore.Key.DOWN
            return bits
        }

        private val DEFAULT_CONTROLS = mapOf(
            KeyEvent.KEYCODE_BUTTON_A to MgbaCore.Key.A, KeyEvent.KEYCODE_Z to MgbaCore.Key.A,
            KeyEvent.KEYCODE_BUTTON_B to MgbaCore.Key.B, KeyEvent.KEYCODE_X to MgbaCore.Key.B,
            KeyEvent.KEYCODE_BUTTON_L1 to MgbaCore.Key.L, KeyEvent.KEYCODE_A to MgbaCore.Key.L,
            KeyEvent.KEYCODE_BUTTON_R1 to MgbaCore.Key.R, KeyEvent.KEYCODE_S to MgbaCore.Key.R,
            KeyEvent.KEYCODE_BUTTON_START to MgbaCore.Key.START, KeyEvent.KEYCODE_ENTER to MgbaCore.Key.START,
            KeyEvent.KEYCODE_BUTTON_SELECT to MgbaCore.Key.SELECT,
            KeyEvent.KEYCODE_BACKSLASH to MgbaCore.Key.SELECT, KeyEvent.KEYCODE_SHIFT_RIGHT to MgbaCore.Key.SELECT,
        )
    }
}
