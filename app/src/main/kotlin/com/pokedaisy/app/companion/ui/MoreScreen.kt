package com.pokedaisy.app.companion.ui

import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import com.pokedaisy.app.companion.i18n.tk
import com.pokedaisy.app.companion.ui.theme.gbaFocusRing
import com.pokedaisy.app.companion.i18n.tr

/** Tiles a row on the MORE grid: every tab can be off the bar (8), so two rows at most. */
private const val MORE_COLUMNS = 4

/**
 * The MORE tab (left of the gear, at the bar's end): every tab this game has that isn't on the tab bar ([tabs], ids), as app
 * icons on a grid ([TabIcons]) - a tap opens it ([onOpen]). Under the grid, a line saying the bar's tabs are
 * the player's pick and a TAB BAR button to SETTINGS' page for it ([onConfigure]). A tab [usable] says no to
 * (an unsupported ROM: only ACHIEVEMENTS works there) is greyed out.
 */
@Composable
internal fun MoreScreen(
    tabs: List<String>,
    onOpen: (String) -> Unit,
    onConfigure: () -> Unit,
    modifier: Modifier = Modifier,
    usable: (String) -> Boolean = { true },
) {
    val m = rememberGbaTextMetrics()
    val small = rememberGbaTextMetrics(1f)
    Column(modifier.fillMaxSize()) {
        OptionTitleWindow(tk("MORE"), m)
        Spacer(Modifier.height(m.u * 4))
        OptionListWindow(m, Modifier.fillMaxWidth().weight(1f)) {
            // The same room on every side of the grid (the window pads its top and sides alike).
            Column(Modifier.fillMaxSize().padding(m.u * 4)) {
                Column(
                    Modifier.fillMaxWidth().weight(1f).verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(m.u * 6),
                ) {
                    tabs.chunked(MORE_COLUMNS).forEachIndexed { row, line ->
                        Row(
                            Modifier.fillMaxWidth().height(IntrinsicSize.Min),
                            horizontalArrangement = Arrangement.spacedBy(m.u * 6),
                        ) {
                            line.forEachIndexed { col, id ->
                                AppTile(
                                    id, m, enabled = usable(id),
                                    modifier = Modifier.weight(1f).fillMaxHeight().focusStart(row == 0 && col == 0),
                                ) { onOpen(id) }
                            }
                            repeat(MORE_COLUMNS - line.size) { Spacer(Modifier.weight(1f)) }
                        }
                    }
                }
                Separator(m, Modifier.padding(vertical = m.u * 4))
                // The line over its button: the button's label (TAB BAR's own name) runs long in some languages.
                Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.End) {
                    GbaText(
                        tr("Tabs that aren't on the tab bar are here. Pick the bar's tabs in TAB BAR."),
                        OptionColors.label, OptionColors.labelShadow, small, maxLines = 3, modifier = Modifier.fillMaxWidth(),
                    )
                    Spacer(Modifier.height(m.u * 4))
                    OptionButton(tk("TAB BAR"), m, onClick = onConfigure, pixelIcon = PixelIcons.gear, modifier = Modifier.testTag("more-tab-bar"))
                }
            }
        }
    }
}

/** One app on the MORE grid: its coloured icon over its name, in a tab chip's frame. */
@Composable
private fun AppTile(id: String, m: GbaTextMetrics, enabled: Boolean, modifier: Modifier = Modifier, onClick: () -> Unit) {
    val u = m.u
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val label = companionTabLabel(id)
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
        modifier = modifier
            .alpha(if (enabled) 1f else 0.4f)
            .gbaFocusRing(focused, PixelRoundedShape(u * 3))
            .drawBehind {
                val px = u.toPx()
                drawLayeredBox(OptionColors.chipLayers.inPx(px), if (focused) OptionColors.tabSelectedFill else OptionColors.tabIdleFill, radius = 3 * px)
            }
            .soundClickable(interactionSource = interaction, indication = null, enabled = enabled, onClick = onClick)
            .semantics { contentDescription = label }
            .testTag("more-$id")
            .padding(horizontal = u * 2, vertical = u * 6),
    ) {
        TabIcons.forTab(id)?.let { icon ->
            // Three GBA pixels an icon pixel: 16 wide = 48 GBA pixels.
            PixelArt(icon.bitmap, icon.palette, Modifier.size(u * 48))
        }
        Spacer(Modifier.height(u * 4))
        Box(contentAlignment = Alignment.Center) {
            GbaText(label, OptionColors.label, OptionColors.labelShadow, m, bold = gameBoldLabels())
        }
    }
}
