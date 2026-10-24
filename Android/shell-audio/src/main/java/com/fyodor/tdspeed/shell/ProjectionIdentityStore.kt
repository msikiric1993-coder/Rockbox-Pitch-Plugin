package com.fyodor.tdspeed.shell

import android.content.Context
import android.os.Build
import android.util.AtomicFile
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** Shell-private registry; only identities from this boot/package/UID pair can be reused. */
internal class ProjectionIdentityStore(context: Context, ownerUid: Int, projectionUid: Int, testAddress: String? = null) {
    private val boot = runCatching { File("/proc/sys/kernel/random/boot_id").readText().trim()
        .also { require(Regex("[0-9a-fA-F-]{36}").matches(it)) } }.getOrNull()
    private val scope = "${context.packageName}:$ownerUid:$projectionUid:${Build.FINGERPRINT}"
    private val file = AtomicFile(File(path(ownerUid, projectionUid, testAddress)))
    fun load(): Set<String> = runCatching {
        if (boot == null) return emptySet()
        val bytes = file.openRead().use { it.readNBytes(32 * 1024 + 1) }
        require(bytes.size <= 32 * 1024)
        val json = JSONObject(String(bytes, Charsets.UTF_8))
        if (json.getString("boot") != boot || json.getString("scope") != scope) return emptySet()
        val addresses = json.getJSONArray("addresses")
        require(addresses.length() <= 64)
        (0 until addresses.length()).map { addresses.getString(it).also { address -> require(address.length <= 256) } }.toSet()
    }.getOrDefault(emptySet())
    private var lastSaved: Set<String>? = null
    fun save(addresses: Set<String>) {
        if (boot == null || addresses == lastSaved) return
        require(addresses.size <= 64)
        val data = JSONObject().put("boot", boot).put("scope", scope).put("addresses", JSONArray(addresses.sorted())).toString().toByteArray()
        val stream = file.startWrite()
        try {
            android.system.Os.fchmod(stream.fd, 384) // 0600, shell UID only.
            stream.write(data); file.finishWrite(stream); lastSaved = addresses.toSet()
        } catch (error: Throwable) { file.failWrite(stream); throw error }
    }
    companion object {
        fun path(ownerUid: Int, projectionUid: Int, testAddress: String? = null) =
            "/data/local/tmp/tdspeed-projection-$ownerUid-$projectionUid" +
                (testAddress?.let { "-test-${it.hashCode()}" } ?: "") + ".json"
    }
}
