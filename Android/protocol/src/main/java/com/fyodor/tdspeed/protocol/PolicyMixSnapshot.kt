package com.fyodor.tdspeed.protocol

data class PolicyMixSnapshot(val address: String, val flags: String, val device: String,
    val type: String, val rules: List<String>) {
    val signature get() = rules.sorted().joinToString(",")
    fun varispeedInputFor(ownerUid: Int) = flags == "MIX_ROUTE_FLAG_LOOP_BACK" &&
        device == "AUDIO_DEVICE_OUT_REMOTE_SUBMIX" && type == "MIX_TYPE_PLAYERS" && rules.size == 4 &&
        rules.toSet() == setOf("RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_MEDIA", "RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_UNKNOWN",
            "RULE_EXCLUDE_UID 2000", "RULE_EXCLUDE_UID $ownerUid")
    val projectionMedia get() = flags == "MIX_ROUTE_FLAG_LOOP_BACK" &&
        device == "AUDIO_DEVICE_OUT_REMOTE_SUBMIX" && type == "MIX_TYPE_PLAYERS" &&
        rules.toSet() == setOf("RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_MEDIA", "RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_GAME") && rules.size == 2
    companion object {
        fun parse(dump: String): List<PolicyMixSnapshot> {
            val headers = Regex("(?m)^\\s*Audio Policy Mix \\d+ \\([^\\n]*\\):[^\\n]*").findAll(dump).toList()
            return headers.mapIndexedNotNull { i, header ->
                val block = dump.substring(header.range.last + 1, headers.getOrNull(i + 1)?.range?.first ?: dump.length)
                fun value(label: String) = Regex("(?m)^\\s*- $label: ([^\\n]*)").find(block)?.groupValues?.get(1)?.trim().orEmpty()
                val address = value("device address")
                if (address.isEmpty()) null else PolicyMixSnapshot(address, value("Route Flags"), value("device type"), value("mix type"),
                    Regex("(?m)^\\s*- Criterion \\d+: ([^\\n]+)").findAll(block).map { it.groupValues[1].trim() }.toList())
            }
        }
        fun expected(uids: Set<Int>) = (listOf("RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_MEDIA", "RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_GAME") +
            uids.map { "RULE_EXCLUDE_UID $it" }).sorted().joinToString(",")
    }
}
