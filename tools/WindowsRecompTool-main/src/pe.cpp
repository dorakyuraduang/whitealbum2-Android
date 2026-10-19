#include "winrecomp/image.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace wr {
namespace {
void require(bool condition, const char* what) { if (!condition) throw std::runtime_error(what); }
std::uint16_t u16(const std::vector<std::uint8_t>& b, std::size_t p) {
    require(p <= b.size() && b.size()-p >= 2, "truncated 16-bit field");
    return std::uint16_t(std::uint16_t(b[p]) | (std::uint16_t(b[p+1]) << 8));
}
std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t p) {
    require(p <= b.size() && b.size()-p >= 4, "truncated 32-bit field");
    return std::uint32_t(b[p]) | (std::uint32_t(b[p+1])<<8) | (std::uint32_t(b[p+2])<<16) | (std::uint32_t(b[p+3])<<24);
}
}
std::string hex(std::uint64_t v) { std::ostringstream s; s<<"0x"<<std::hex<<v; return s.str(); }
std::string quote(const std::string& v) {
    std::ostringstream s; s<<'"';
    for (unsigned char c : v) {
        if (c=='"' || c=='\\') s<<'\\'<<c;
        else if (c<32 || c>=127) s<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c);
        else s<<c;
    }
    s<<'"'; return s.str();
}
void write_file(const std::filesystem::path& p,const std::string& content) {
    std::ofstream f(p, std::ios::binary|std::ios::trunc);
    if (!f || !(f<<content)) throw std::runtime_error("cannot write output: "+p.string());
    f.close(); if (!f) throw std::runtime_error("output close failed: "+p.string());
}
std::uint64_t Section::span() const { return std::max(virtual_size,raw_size); }
std::optional<std::size_t> Image::offset(Address a,std::size_t n) const {
    if (a<base) return {};
    const std::uint64_t r=a-base;
    if (n>size || r>size-n) return {};
    if (r<headers_size && n<=headers_size-r && n<=bytes.size() && r<=bytes.size()-n) return std::size_t(r);
    for (const auto& s:sections) {
        if (r<s.rva) continue;
        const auto d=r-s.rva;
        if (d<s.raw_size && n<=s.raw_size-d) return std::size_t(s.raw_offset+d);
    }
    return {};
}
std::span<const std::uint8_t> Image::code(Address a) const {
    if (a<base) return {};
    const std::uint64_t r=a-base;
    for (const auto& s:sections) {
        if (!(s.characteristics & 0x20000000u) || r<s.rva) continue;
        const auto d=r-s.rva;
        if (d<s.raw_size) return std::span(bytes).subspan(std::size_t(s.raw_offset+d),std::size_t(s.raw_size-d));
    }
    return {};
}
const Import* Image::import_at(Address a) const {
    for (const auto& i:imports) if (i.iat==a) return &i;
    return nullptr;
}
Image Image::load(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    if (!f) throw std::runtime_error("cannot open input: "+p.string());
    const auto n=f.tellg();
    require(n>=0 && std::uint64_t(n)<=512ull*1024*1024,"input exceeds 512 MiB safety limit");
    std::vector<std::uint8_t> b(static_cast<std::size_t>(n));
    f.seekg(0); if (!b.empty()) f.read(reinterpret_cast<char*>(b.data()),n);
    require(bool(f),"input read failed"); return parse(std::move(b));
}
Image Image::parse(std::vector<std::uint8_t> data) {
    Image out; out.bytes=std::move(data); const auto& b=out.bytes;
    require(b.size()>=64 && u16(b,0)==0x5a4d,"not an MZ image");
    const std::size_t pe=u32(b,0x3c);
    require(pe<=b.size() && b.size()-pe>=24 && u32(b,pe)==0x4550,"bad PE signature/header");
    require(u16(b,pe+4)==0x14c,"only PE32 i386 is supported");
    const auto count=u16(b,pe+6), osize=u16(b,pe+20);
    const auto opt=pe+24;
    require(count>0 && count<=96,"invalid section count");
    require(osize>=96 && opt<=b.size() && osize<=b.size()-opt,"truncated optional header");
    require(u16(b,opt)==0x10b,"only PE32 is supported");
    const auto entry_rva=u32(b,opt+16);
    out.base=u32(b,opt+28); out.size=u32(b,opt+56); out.headers_size=u32(b,opt+60);
    require(out.size && out.size<=512u*1024*1024,"invalid/oversized image");
    require(std::uint64_t(out.base)+out.size<=0x100000000ull,"image wraps 32-bit address space");
    require(entry_rva<out.size,"entry outside image"); out.entry=out.base+entry_rva;
    require(out.headers_size && out.headers_size<=b.size() && out.headers_size<=out.size,"invalid SizeOfHeaders");
    const auto dirs=u32(b,opt+92);
    require(dirs<=(osize-96u)/8,"data directories exceed optional header");
    for (std::size_t i=0;i<std::min<std::uint32_t>(dirs,16);++i) out.directories[i]={u32(b,opt+96+i*8),u32(b,opt+100+i*8)};
    const auto table=opt+osize;
    require(table<=out.headers_size && std::size_t(count)*40<=out.headers_size-table,"section table outside headers");
    for (std::size_t i=0;i<count;++i) {
        const auto p=table+i*40; std::string name;
        for(std::size_t j=0;j<8 && b[p+j];++j) name.push_back(char(b[p+j]));
        Section s{name,u32(b,p+12),u32(b,p+8),u32(b,p+20),u32(b,p+16),u32(b,p+36)};
        require(s.rva>=out.headers_size && std::uint64_t(s.rva)+s.span()<=out.size,"section outside image or overlaps headers");
        require(s.raw_size==0 || (s.raw_offset>=out.headers_size && std::uint64_t(s.raw_offset)+s.raw_size<=b.size()),"section raw range invalid");
        for (const auto& old:out.sections) {
            require(!(s.span() && old.span() && s.rva<std::uint64_t(old.rva)+old.span() && old.rva<std::uint64_t(s.rva)+s.span()),"overlapping virtual sections");
            require(!(s.raw_size && old.raw_size && s.raw_offset<std::uint64_t(old.raw_offset)+old.raw_size && old.raw_offset<std::uint64_t(s.raw_offset)+s.raw_size),"overlapping raw sections");
        }
        out.sections.push_back(s);
    }
    auto va=[&](std::uint64_t r)->Address { require(r<out.size,"RVA outside image"); return out.base+std::uint32_t(r); };
    auto at=[&](std::uint64_t r,std::size_t n)->std::size_t {
        auto o=out.offset(va(r),n); require(o.has_value(),"directory references unbacked/truncated bytes"); return *o;
    };
    auto read32=[&](std::uint64_t r) { return u32(b,at(r,4)); };
    auto str=[&](std::uint64_t r) {
        std::string text;
        for (unsigned i=0;i<4096;++i) { const auto c=b[at(r+i,1)]; if (!c) return text; text.push_back(char(c)); }
        throw std::runtime_error("unterminated/oversized PE string");
    };
    const auto imp=out.directories[1];
    if (imp.rva || imp.size) {
        require(imp.rva && imp.size>=20,"invalid import directory"); bool done=false;
        for (std::uint64_t d=0;d+20<=imp.size;d+=20) {
            const auto r=std::uint64_t(imp.rva)+d;
            const auto lookup=read32(r), stamp=read32(r+4), chain=read32(r+8), name=read32(r+12), iat=read32(r+16);
            if (!(lookup|stamp|chain|name|iat)) { done=true; break; }
            require(name && iat,"invalid import descriptor"); const auto dll=str(name);
            const auto list=lookup?lookup:iat; bool ended=false;
            for (std::uint64_t j=0;j<100000;++j) {
                const auto item=read32(std::uint64_t(list)+j*4); if (!item) { ended=true; break; }
                const auto slot=va(std::uint64_t(iat)+j*4); require(out.offset(slot,4).has_value(),"unbacked IAT slot");
                Import x; x.dll=dll; x.iat=slot;
                if (item&0x80000000u) { x.ordinal=std::uint16_t(item); x.name="#"+std::to_string(*x.ordinal); }
                else { (void)at(item,2); x.name=str(std::uint64_t(item)+2); }
                out.imports.push_back(std::move(x));
            }
            require(ended,"unterminated import lookup table");
        }
        require(done,"unterminated import descriptor table");
    }
    const auto ex=out.directories[0];
    if (ex.rva || ex.size) {
        require(ex.rva && ex.size>=40,"invalid export directory"); (void)at(ex.rva,40);
        const auto n=read32(std::uint64_t(ex.rva)+20), functions=read32(std::uint64_t(ex.rva)+28);
        require(n<=1000000,"too many exports");
        for(std::uint64_t i=0;i<n;++i) {
            const auto r=read32(std::uint64_t(functions)+i*4);
            if (r && !(r>=ex.rva && r<std::uint64_t(ex.rva)+ex.size)) { auto a=va(r); if (!out.code(a).empty()) out.exports.insert(a); }
        }
    }
    const auto tls=out.directories[9];
    if (tls.rva || tls.size) {
        require(tls.rva && tls.size>=24,"invalid TLS directory"); (void)at(tls.rva,24);
        const auto callbacks=read32(std::uint64_t(tls.rva)+12);
        if (callbacks) {
            require(callbacks>=out.base,"TLS callback table before image"); bool done=false;
            for (std::uint64_t i=0;i<4096;++i) {
                const auto a=read32(std::uint64_t(callbacks-out.base)+i*4);
                if (!a) {done=true;break;} require(!out.code(a).empty(),"TLS callback is not file-backed executable code"); out.tls_callbacks.insert(a);
            }
            require(done,"unterminated TLS callbacks");
        }
    }
    return out;
}
std::vector<std::uint8_t> Image::mapped() const {
    std::vector<std::uint8_t> result(size,0);
    std::copy_n(bytes.begin(),headers_size,result.begin());
    for(const auto& s:sections) if(s.raw_size) std::copy_n(bytes.begin()+s.raw_offset,s.raw_size,result.begin()+s.rva);
    return result;
}
std::string Image::json() const {
    std::ostringstream o;
    o<<"{\"schema\":\"winrecomp.pe.v1\",\"machine\":\"i386\",\"image_base\":"<<base<<",\"entry\":"<<entry<<",\"image_size\":"<<size<<",\"sections\":[";
    bool first=true;
    for(const auto& s:sections) { if(!first)o<<',';first=false; o<<"{\"name\":"<<quote(s.name)<<",\"rva\":"<<s.rva<<",\"virtual_size\":"<<s.virtual_size<<",\"raw_offset\":"<<s.raw_offset<<",\"raw_size\":"<<s.raw_size<<",\"characteristics\":"<<s.characteristics<<'}'; }
    o<<"],\"directories\":[";
    for(std::size_t i=0;i<16;++i){if(i)o<<',';o<<"{\"rva\":"<<directories[i].rva<<",\"size\":"<<directories[i].size<<'}';}
    o<<"],\"imports\":[";first=true;
    for(const auto& x:imports){if(!first)o<<',';first=false;o<<"{\"dll\":"<<quote(x.dll)<<",\"name\":"<<quote(x.name)<<",\"iat\":"<<x.iat<<",\"ordinal\":";if(x.ordinal)o<<*x.ordinal;else o<<"null";o<<'}';}
    o<<"],\"exports\":[";first=true;for(auto a:exports){if(!first)o<<',';first=false;o<<a;}
    o<<"],\"tls_callbacks\":[";first=true;for(auto a:tls_callbacks){if(!first)o<<',';first=false;o<<a;}
    o<<"]}";return o.str();
}
}
