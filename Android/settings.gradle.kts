pluginManagement {
    repositories { google(); mavenCentral(); gradlePluginPortal() }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories { google(); mavenCentral() }
}
rootProject.name = "TDSpeedAndroid"
include(":app", ":protocol", ":shell-audio", ":dsp-native", ":hidden-api-stubs", ":test-source")
