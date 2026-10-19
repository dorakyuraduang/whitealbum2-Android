plugins { id("com.android.application") }
android {
    namespace = "org.wa2.port"
    compileSdk = 35
    ndkVersion = "27.0.12077973"
    defaultConfig {
        applicationId = "org.wa2.port"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.0.1-dev"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared", "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON", "-DCMAKE_JOB_POOLS=compile=1", "-DCMAKE_JOB_POOL_COMPILE=compile")
                targets += "wa2_android"
            }
        }
    }
    sourceSets["main"].java.srcDir("../../tools/SDL-release-2.32.10/android-project/app/src/main/java")
    externalNativeBuild { cmake { path = file("../CMakeLists.txt"); version = "3.22.1" } }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildTypes { getByName("release") { isMinifyEnabled = false } }
}
