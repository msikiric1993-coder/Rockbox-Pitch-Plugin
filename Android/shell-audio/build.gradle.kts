plugins { id("com.android.library"); id("org.jetbrains.kotlin.android") }
android {
    namespace = "com.fyodor.tdspeed.shell"
    compileSdk = 36
    defaultConfig { minSdk = 33 }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
}
dependencies {
    implementation(project(":protocol"))
    implementation(project(":dsp-native"))
    compileOnly(project(":hidden-api-stubs"))
    implementation("androidx.annotation:annotation:1.9.1")
}
