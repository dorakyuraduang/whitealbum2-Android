#pragma once
#include <jni.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wa2::android {
// Called by the library's single JNI_OnLoad/OnUnload pair, where the app class
// loader is available. Game-thread JNI calls reuse these cached references.
bool initialize_resource_jni(JNIEnv* env) noexcept;
void release_resource_jni(JNIEnv* env) noexcept;
jclass resource_directory_class()noexcept;
struct ArchiveMetadata {std::string name;std::uint64_t size{},modified_millis{};};
std::vector<ArchiveMetadata> resource_metadata();
std::vector<ArchiveMetadata> legacy_save_metadata();
int open_resource_descriptor(std::string_view filename,bool optional=false);
}
