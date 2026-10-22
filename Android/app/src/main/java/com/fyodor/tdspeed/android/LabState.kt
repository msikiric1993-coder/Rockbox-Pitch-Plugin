package com.fyodor.tdspeed.android

import android.os.Bundle
import kotlinx.coroutines.flow.MutableStateFlow

data class LabState(val status: String = "STOPPED", val message: String = "Connect Shizuku to inspect the route.", val data: Bundle = Bundle())
object LabModel { val state = MutableStateFlow(LabState()) }
