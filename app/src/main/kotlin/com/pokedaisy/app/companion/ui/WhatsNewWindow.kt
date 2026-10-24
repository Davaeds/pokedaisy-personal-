package com.pokedaisy.app.companion.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.unit.dp
import com.pokedaisy.app.companion.WhatsNew
import com.pokedaisy.app.companion.i18n.tr

/**
 * The Library's WHAT'S NEW, once after an update to a version with [WhatsNew] notes: the version in the title window,
 * then each section (NEW, FIXES) as a red title over its lines in the smaller text ([small]), each behind a square
 * bullet. The lines scroll on a small screen (the D-pad too), fading out over a down arrow (right, clear of the words) while there's more below;
 * RELEASE PAGE (the GitHub release, for everything else) and OK stay below them.
 */
@Composable
fun WhatsNewWindow(
    notes: WhatsNew.Notes,
    m: GbaTextMetrics,
    small: GbaTextMetrics,
    onReleasePage: () -> Unit,
    onClose: () -> Unit,
) {
    val u = m.u
    val bullet = small.lineHeight * 0.2f
    val scroll = rememberScrollState()
    OptionOverlay(onClose, Modifier.widthIn(max = 760.dp)) {
        Column {
            OptionTitleWindow("WHAT'S NEW", m, trailing = "v${notes.version}")
            Spacer(Modifier.height(u * 4))
            OptionListWindow(m, Modifier.fillMaxWidth()) {
                Column(Modifier.padding(u * 4)) {
                    Box(Modifier.weight(1f, fill = false)) {
                    Column(Modifier.dpadScroll(scroll, RectangleShape, small.lineHeight * 3).verticalScroll(scroll)) {
                        notes.sections.forEachIndexed { i, section ->
                            if (i > 0) Spacer(Modifier.height(u * 6))
                            GbaText(tr(section.title), OptionColors.value, OptionColors.valueShadow, m, modifier = Modifier.padding(horizontal = u * 4))
                            section.items.forEach { item ->
                                Spacer(Modifier.height(u * 3))
                                Row(Modifier.padding(horizontal = u * 4)) {
                                    // A square bullet, centred on the first line (the pixel font has no bullet glyph).
                                    Box(
                                        Modifier.padding(top = (small.lineHeight - bullet) / 2).size(bullet)
                                            .background(OptionColors.label),
                                    )
                                    Spacer(Modifier.width(u * 5))
                                    GbaText(item, OptionColors.label, OptionColors.labelShadow, small, maxLines = Int.MAX_VALUE, modifier = Modifier.weight(1f))
                                }
                            }
                        }
                    }
                    if (scroll.canScrollForward) {
                        Box(
                            Modifier.align(Alignment.BottomCenter).fillMaxWidth().height(small.lineHeight * 1.5f)
                                .background(Brush.verticalGradient(listOf(Color.Transparent, optionLook.listFill))),
                            contentAlignment = Alignment.BottomEnd,
                        ) { ShadowedPixelIcon(PixelIcons.arrowDown, OptionColors.value, OptionColors.valueShadow, m, Modifier.padding(end = u * 4)) }
                    }
                    }
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(u * 4),
                        modifier = Modifier.fillMaxWidth().padding(top = u * 8),
                    ) {
                        OptionButton(tr("RELEASE PAGE"), m, onClick = onReleasePage, modifier = Modifier.weight(1f).height(m.rowHeight * 1.4f))
                        OptionButton(tr("OK"), m, onClick = onClose, emphasis = true, modifier = Modifier.weight(1f).height(m.rowHeight * 1.4f))
                    }
                }
            }
        }
    }
}
