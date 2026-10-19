#pragma once
#include <winrecomp/win64_abi.hpp>
#include "guest_api.hpp"
#ifdef _WIN32
namespace wr::port {
inline U32 boolean(Process& p,int result){if(!result)p.set_error(::GetLastError());return U32(result);}
}
#endif
