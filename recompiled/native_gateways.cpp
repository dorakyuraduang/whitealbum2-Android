#include <winrecomp/memory.hpp>
#include "../generated/static-data/static_data.hpp"
// The existing ABI adapters ask this guard before entering an ordinary C++
// resource/codec function. No legacy CPU backend is linked to provide it.
namespace wr {
bool wa2_native_code_verified(Memory& memory,std::uint32_t entry){
    const auto contracts=wa2::aot::native_code_contracts();
    const auto found=std::lower_bound(contracts.begin(),contracts.end(),entry,[](const auto& c,auto pc){return c.entry<pc;});
    if(found==contracts.end() || found->entry!=entry)return false;
    auto scope=memory.execution_scope();
    const auto proofs=memory.native_code_proofs(contracts.data(),contracts.size(),[&](std::span<std::uint8_t> result){
        for(unsigned i=0;i<contracts.size();++i){
            const auto& c=contracts[i];bool immutable=true;
            try{
                for(std::uint64_t at=c.entry;at<std::uint64_t(c.entry)+c.size;at=(at/4096+1)*4096)
                    if(memory.permissions(std::uint32_t(at))&Memory::Write){immutable=false;break;}
                result[i]=immutable && memory.matches_code(c.entry,{wa2::aot::static_code_bytes+c.entry-wa2::aot::static_code_base,c.size});
            }catch(const GuestFault&){result[i]=0;}
        }
    });
    return proofs[std::size_t(found-contracts.begin())]!=0;
}
}
