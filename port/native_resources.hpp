#pragma once
#include <cstdint>
#include <functional>
#include <string>
namespace wr {class Process;class Memory;
struct NativeResourceProfile {
    std::string sha256="246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327";
    std::uint32_t decoder=0x437000,image_check=0x437220,image_load=0x437310;
    std::uint32_t directory_lookup=0x4588a0,resource_load=0x458050,resource_decode=0x457010;
    std::uint32_t dictionary=0xa7cdd0,trace_set=0x4a40ec,trace_clear=0x4a40f0;
    std::string report_key="native_resources";
    std::function<bool(Memory&,std::uint32_t)> verify;
};
// The reference regression keeps the original image-directory functions.
void install_wa2_native_resources(Process&,bool image_directory_functions=true);
void install_wa2_native_resources(Process&,NativeResourceProfile,bool image_directory_functions=true);
}
