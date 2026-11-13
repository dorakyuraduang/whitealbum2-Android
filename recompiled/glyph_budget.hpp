#pragma once
#include <cstdint>
namespace wr {class Memory;}
namespace wa2::aot {
// A typed glyph submits no guest callback or input/wait operation. Give these
// bounded packets their own allowance instead of exhausting the 32 shared
// indirect/leaf calls midway through an otherwise unchanged text run.
struct GlyphBudgetCounts {std::uint64_t exhaustions{};};
inline thread_local GlyphBudgetCounts glyph_budget_counts;
inline thread_local std::uint64_t glyph_budget_deferrals{};
class GlyphCallScope {
    inline static thread_local GlyphCallScope* current_{};
    GlyphCallScope* previous_{current_};
    wr::Memory* memory_;
    unsigned remaining_;
public:
    static constexpr unsigned MaximumCalls=256;
    explicit GlyphCallScope(wr::Memory& memory,unsigned limit=MaximumCalls)
        :memory_(&memory),remaining_(limit>MaximumCalls?MaximumCalls:limit){current_=this;}
    ~GlyphCallScope(){current_=previous_;}
    GlyphCallScope(const GlyphCallScope&)=delete;
    static unsigned* remaining(wr::Memory& memory){
        return current_ && current_->memory_==&memory?&current_->remaining_:nullptr;
    }
};
}
