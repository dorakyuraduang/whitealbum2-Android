#pragma once
#include <cstdint>
namespace wa2::aot {
struct TextJumpCounts {std::uint64_t entries{},local_jumps{},budget_yields{},external_targets{},fresh_entries{},resumed_entries{},pc24{},pc53{},pc64{},pc_reserved{};};
inline thread_local TextJumpCounts text_jump_counts;
// Count locally while the source function runs. One TLS access on return,
// rather than reentering the dispatcher/TLS machinery for every local JMP.
template<bool Metered=false> class TextJumpBudget {
    unsigned remaining_{128},local_{},yields_{},external_{};
    bool resumed_{};
    std::uint16_t precision_{};
public:
    static constexpr unsigned MaximumJumps=128;
    bool take(){if(!remaining_){++yields_;return false;}--remaining_;++local_;return true;}
    void external(){++external_;}
    ~TextJumpBudget(){if constexpr(!Metered){auto& c=text_jump_counts;++c.entries;c.local_jumps+=local_;c.budget_yields+=yields_;c.external_targets+=external_;++(resumed_?c.resumed_entries:c.fresh_entries);
        switch(precision_){case 0:++c.pc24;break;case 0x200:++c.pc53;break;case 0x300:++c.pc64;break;default:++c.pc_reserved;break;}}}
    explicit TextJumpBudget(std::uint32_t resume=0,std::uint16_t control=0):resumed_(resume!=0),precision_(control&0x300u){}
    TextJumpBudget(const TextJumpBudget&)=delete;
};
}
