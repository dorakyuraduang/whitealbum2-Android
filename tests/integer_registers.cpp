// Exact RAM/register snapshots for the hot-function local integer file.
#include "gpr_registers.hpp"
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace a=wa2::aot;
constexpr wr::U32 Data=0x2100000;
struct Result {wr::Cpu cpu;std::array<std::uint8_t,8192> bytes;bool raised{};wr::FaultKind kind{};wr::U32 address{};};
template<bool Metered> Result exercise(wr::Memory& memory,unsigned mode,unsigned seed){
    memory.protect(Data,8192,wr::Memory::Read|wr::Memory::Write);std::array<std::uint8_t,8192> pattern{};
    for(unsigned n=0;n<pattern.size();++n)pattern[n]=std::uint8_t(n*13+seed);memory.copy_in(Data,pattern);
    Result result{};auto& cpu=result.cpu;cpu.eip=0x12345678;
    for(unsigned n=0;n<8;++n)cpu.r[n]=(n+seed)*0x9e3779b9u;
    for(unsigned n=32;n<cpu.fp.bytes.size();++n)cpu.fp.bytes[n]=std::uint8_t(n*17+seed);
    cpu.r[wr::ESP]=Data+4096+2;
    if(mode==2)memory.protect(Data+4096,4096,0);
    if(mode==3)memory.protect(Data+4096,4096,wr::Memory::Read);
    try{
        auto scope=memory.execution_scope();a::IntegerRegisters<Metered> integer(cpu,memory,scope);
        integer.template set<wr::EAX>(0x89abcdefu+seed);
        integer.template set<wr::EAX,8,8>(seed+9);integer.template set<wr::ECX,16>(seed+1);
        integer.template set<wr::EBX>(integer.template get<wr::EAX,8,8>()+integer.template get<wr::ECX,16>());
        if(mode==1){
            integer.external([&]{const auto value=integer.template get<wr::EAX>();cpu.r[wr::EAX]=value^0xfedcba98u;cpu.r[wr::EDX]=91;throw wr::GuestFault(wr::FaultKind::unsupported,cpu.eip,"synthetic external prefix");});
        }else if(mode==2){
            integer.template set<wr::ESP>(Data+4096-2);integer.template set<wr::EDI>(integer.template pop<32>());
        }else if(mode==3){
            integer.template push<32>(0x11223344);
        }else if(mode==4){
            integer.external([&]{return wr::divide(cpu,integer.template get<wr::ECX>()&0u,32,false);});
        }else{
            integer.template push<32>(integer.template get<wr::EAX>());
            integer.template set<wr::ESI>(integer.template pop<32>());
            integer.template push<16>(integer.template get<wr::ECX,16>());
            integer.template set<wr::EDI,16>(integer.template pop<16>());
            integer.external([&]{return wr::multiply(cpu,integer.template get<wr::ECX>(),32,true);});
            integer.template word<wr::EDX>()^=integer.template get<wr::EAX>();
            integer.template set<wr::EBP>(integer.template get<wr::EDX>());
            if(mode==5)integer.external([&]{cpu.r.fill(0xfeedcafe);cpu.flags=0x903;cpu.fp.put16(0,0x37f);});
        }
    }catch(const wr::GuestFault& fault){result.raised=true;result.kind=fault.kind;result.address=fault.address;}
    memory.protect(Data,8192,wr::Memory::Read|wr::Memory::Write);memory.copy_out(Data,result.bytes);return result;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{};
    for(bool linear:{false,true}){wr::Memory memory;if(linear)CHECK(memory.enable_linear_memory());memory.map(Data,8192,wr::Memory::Read|wr::Memory::Write);
        for(unsigned mode=0;mode<6;++mode)for(unsigned seed=0;seed<32;++seed){const auto checked=exercise<true>(memory,mode,seed),native=exercise<false>(memory,mode,seed);
            CHECK(checked.cpu.r==native.cpu.r && checked.cpu.eip==native.cpu.eip && checked.cpu.flags==native.cpu.flags && checked.cpu.defined_flags==native.cpu.defined_flags);
            CHECK(checked.cpu.fp.bytes==native.cpu.fp.bytes && checked.cpu.selectors==native.cpu.selectors && checked.cpu.fs_base==native.cpu.fs_base && checked.cpu.gs_base==native.cpu.gs_base && checked.cpu.fs_valid==native.cpu.fs_valid && checked.cpu.gs_valid==native.cpu.gs_valid);
            CHECK(checked.raised==native.raised && checked.kind==native.kind && checked.address==native.address && checked.bytes==native.bytes);
            CHECK(mode==0 || mode==5 ? !native.raised:native.raised);++cases;
        }
    }
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-integer-registers.v1\",\"exact_cpu_stack_fault_cases\":"<<cases
        <<",\"partial_register_writes\":true,\"unaligned_cross_page_stack\":true,\"external_argument_cache_invalidated_on_success_and_exception\":true,\"mapped_and_linear_memory\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS "<<cases<<" exact local integer register, external prefix and stack fault snapshots.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
