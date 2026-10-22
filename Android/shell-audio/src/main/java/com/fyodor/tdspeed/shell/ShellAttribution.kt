package com.fyodor.tdspeed.shell

import android.content.AttributionSource
import android.content.Context
import android.content.ContextWrapper
import android.os.Process

/** Shell-only: AudioRecord must be attributed to the UID actually executing this code. */
internal object ShellAttribution {
    fun install(context: Context) {
        check(Process.myUid() == 2000) { "WRONG_BACKEND_UID: expected ADB shell" }
        val attribution = AttributionSource.Builder(Process.myUid()).setPackageName("com.android.shell").build()
        val application = Class.forName("android.app.ActivityThread")
            .getMethod("currentApplication").invoke(null) as? Context
        for (candidate in listOfNotNull(context, application)) {
            var base = candidate
            while (base is ContextWrapper && base.baseContext !== base) base = base.baseContext
            var type: Class<*>? = base.javaClass
            var installed = false
            while (type != null) {
                val field = runCatching { type!!.getDeclaredField("mAttributionSource") }.getOrNull()
                if (field != null) {
                    field.isAccessible = true; field.set(base, attribution); installed = true; break
                }
                type = type.superclass
            }
            check(installed) { "ATTRIBUTION_FAILED: framework context layout unsupported" }
        }
    }
}
