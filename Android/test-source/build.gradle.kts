plugins { id("com.android.application"); id("org.jetbrains.kotlin.android") }
android {
    namespace = "com.fyodor.tdspeed.testsource"
    compileSdk = 36
    defaultConfig { applicationId = "com.fyodor.tdspeed.testsource"; minSdk = 33; targetSdk = 36; versionCode = 1; versionName = "0.1.0-p0" }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
}
