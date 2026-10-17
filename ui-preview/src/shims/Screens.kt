package com.pokedaisy.app

/** Stand-in for the app's Screens (DisplayManager): the Thor, a 1240x1080 second screen. */
object Screens {
    fun hasSecond(context: android.content.Context) = true
    fun secondAspect(context: android.content.Context): Float? = 1240f / 1080f
    /** The preview is the main display: no options needed. */
    fun mainDisplayLaunch(activity: Any): android.os.Bundle? = null
    fun isOnMainDisplay(activity: Any) = true
    fun reopenOnMainDisplay(activity: Any) = false
    fun bringToMainDisplay(activity: Any) {}
    /** The Thor's bottom screen is built in: SWAP SCREENS is [Prefs.swapScreens]. */
    fun swapped(context: android.content.Context, prefs: Prefs): Boolean = prefs.swapScreens
    fun setSwapped(context: android.content.Context, prefs: Prefs, on: Boolean) { prefs.swapScreens = on }
}
