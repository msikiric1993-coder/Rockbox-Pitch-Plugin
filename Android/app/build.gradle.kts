plugins { id("com.android.application"); id("org.jetbrains.kotlin.android"); id("org.jetbrains.kotlin.plugin.compose") }
android {
    namespace = "com.fyodor.tdspeed.android"
    compileSdk = 36
    defaultConfig {
        applicationId = "com.fyodor.tdspeed.android"
        minSdk = 33; targetSdk = 36; versionCode = 6; versionName = "0.5.1-varispeed-ui"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
    }
    buildTypes { debug { applicationIdSuffix = ".debug" }; release { isMinifyEnabled = false } }
    buildFeatures { compose = true; buildConfig = true }
    packaging { jniLibs { useLegacyPackaging = true } }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
}
dependencies {
    implementation(project(":protocol")); implementation(project(":shell-audio")); implementation(project(":dsp-native"))
    implementation("dev.rikka.shizuku:api:13.1.5"); implementation("dev.rikka.shizuku:provider:13.1.5")
    implementation(platform("androidx.compose:compose-bom:2025.10.00"))
    implementation("androidx.activity:activity-compose:1.11.0")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.9.4")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
