package com.pokedaisy.app

import android.content.Context
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.platform.ComposeView
import com.pokedaisy.app.companion.ui.CompanionBack
import com.pokedaisy.app.companion.ui.LocalClickSound
import com.pokedaisy.app.companion.ui.LocalCompanionTopInset
import com.pokedaisy.app.companion.ui.PortraitCloseTab
import com.pokedaisy.app.companion.ui.PortraitGrip
import com.pokedaisy.app.companion.ui.PortraitOpenTab
import com.pokedaisy.app.companion.ui.PortraitLayout
import com.pokedaisy.app.companion.ui.SidePanelCompanion

/**
 * The companion on a single-screen device held upright (a phone in portrait):
 * the game across the top of the screen ([GameStageLayout.topAligned]), the
 * companion under it, always open - along the screen's bottom, or right under
 * the game with the touch controls below it ([Prefs.portraitCompanionUnderGame]; see
 * [PortraitLayout.arrange]). Its height is the player's
 * ([Prefs.portraitCompanionRatio]): drag the grip on its edge, or tap it to step
 * through small / the Thor's shape / everything under the game. The gap left
 * takes the touch controls when it's shown and fits; else the pad lies over the game
 * as usual. The grip has a strip of its own and never covers the game; only a
 * screen too short for anything else puts it inside the companion's top. BACK is the companion's back
 * ([back]).
 *
 * The X beside the grip puts the companion away ([hidden], remembered in [Prefs.portraitCompanionHidden]): it slides
 * down off the screen and the game, centred, and the touch controls get the whole screen, as with the side panel
 * closed held sideways. A tab on the screen's bottom edge, or a BACK tap, slides it back up. A DS game's touch screen
 * can't be put away (it's the game's second screen).
 *
 * Held sideways the same device gets [SidePanel] instead (PokeDaisyActivity
 * switches between them on rotation).
 */
