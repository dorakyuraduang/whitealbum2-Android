#pragma once
#include <winrecomp/compiled.hpp>
#include <string>
namespace wa2::special {
bool try_lzss_prefix(wr::Cpu&,wr::Memory&);
std::string lzss_report();
std::uint64_t lzss_activity();
// Memory fixtures can compare the unchanged static loop with the admission.
inline thread_local bool lzss_acceleration_enabled=true;
struct LzssReferenceScope {
    bool previous{lzss_acceleration_enabled};
    LzssReferenceScope(){lzss_acceleration_enabled=false;}
    ~LzssReferenceScope(){lzss_acceleration_enabled=previous;}
};
}
