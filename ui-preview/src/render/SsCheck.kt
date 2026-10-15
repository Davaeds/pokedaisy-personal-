package render

import com.pokedaisy.app.GameMedia
import com.pokedaisy.app.GameTitles
import com.pokedaisy.app.ScreenScraperClient
import java.io.File

/**
 * `gradle sscheck`: asks ScreenScraper for every game the app supports - the same calls the app makes
 * (`GameMedia.find` / `save`), by each title's known SHA-1s (GameTitles.BY_SHA1), then its name, then
 * its base game - and writes what came back to build/sscheck/: report.md (how each game matched and
 * which media it has) and media/<title>/, which `gradle render -PssMedia=build/sscheck/media` puts on the
 * launcher shots. No ROMs needed.
 *
 * Credentials come from the environment only, never the repo: SS_USER / SS_PASSWORD (a screenscraper.fr
 * account) and SS_DEV_ID / SS_DEV_PASSWORD (the app's developer login). `-Ponly=unbound,emerald` checks
 * some titles; `-Pvideos=true` downloads videos too.
 */
fun main(args: Array<String>) {
    val out = File(args.getOrElse(0) { "build/sscheck" }).apply { mkdirs() }
    val only = args.getOrElse(1) { "" }.split(',').map { it.trim().lowercase() }.filter { it.isNotEmpty() }
    val videos = System.getProperty("videos") == "true"
    fun env(name: String) = System.getenv(name).orEmpty()
    val user = env("SS_USER")
    val password = env("SS_PASSWORD")
    if (user.isEmpty() || password.isEmpty()) {
        System.err.println("Set SS_USER and SS_PASSWORD (a screenscraper.fr account), and SS_DEV_ID / SS_DEV_PASSWORD (the app's developer login).")
        kotlin.system.exitProcess(2)
    }
    ScreenScraperClient.developer = env("SS_DEV_ID") to env("SS_DEV_PASSWORD")
    val account = ScreenScraperClient.Account(user, password)

    try {
        val me = ScreenScraperClient.userInfo(account)
        println("Signed in as ${me.name}: ${me.requestsToday} of ${me.maxRequestsPerDay} requests used today, ${me.maxThreads} thread(s).")
    } catch (e: Exception) {
        System.err.println("Sign-in failed: ${e.message}")
        kotlin.system.exitProcess(1)
    }

    // Every supported title, with its hashes (several revisions / languages can share one).
    val titles = GameTitles.BY_SHA1.entries.groupBy({ it.value }, { it.key })
        .filterKeys { t -> only.isEmpty() || only.any { it in t.lowercase() } }
        .toSortedMap()
    val rows = mutableListOf<String>()
    val media = File(out, "media")
    for ((title, sha1s) in titles) {
        print("$title ... ")
        val result = try {
            val gameBoy = title in GAME_BOY
            val systems = if (gameBoy) listOf(ScreenScraperClient.SYSTEM_GB, ScreenScraperClient.SYSTEM_GBC) else listOf(ScreenScraperClient.SYSTEM_GBA)
            // Each known dump's hash first, then (once) the name and base game.
            var match: GameMedia.Match? = null
            for (sha1 in sha1s) {
                val hashes = ScreenScraperClient.RomHashes("", "", sha1.uppercase(), 0)
                match = GameMedia.find(account, hashes, systems, "", "", null)
                if (match != null) break
            }
            match ?: GameMedia.find(account, ScreenScraperClient.RomHashes("", "", "", 0), systems, "", title, baseOf(title))
        } catch (e: ScreenScraperClient.ApiException) {
            println("stopped: ${e.message}")
            rows += "| $title | ERROR: ${e.message} | | |"
            if (e.fatal) break else continue
        }
        if (result == null) {
            println("not found")
            rows += "| $title | NOT FOUND | | |"
            continue
        }
        val dir = File(media, title)
        val failed = GameMedia.save(dir, result.game, null, videos, replace = false)
        val have = GameMedia.Kind.entries.filter { File(dir, it.file).isFile }.joinToString(" ") { it.name.lowercase() }
        val info = GameMedia.info(media, File(title))
        val facts = listOfNotNull(
            info?.released?.let { "released" }, info?.developer?.let { "developer" }, info?.publisher?.let { "publisher" },
            info?.genre?.let { "genre" }, info?.players?.let { "players" }, info?.rating?.let { "rating" },
            info?.synopsis?.let { "story" },
        ).joinToString(" ")
        val name = result.game.names["ss"] ?: result.game.names.values.firstOrNull().orEmpty()
        println("${result.by} -> $name ($have)${if (failed > 0) ", $failed download(s) failed" else ""}")
        rows += "| $title | ${result.by} -> $name (id ${result.game.id}) | $have | $facts |"
    }
    File(out, "report.md").writeText(
        "# ScreenScraper coverage\n\n| Game | Matched | Media | Facts |\n| --- | --- | --- | --- |\n" + rows.joinToString("\n") + "\n",
    )
    println("wrote ${File(out, "report.md").absolutePath}")
}

private val GAME_BOY = setOf(
    "Pokémon Red", "Pokémon Blue", "Pokémon Yellow", "Pokémon Gold", "Pokémon Silver", "Pokémon Crystal", "Pokémon Crystal Clear",
    "Pokémon Prism",
)

/** Hacks without their own entry fall back to their base game's media, like the app (by the ROM's header there). */
private val EMERALD_HACKS = listOf(
    "Emerald", "Heart & Soul", "Lazarus", "Quetzal", "R.O.W.E.", "SoulGold", "Too Many Types", "Glazed", "Mariomon",
    "Smaragd", "Émeraude", "Esmeralda", "Smeraldo", "エメラルド",
)

private fun baseOf(title: String): String? = when {
    title in GAME_BOY -> null
    EMERALD_HACKS.any { it in title } -> GameMedia.baseGameTitle("BPEE")
    "Ruby" in title || "Rubin" in title || "Rubis" in title || "Rubí" in title || "Rubino" in title -> GameMedia.baseGameTitle("AXVE")
    "Sapphire" in title || "Saphir" in title || "Zafiro" in title || "Zaffiro" in title -> GameMedia.baseGameTitle("AXPE")
    "LeafGreen" in title || "Blattgrün" in title || "Vert Feuille" in title || "Verde Hoja" in title || "Verde Foglia" in title -> GameMedia.baseGameTitle("BPGE")
    else -> GameMedia.baseGameTitle("BPRE")
}
