plugins { id("com.android.application"); id("org.jetbrains.kotlin.android"); id("org.jetbrains.kotlin.plugin.compose") }
android {
    namespace = "com.fyodor.tdspeed.android"
    compileSdk = 36
    defaultConfig {
        applicationId = "com.fyodor.tdspeed.android"
        minSdk = 33; targetSdk = 36; versionCode = 9; versionName = "0.6.0-car-lab"
        manifestPlaceholders["appLabel"] = "Varispeed"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
    }
    buildTypes {
        debug { applicationIdSuffix = ".debug"; buildConfigField("boolean", "CAR_LAB", "false") }
        release { isMinifyEnabled = false; buildConfigField("boolean", "CAR_LAB", "false") }
        create("carLab") {
            initWith(getByName("debug"))
            applicationIdSuffix = ".carlab"
            matchingFallbacks += listOf("debug")
            manifestPlaceholders["appLabel"] = "Varispeed Car Lab"
            buildConfigField("boolean", "CAR_LAB", "true")
        }
    }
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
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
    androidTestImplementation(platform("androidx.compose:compose-bom:2025.10.00"))
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.7.0")
}
