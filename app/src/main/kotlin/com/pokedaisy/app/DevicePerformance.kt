package com.pokedaisy.app

import android.app.ActivityManager
import android.content.Context
import android.util.Log
import java.io.File

/**
 * Whether this device looks too weak for the full companion (LIGHT COMPANION's AUTO, [com.pokedaisy.app.companion.LightCompanion]):
 * Android's own low-RAM flag, under [MIN_RAM_BYTES] of memory, or no CPU core faster than [MIN_TOP_KHZ]. The handhelds
 * this is about (the RG DS's RK3568: four Cortex-A55 at up to 2.0 GHz) have only little cores; the Thor's and the
 * Retroid Pocket 5 / 6's big cores run at 2.8 GHz and more. A core speed that can't be read counts as fast.
 */
object DevicePerformance {
    private const val MIN_TOP_KHZ = 2_300_000L
    private const val MIN_RAM_BYTES = 3L shl 30

    @Volatile private var cached: Boolean? = null

    fun looksWeak(context: Context): Boolean = cached ?: check(context).also { cached = it }

    private fun check(context: Context): Boolean {
        val am = context.getSystemService(ActivityManager::class.java)
        val mem = ActivityManager.MemoryInfo().also { am?.getMemoryInfo(it) }
        val topKhz = topCoreKhz()
        val lowRam = am?.isLowRamDevice == true
        val weak = lowRam || (mem.totalMem in 1 until MIN_RAM_BYTES) || (topKhz in 1 until MIN_TOP_KHZ)
        Log.i("pokedaisy/perf", "device: topCore=${topKhz / 1000} MHz ram=${mem.totalMem shr 20} MB lowRam=$lowRam -> ${if (weak) "weak" else "ok"}")
        return weak
    }

    /** The fastest core's top speed (kHz) from cpufreq, 0 when it can't be read. */
    private fun topCoreKhz(): Long = runCatching {
        File("/sys/devices/system/cpu").listFiles { f -> f.name.matches(Regex("cpu\\d+")) }.orEmpty()
            .maxOfOrNull { File(it, "cpufreq/cpuinfo_max_freq").takeIf(File::canRead)?.readText()?.trim()?.toLongOrNull() ?: 0L } ?: 0L
    }.getOrDefault(0L)
}
