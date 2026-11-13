#pragma once
#include <cstdint>
namespace wa2::aot {
struct TextJumpCounts {std::uint64_t entries{},local_jumps{},budget_yields{},external_targets{};};
inline thread_local TextJumpCounts text_jump_counts;
// Count locally while the source function runs. One TLS access on return,
// rather than reentering the dispatcher/TLS machinery for every local JMP.
template<bool Metered=false> class TextJumpBudget {
    unsigned remaining_{128},local_{},yields_{},external_{};
public:
    static constexpr unsigned MaximumJumps=128;
    bool take(){if(!remaining_){++yields_;return false;}--remaining_;++local_;return true;}
    void external(){++external_;}
    ~TextJumpBudget(){if constexpr(!Metered){auto& c=text_jump_counts;++c.entries;c.local_jumps+=local_;c.budget_yields+=yields_;c.external_targets+=external_;}}
    TextJumpBudget()=default;
    TextJumpBudget(const TextJumpBudget&)=delete;
};
}
