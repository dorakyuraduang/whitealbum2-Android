if(NOT TARGET wa2_game_scene)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../game" native_scene EXCLUDE_FROM_ALL)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../platform/native_services.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/entry.cmake")
# This incomplete migration library is separate from the retained AOT release.
# A complete native APK is not selected by android/CMakeLists.txt yet.
wa2_add_native_android_entry(wa2_native_android)
