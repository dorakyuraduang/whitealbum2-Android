// Whole insertion-search loop against the unchanged 0x442000 instruction page.
namespace sort_object_test {
struct Input {unsigned index{},mode{};U32 key{},flags{},defined{},stack{0x07000100};unsigned slice{};std::uint64_t budget{~std::uint64_t(0)};};
template<class CodeFixture>
Result execute(const CodeFixture& image,const Input& input,bool fast){
    const U32 sp=input.stack;constexpr U32 objects=0xab3fb9;
    Memory m;const bool linear=input.mode!=16;
    if(linear)CHECK(m.enable_linear_memory());
    m.map(0x442000,4096,Memory::Read|Memory::Execute);m.initialize(0x442000,image.code(0x442000).first(4096));
    m.map(0x4c2000,4096,Memory::Read|Memory::Write);
    m.map(0x07000000,16384,Memory::Read|Memory::Write|(input.mode==17?Memory::Execute:0));
    m.map(0xab3000,0x81000,Memory::Read|Memory::Write);
    std::vector<std::uint8_t> stack(16384),data(0x81000);
    for(unsigned j=0;j<stack.size();++j)stack[j]=std::uint8_t(j*17+(j>>4));
    m.initialize(0x07000000,stack);m.store(0x4c22b0,1,32);
    for(unsigned j=0;j<2048;++j){
        const auto value=j<input.index?std::int16_t(-16000+int(j)*16):std::int16_t(30000);
        m.store(sp+0x438+j*2,std::bit_cast<std::uint16_t>(value),16);m.store(sp+0x1438+j*2,0xffff-j,16);
    }
    for(unsigned n=0;n<6 && input.index+n<2048;++n){
        const auto at=objects-0xab3000+(input.index+n)*256;
        data[at-1]=1;data[at]=1;U32 attributes=n==5?0:1;
        const std::uint16_t values[]={std::uint16_t(input.key),0x8000,30000,32767,0xffff,123};
        std::memcpy(data.data()+at+1,&values[n],2);std::memcpy(data.data()+at+3,&attributes,4);
    }
    if(input.mode==18){data[objects-0xab3000+input.index*256-1]=0;}
    if(input.mode==19){data[objects-0xab3000+input.index*256]=0;}
    m.initialize(0xab3000,data);
    if(input.mode==20){m.store(sp+0x438,30000,16);m.store(sp+0x43a,0x8000,16);}
    if(input.mode==21)m.initialize(0x4422bf,std::array<std::uint8_t,1>{0xcc});
    if(input.mode==22)m.protect(0x07001000,4096,Memory::Read);
    if(input.mode==23)m.protect(0xab4000,4096,Memory::Read);
    Cpu cpu;cpu.eip=0x442290;cpu.r={objects+input.index*256,0x01234567,0x89abcdef,input.index-1,sp,0xabcdef01,0x13579bdf,0x2468ace0};
    cpu.flags=input.flags;cpu.defined_flags=input.defined;
    for(unsigned j=0;j<512;++j)cpu.fp.bytes[j]=std::uint8_t(j*23+input.key);
    m.store(sp+0x1c,cpu.r[EAX],32);
    if(input.mode==24)m.protect(0xab4000,4096,0);
    if(input.mode==25)m.protect(sp&~4095u,4096,Memory::Read);
    if(input.mode==26)m.protect(0x4c2000,4096,0);
    const unsigned slices[]={1,8,9,32,4096,131072};const auto slice=input.slice?input.slice:slices[input.mode%6];
    const std::uint64_t budgets[]={0,1,8,9,14,32,33,35,36,37,200,1000,10000,200000,200000,200000,
        200000,200000,200000,200000,200000,200000,200000,200000,200000,200000,200000};
    auto budget=input.budget==~std::uint64_t(0)?budgets[input.mode]:input.budget;Result result;
    const auto running=[&]{return cpu.eip>=0x442290 && cpu.eip<0x442374;};
    try{
        while(running()){
            auto scope=m.execution_scope();unsigned count=0;
            while(count<slice && running()){
                if(fast)if(const auto n=wa2_static_batch(cpu,m,budget,slice-count,0xfffffff0)){
                    CHECK(n<=slice-count);count+=n;result.instructions+=n;++result.batches;continue;
                }
                tick(budget,cpu.eip);CHECK(code_page_1090(cpu,m,budget));++count;++result.instructions;
            }
        }
    }catch(const GuestFault& fault){result.fault=std::to_string(int(fault.kind))+":"+std::to_string(fault.address);}
    result.cpu=cpu;result.budget=budget;
    if(input.mode==24)m.protect(0xab4000,4096,Memory::Read|Memory::Write);
    if(input.mode==26)m.protect(0x4c2000,4096,Memory::Read|Memory::Write);
    for(auto [at,size]:{std::pair<U32,unsigned>{0x442000,4096},{0x4c2000,4096},{0x07000000,16384},{0xab3000,0x81000}}){
        const auto start=result.bytes.size();result.bytes.resize(start+size);m.copy_out(at,std::span(result.bytes).subspan(start,size));
    }
    return result;
}
template<class CodeFixture>
unsigned run(const CodeFixture& image){
    std::mt19937 random(0x442290);unsigned cases=0;std::uint64_t batches=0;
    for(unsigned index:{0u,1u,7u,31u,127u,1023u,2047u})for(unsigned mode=0;mode<27;++mode){
        Input input{index,mode,random(),random(),random()};
        const auto original=execute(image,input,false),optimized=execute(image,input,true);
        equal(original,optimized);batches+=optimized.batches;++cases;
    }
    CHECK(batches>0);return cases;
}
}
