package com.pokedaisy.app

import android.view.InputDevice
import android.view.KeyEvent

/** Game controllers attached to the device, for the touch controls' AUTO (the game's activity and Settings). */
object Controllers {
    /** A real controller: built-in handheld controls or a pad, never a phone's own keys / sensors. Some phones list a
     * built-in device with a joystick source (and no buttons), which used to hide AUTO's touch controls for good; a
     * gamepad source with an A button is what counts. */
    fun isGamepad(d: InputDevice) = !d.isVirtual &&
        d.sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD &&
        d.hasKeys(KeyEvent.KEYCODE_BUTTON_A).firstOrNull() == true

    /** Whether any controller is attached now. */
    fun anyConnected(): Boolean = InputDevice.getDeviceIds().any { id -> InputDevice.getDevice(id)?.let(::isGamepad) == true }
}