class PortraitPanel(
    private val root: GameStageLayout,
    private val game: View,
    private val touchControls: View,
    private val prefs: Prefs,
    private val back: CompanionBack,
    private val clickSound: () -> Unit,
    /** Put away (true) or back up: the activity lays the touch controls out again for the new room. */
    private val onHiddenChanged: (Boolean) -> Unit = {},
    private val companion: @Composable () -> Unit,
) {
    private val context: Context get() = root.context

    var enabled = false
        private set
    private var ratio = prefs.portraitCompanionRatio

    /** A DS game: its touch screen fills the companion's place ([DsTouchPane]), with a height of its own
     * ([Prefs.portraitDsRatio]). */
    var ds = false
        set(v) {
            if (field == v) return
            field = v
            ratio = storedRatio()
            apply()
        }

    private fun storedRatio() = if (ds) prefs.portraitDsRatio else prefs.portraitCompanionRatio

    /** ASPECT > WIDE: the game fills everything above the companion ([GameStageLayout.expand]). */
    var expand = false
        set(v) {
            if (field == v) return
            field = v
            apply()
        }
    private var underGame = prefs.portraitCompanionUnderGame

    /** The touch controls' own strip along the bottom (px) for EXPAND + UNDER GAME; 0 while they're hidden. */
    var padStrip = 0
        set(v) {
            if (field == v) return
            field = v
            apply()
        }

    /** Put away by its X: the game has the whole screen ([Prefs.portraitCompanionHidden]). */
    private var hiddenPref = prefs.portraitCompanionHidden

    /** Put away right now (never a DS game's touch screen). */
    val hidden: Boolean get() = hiddenPref && !ds

    private var panel: View? = null
    private var gripView: View? = null
    private var closeTab: View? = null
    private var openTab: View? = null
    private val grip = mutableStateOf(PortraitLayout.Grip.ABOVE)
    private val topInset = mutableIntStateOf(0)
    private var laidOutFor = Triple(0, 0, 0)

    private val screenW get() = root.width.takeIf { it > 0 } ?: context.resources.displayMetrics.widthPixels
    private val screenH get() = root.height.takeIf { it > 0 } ?: context.resources.displayMetrics.heightPixels
    private val gameH get() = root.gameHeightFor(screenW)
    private val gripH get() = gripView?.height ?: 0
    /** The grip's strip, kept free in either placement ([PortraitLayout.arrange]). */
    private val reserve get() = gripH + if (expand && underGame && padStrip > 0) minOf(padStrip, screenH / 3) else 0
    private fun arrangement() = PortraitLayout.arrange(screenW, screenH, gameH, gripH, ratio, underGame, expand, padStrip)

    // A rotation, the status bar coming or going, a Game Boy game's 10:9: lay out again.
    private val relayout = View.OnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
        val now = Triple(screenW, screenH, gameH)
        if (now != laidOutFor) root.post { apply() }
    }

    fun setEnabled(on: Boolean) {
        if (on == enabled) return
        enabled = on
        if (on) {
            ratio = storedRatio()
            underGame = prefs.portraitCompanionUnderGame
            hiddenPref = prefs.portraitCompanionHidden
            val at = root.indexOfChild(touchControls) + 1
            panel = CompanionColors.track(ComposeView(context)).apply {
                setContent {
                    CompositionLocalProvider(LocalCompanionTopInset provides topInset.intValue) { SidePanelCompanion(content = companion) }
                }
            }.also { root.addView(it, at, FrameLayout.LayoutParams(-1, 0, Gravity.TOP)) }
            gripView = DragFrame(context, vertical = true, ::onDragStart, ::onDrag, ::onDragEnd).apply {
                addView(CompanionColors.track(ComposeView(context)).apply {
                    setContent {
                        CompositionLocalProvider(LocalClickSound provides clickSound) { PortraitGrip(grip.value, ::stepSize) }
                    }
                })
            }.also {
                root.addView(it, at + 1, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.CENTER_HORIZONTAL))
            }
            closeTab = CompanionColors.track(ComposeView(context)).apply {
                setContent {
                    CompositionLocalProvider(LocalClickSound provides clickSound) { PortraitCloseTab(grip.value, ::hide) }
                }
            }.also { root.addView(it, at + 2, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.CENTER_HORIZONTAL)) }
            openTab = CompanionColors.track(ComposeView(context)).apply {
                setContent {
                    CompositionLocalProvider(LocalClickSound provides clickSound) { PortraitOpenTab(::show) }
                }
            }.also { root.addView(it, at + 3, FrameLayout.LayoutParams(-2, -2, Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL)) }
            // The X sits just right of the grip: placed again once either is measured.
            closeTab?.addOnLayoutChangeListener { v, l, _, r, _, ol, _, or, _ -> if (r - l != or - ol) v.post { apply() } }
            // The grip's own height decides where things go: place them again once it's measured.
            gripView?.addOnLayoutChangeListener { v, _, t, _, b, _, ot, _, ob -> if (b - t != ob - ot) v.post { apply() } }
            root.addOnLayoutChangeListener(relayout)
            root.topAligned = true
        } else {
            root.removeOnLayoutChangeListener(relayout)
            for (v in listOfNotNull(panel, gripView, closeTab, openTab)) { v.animate().cancel(); root.removeView(v) }
            panel = null
            gripView = null
            closeTab = null
            openTab = null
            root.topAligned = false
            root.centered = false
            setMargins(game, 0, 0)
            setMargins(touchControls, 0, 0)
        }
        apply()
    }

    /** Picks up SETTINGS' COMPANION (along the bottom / under the game) and the size from [prefs]. */
    fun refresh() {
        if (!enabled) return
        ratio = storedRatio()
        underGame = prefs.portraitCompanionUnderGame
        apply()
    }

    /** Where the companion is on the screen (px), for TOUCH CONTROLS' preview; null while off. */
    fun companionBounds(): androidx.compose.ui.geometry.Rect? {
        if (hidden) return null
        val v = panel ?: return null
        val lp = v.layoutParams as FrameLayout.LayoutParams
        return androidx.compose.ui.geometry.Rect(0f, lp.topMargin.toFloat(), screenW.toFloat(), (lp.topMargin + lp.height).toFloat())
    }

    /** A BACK tap; false = not ours (held sideways, or a second screen has the companion). */
    fun onBack(): Boolean {
        if (!enabled) return false
        if (hidden) show() else back.back()
        return true
    }

    /** Brings a put-away companion back up (the QUICK MENU's or a hotkey's COMPANION). */
    fun openPanel() = show()

    private var sliding = false

    /** The touch controls' strip (top, bottom px) the last time the companion was up: put away, they move only part
     * of the way down from there ([HIDDEN_PAD_DROP]) instead of to the screen's bottom. */
    private var shownPad: Pair<Int, Int>? = null

    /** The X: slides the companion (and its grip) down off the screen, then gives the game the whole screen. */
    private fun hide() {
        if (!enabled || hidden || ds) return
        hiddenPref = true
        prefs.portraitCompanionHidden = true
        onHiddenChanged(true)
        val views = listOfNotNull(panel, gripView, closeTab)
        val top = views.minOfOrNull { it.top } ?: 0
        val dy = (screenH - top).toFloat()
        sliding = true
        views.forEachIndexed { i, v ->
            v.animate().translationY(dy).setDuration(SLIDE_MS).setUpdateListener { punchThrough() }
                .withEndAction(if (i == 0) Runnable { sliding = false; apply() } else null).start()
        }
    }

    /** The bottom tab or BACK: lays the screen out for the companion again, then slides it up into its place. */
    private fun show() {
        if (!enabled || !hidden) return
        hiddenPref = false
        prefs.portraitCompanionHidden = false
        onHiddenChanged(false)
        sliding = false
        apply()
        val views = listOfNotNull(panel, gripView, closeTab)
        val lp = panel?.layoutParams as? FrameLayout.LayoutParams
        val top = minOf(lp?.topMargin ?: 0, (gripView?.layoutParams as? FrameLayout.LayoutParams)?.topMargin ?: Int.MAX_VALUE)
        val dy = (screenH - top).toFloat()
        for (v in views) {
            v.animate().cancel()
            v.translationY = dy
            v.animate().translationY(0f).setDuration(SLIDE_MS).setUpdateListener { punchThrough() }.withEndAction(null).start()
        }
    }

    /** The game is a SurfaceView: a moving view over it has to tell the window where its hole is (see [SidePanel]). */
    private fun punchThrough() {
        panel?.let { root.requestTransparentRegion(it) }
    }

    private fun apply() {
        if (!enabled) return
        val h = screenH
        if (hidden) {
            if (sliding) return
            laidOutFor = Triple(screenW, h, gameH)
            for (v in listOfNotNull(panel, gripView, closeTab)) { v.visibility = View.GONE; v.translationY = 0f }
            openTab?.visibility = View.VISIBLE
            root.centered = true
            setMargins(game, 0, 0)
            // The touch controls keep near where they were: their strip's bottom goes only part of the way to the
            // screen's bottom (their layout is anchored to it), not all the way.
            val (padTop, padBottom) = shownPad ?: arrangement().let { it.padTop to it.padBottom }
            val bottom = (padBottom + (h - padBottom) * HIDDEN_PAD_DROP).toInt().coerceIn(padBottom, h)
            setMargins(touchControls, padTop, h - bottom)
            return
        }
        for (v in listOfNotNull(panel, gripView)) v.visibility = View.VISIBLE
        closeTab?.visibility = if (ds) View.GONE else View.VISIBLE
        openTab?.visibility = View.GONE
        root.centered = false
        val a = arrangement()
        laidOutFor = Triple(screenW, h, gameH)
        grip.value = a.grip
        topInset.intValue = a.topInset(gripH)
        panel?.let { setFrame(it, a.companionTop, a.companionHeight) }
        gripView?.let { setMargins(it, a.gripTop, 0) }
        // The X right of the grip, on the same strip.
        closeTab?.let { x ->
            val lp = x.layoutParams as FrameLayout.LayoutParams
            val left = ((gripView?.width ?: 0) + x.width) / 2 + (CLOSE_GAP_DP * context.resources.displayMetrics.density).toInt()
            if (lp.topMargin != a.gripTop || lp.leftMargin != left) {
                lp.topMargin = a.gripTop
                lp.leftMargin = left
                x.layoutParams = lp
            }
        }
        setMargins(game, 0, h - a.gameAreaBottom)
        setMargins(touchControls, a.padTop, h - a.padBottom)
        shownPad = a.padTop to a.padBottom
    }

    private fun setFrame(v: View, top: Int, height: Int) {
        val lp = v.layoutParams as FrameLayout.LayoutParams
        if (lp.topMargin == top && lp.height == height) return
        lp.topMargin = top
        lp.height = height
        v.layoutParams = lp
    }

    private fun setMargins(v: View, top: Int, bottom: Int) {
        val lp = v.layoutParams as FrameLayout.LayoutParams
        if (lp.topMargin == top && lp.bottomMargin == bottom) return
        lp.topMargin = top
        lp.bottomMargin = bottom
        v.layoutParams = lp
    }

    private fun save() {
        if (ds) prefs.portraitDsRatio = ratio else prefs.portraitCompanionRatio = ratio
        apply()
    }

    private val currentRatio get() = arrangement().companionHeight.toFloat() / screenW

    private fun stepSize() {
        ratio = PortraitLayout.next(screenW, screenH, gameH, currentRatio, reserve)
        save()
    }

    // --- resizing by the grip --------------------------------------------------

    private var dragStartHeight = 0
    private var dragGrows = -1

    private fun onDragStart() {
        dragStartHeight = arrangement().companionHeight
        // Along the bottom the grip is on the companion's top edge: up = taller. Under the game it hangs below: down.
        dragGrows = if (underGame && !expand) 1 else -1
    }

    private fun onDrag(dy: Float) {
        ratio = ((dragStartHeight + dragGrows * dy) / screenW).coerceAtLeast(0f)
        apply()
    }

    private fun onDragEnd() {
        ratio = PortraitLayout.snap(screenW, screenH, gameH, currentRatio, reserve)
        save()
    }

    private companion object {
        const val SLIDE_MS = 160L
        /** Between the grip and its X. */
        const val CLOSE_GAP_DP = 8
        /** Put away, how far the touch controls' strip drops toward the screen's bottom: 0 = where it was, 1 = all the way. */
        const val HIDDEN_PAD_DROP = 0.5f
    }
}
