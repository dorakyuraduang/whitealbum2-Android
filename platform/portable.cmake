# Shared by the game build and the independent portable-module build.
get_filename_component(WA2_PLATFORM_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
add_library(wa2_platform STATIC
    "${WA2_PLATFORM_DIRECTORY}/file_io.cpp"
    "${WA2_PLATFORM_DIRECTORY}/resource_fd.cpp"
    "${WA2_PLATFORM_DIRECTORY}/clock.cpp"
    "${WA2_PLATFORM_DIRECTORY}/timer_resolution.cpp")
target_include_directories(wa2_platform PUBLIC "${WA2_PLATFORM_DIRECTORY}")
target_link_libraries(wa2_platform PUBLIC wa2_engine)
if(WIN32)
    target_link_libraries(wa2_platform PRIVATE winmm)
endif()
add_executable(wa2_resource_tests "${WA2_PLATFORM_DIRECTORY}/../tests/resources.cpp")
target_link_libraries(wa2_resource_tests PRIVATE wa2_platform)
# Only the test's SHA-256 implementation is used; no runtime library is linked.
target_include_directories(wa2_resource_tests PRIVATE
    "${WA2_PLATFORM_DIRECTORY}/../tools/WindowsRecompTool-main/include")
