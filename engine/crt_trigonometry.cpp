#include "scalar_math.hpp"
#include "trigonometry_tables.hpp"
#include <bit>

#pragma STDC FENV_ACCESS ON
namespace wa2::math {
bool exact_trigonometry_input(double value){
    const auto magnitude=std::bit_cast<std::uint64_t>(value)&0x7fffffffffffffffull;
    // This is a closed subset of the cached ordinary SSE branch. Preserve
    // its separately compiled small/large argument handlers outside it.
    return !magnitude || (magnitude>=0x3030000000000000ull && magnitude<=0x4060000000000000ull);
}
std::span<const std::uint64_t> trigonometry_source_words(){return detail::crt_trigonometry_words;}

double exact_trigonometry(double value,bool cosine){
    if(value==0)return cosine?1.:value;
    const auto k=[](unsigned n){return std::bit_cast<double>(detail::crt_trigonometry_words[n]);};
    struct Pair {double lo{},hi{};
        void mul(Pair other){lo*=other.lo;hi*=other.hi;}
        void add(Pair other){lo+=other.lo;hi+=other.hi;}
        void sub(Pair other){lo-=other.lo;hi-=other.hi;}
    };
    // Native binary64 reconstruction of the cached 0x47cc05/0x47e085
    // ordinary paths, in source operation order. No FMA or fast-math.
    // Under |value|<=128, add/sub of the 1.5*2^52 bias is the same
    // nearest-even integer as CVTSD2SI (the quotient is below 1305).
    Pair a{value},b{k(264)*value},c{k(265)},d{k(268)},e,f{k(262),k(263)},g,h;
    b.lo+=c.lo;
    b.lo-=c.lo;
    const auto row=(unsigned(int(b.lo)+(cosine?16:0))&63)*4;
    c={k(266),k(267)};
    d.lo*=b.lo;
    b={b.lo,b.lo};
    e.lo=value;
    c.mul(b);
    a.lo-=d.lo;
    b.lo*=k(269);
    e.lo-=d.lo;
    h.lo=k(row+1);
    a={a.lo,a.lo};
    d.lo=e.lo;
    e.lo-=c.lo;
    f.mul(a);
    a.sub(c);
    g={k(258),k(259)};
    h.lo*=e.lo;
    d.lo-=e.lo;
    f.mul(a);
    a.mul(a);
    d.lo-=c.lo;
    c={k(row),k(row+1)};
    b.lo-=d.lo;
    d.lo=k(row+3);
    c.lo+=d.lo;
    h.lo-=c.lo;
    c.lo*=e.lo;
    g.mul(a);
    d.lo*=e.lo;
    c.mul(a);
    a.mul(a);
    f.add({k(260),k(261)});
    e.lo*=k(row);
    g.add({k(256),k(257)});
    f.mul(a);
    a.lo=d.lo;
    d.lo+=k(row+1);
    b.lo*=h.lo;
    h.lo=e.lo;
    e.lo+=d.lo;
    g.add(f);
    f.lo=k(row+1);
    f.lo-=d.lo;
    d.lo-=e.lo;
    b.lo+=k(row+2);
    g.mul(c);
    f.lo+=a.lo;
    d.lo+=h.lo;
    b.lo+=f.lo;
    b.lo+=d.lo;
    b.lo+=g.lo;
    b.lo+=g.hi;
    return e.lo+b.lo;
}
}
