#pragma once
#include <winrecomp/integer.hpp>

namespace wa2::aot {
// One statically selected MOVS/STOS operation. Admission checks the complete
// locked range before writing anything. Unsupported shapes retain the checked
// operation, including its exact partially completed fault prefix.
bool try_bulk_string(wr::Cpu&,wr::Memory&,unsigned kind,unsigned width,
                     unsigned address_width,unsigned repeat,unsigned segment);
bool native_vector_clear(wr::Cpu&,wr::Memory&);
}

namespace wr {
template<bool Metered=true>
inline void aot_string_op(Cpu& cpu,Memory& memory,unsigned kind,unsigned width,
                          unsigned address_width,unsigned repeat,unsigned segment,
                          std::uint64_t& budget){
    if constexpr(!Metered){
        if(wa2::aot::try_bulk_string(cpu,memory,kind,width,address_width,repeat,segment))return;
    }
    string_op<Metered>(cpu,memory,kind,width,address_width,repeat,segment,budget);
}
}
