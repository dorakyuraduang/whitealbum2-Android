// Native single-precision arithmetic for the admitted ordinary geometry path.
// Accepted transactions have exact source results and observable x87 status.
// Values/observations outside the contract retain the extended-precision path.
// Normalize the temporary stack to logical ST(0) at entry. Its slot indexes
// then stay independent of the source's rotating physical TOP, allowing the
// compiler to keep values in scalar registers. Reconstruct physical TOP/tags
// and every dead slot at commit; Empty specializes only a checked empty entry.
template<bool DeferredStatus=false,bool Normalized=true,bool Empty=false,bool Scalar=false,class Frame=GridFrame,unsigned EntryDepth=99> class GridNativeFloating {
    wr::Cpu& cpu_;Frame& frame_;
    std::array<fp::Extended,8> original_;
    std::array<float,8> values_{};
    unsigned source_top_{},top_{};std::uint8_t tags_{},loaded_{},changed_{},entry_tags_{},entry_holes_{};
    fp::Environment environment_;
    bool round_unknown_{};
    std::uint64_t instruction_,data_;std::uint16_t opcode_;
    unsigned slot(unsigned n)const{return (top_+n)&7;}
    static bool ordinary(std::uint32_t word){
        const auto exponent=word&0x7f800000u;
        return exponent!=0x7f800000u && (exponent || !(word&0x7fffffff));
    }
    static float single(std::uint32_t word){if(!ordinary(word))throw GridDeclined{};return std::bit_cast<float>(word);}
    static float extended(fp::Extended value){
        if(value.zero())return std::bit_cast<float>(std::uint32_t(value.sign())<<31);
        const int exponent=int(value.exponent())-16383;
        if(exponent < -126 || exponent>127 || value.unsupported() || (value.significand&0xffffffffffull))throw GridDeclined{};
        return std::bit_cast<float>((std::uint32_t(value.sign())<<31)|(std::uint32_t(exponent+127)<<23)|std::uint32_t((value.significand>>40)&0x7fffff));
    }
    static float binary64(std::uint64_t word){
        if(!(word&0x7fffffffffffffffull))return std::bit_cast<float>(std::uint32_t(word>>63)<<31);
        const int exponent=int((word>>52)&2047)-1023;
        if(exponent < -126 || exponent>127 || (word&0x1fffffffull))throw GridDeclined{};
        return std::bit_cast<float>((std::uint32_t(word>>63)<<31)|(std::uint32_t(exponent+127)<<23)|std::uint32_t((word>>29)&0x7fffff));
    }
    static fp::Extended representation(float value){
        const auto word=std::bit_cast<std::uint32_t>(value);const auto exponent=(word>>23)&255;
        if(!exponent)return {0,std::uint16_t(word>>31?0x8000:0)};
        return {(std::uint64_t((word&0x7fffff)|0x800000)<<40),std::uint16_t((word>>31?0x8000:0)|(exponent-127+16383))};
    }
    void clear_round(){environment_.status&=~fp::RoundUp;round_unknown_=false;}
    float get(unsigned n){
        const auto at=slot(n);if(!(tags_&(1u<<at)))throw GridDeclined{};
        if(!(loaded_&(1u<<at))){values_[at]=extended(original_[at]);loaded_|=std::uint8_t(1u<<at);}
        return values_[at];
    }
    void set(unsigned n,float value){const auto at=slot(n);values_[at]=value;tags_|=std::uint8_t(1u<<at);loaded_|=std::uint8_t(1u<<at);changed_|=std::uint8_t(1u<<at);}
    template<unsigned Depth,unsigned Index=0>[[gnu::always_inline]] float scalar_get(){
        static_assert(Depth<=8 && Index<8);constexpr auto at=(8-Depth+Index)&7,bit=1u<<at;
        if constexpr(Index>=Depth)if(!(tags_&bit))throw GridDeclined{};
        if(!(loaded_&bit)){
            if constexpr(Index<Depth)if(!(entry_tags_&bit))throw GridDeclined{};
            values_[at]=extended(original_[at]);loaded_|=std::uint8_t(bit);
        }
        return values_[at];
    }
    template<unsigned Depth,unsigned Index=0>[[gnu::always_inline]] void scalar_set(float value){
        static_assert(Depth<=8 && Index<8);constexpr auto at=(8-Depth+Index)&7;
        values_[at]=value;changed_|=std::uint8_t(1u<<at);loaded_|=std::uint8_t(1u<<at);
        entry_holes_&=std::uint8_t(~(1u<<at));
        if constexpr(Index>=Depth)tags_|=std::uint8_t(1u<<at);
    }
    template<unsigned Depth>[[gnu::always_inline]] void scalar_push(float value){
        static_assert(Depth<8);constexpr auto at=(7-Depth)&7;
        if(tags_&(1u<<at))throw GridDeclined{};scalar_set<Depth+1>(value);
    }
    void update_top(){environment_.status=std::uint16_t((environment_.status&~0x3800)|(((source_top_+top_)&7)<<11));}
    void push(float value){if(tags_&(1u<<((top_+7)&7)))throw GridDeclined{};top_=(top_+7)&7;update_top();set(0,value);}
    void pop(){if(!(tags_&(1u<<top_)))throw GridDeclined{};tags_&=std::uint8_t(~(1u<<top_));top_=(top_+1)&7;update_top();}
    [[gnu::always_inline]] float binary(unsigned digit,float left,float right){
        if(environment_.control&0xf00)throw GridDeclined{};
        if(digit==5 || digit==7){std::swap(left,right);digit^=1;}
        const auto a=std::bit_cast<std::uint32_t>(left),b=std::bit_cast<std::uint32_t>(right);
        if constexpr(DeferredStatus){
            float result{};bool exact_zero{};
            if(digit==0){result=left+right;exact_zero=left==-right;}
            else if(digit==4){result=left-right;exact_zero=left==right;}
            else if(digit==1){result=left*right;exact_zero=!(a&0x7fffffff) || !(b&0x7fffffff);}
            else if(digit==6){if(!(b&0x7fffffff))throw GridDeclined{};result=left/right;exact_zero=!(a&0x7fffffff);}
            else throw GridDeclined{};
            const auto bits=std::bit_cast<std::uint32_t>(result);
            if(!ordinary(bits) || (!(bits&0x7fffffff) && !exact_zero))throw GridDeclined{};
            // Masked, normal IEEE arithmetic raises the same sticky inexact
            // flag as source PC24. Read it only at an actual observation.
            // C1 must be cleared by a later exact store/load/compare; observing
            // it sooner rejects this transaction instead of guessing a bit.
            clear_round();round_unknown_=true;return result;
        }
        const double x=left,y=right;double exact{},comparison{};float result{};
        if(digit==0 || digit==4){
            // At most 53 significant positions: the double sum/difference is
            // exact, independently of compiler/host extended-precision types.
            if((a&0x7fffffff) && (b&0x7fffffff) && std::abs(int((a>>23)&255)-int((b>>23)&255))>29)throw GridDeclined{};
            if(digit==0){exact=x+y;result=left+right;}else{exact=x-y;result=left-right;}
            comparison=double(result);
        }else if(digit==1){exact=x*y;result=left*right;comparison=double(result);}
        else if(digit==6){
            if(!(b&0x7fffffff))throw GridDeclined{};
            result=left/right;
            // Both factors have at most 24 bits. Their exact double product
            // tells whether division rounded, and its magnitude direction.
            exact=x;comparison=double(result)*y;
        }else throw GridDeclined{};
        const auto bits=std::bit_cast<std::uint32_t>(result);
        if(!ordinary(bits) || (!(bits&0x7fffffff) && exact!=0))throw GridDeclined{};
        clear_round();
        const auto expected=std::bit_cast<std::uint64_t>(exact)&0x7fffffffffffffffull;
        const auto rounded=std::bit_cast<std::uint64_t>(comparison)&0x7fffffffffffffffull;
        if(expected!=rounded){environment_.raise(fp::Inexact);if(rounded>expected)environment_.raise(fp::RoundUp);}
        return result;
    }
public:
    GridNativeFloating(wr::Cpu& cpu,Frame& frame):cpu_(cpu),frame_(frame),source_top_(Normalized?cpu.fp.top():0),top_(Normalized?0:cpu.fp.top()),
        tags_(Empty?0:Normalized?std::rotr(cpu.fp.bytes[4],int(source_top_)):cpu.fp.bytes[4]),
        environment_{cpu.fp.get16(0),cpu.fp.get16(2)},instruction_(cpu.fp.get64(8)),data_(cpu.fp.get64(16)),opcode_(cpu.fp.get16(6)){
        if((environment_.control&0xf3f)!=0x3f)throw GridDeclined{};
        if constexpr(Empty){static_assert(Normalized);if(cpu.fp.bytes[4])throw GridDeclined{};}
        if constexpr(Scalar){
            static_assert(Normalized);
            const unsigned depth=EntryDepth!=99?EntryDepth:cpu.eip==0x43cc17?0:cpu.eip==0x43ce95?3:99;
            if(depth==99)throw GridDeclined{};
            source_top_=(cpu.fp.top()+depth)&7;top_=(8-depth)&7;
            tags_=std::rotr(cpu.fp.bytes[4],int(source_top_));entry_tags_=tags_;
            const auto introduced=std::rotl(std::uint8_t((1u<<depth)-1),int(top_));
            entry_holes_=std::uint8_t(introduced&~entry_tags_);tags_&=std::uint8_t(~introduced);
        }
        for(unsigned n=0;n<8;++n)original_[slot(n)]={cpu.fp.get64(32+n*16),cpu.fp.get16(40+n*16)};
    }
    [[gnu::always_inline]] std::uint16_t word(unsigned offset)const{
        if(offset==0)return environment_.control;
        if constexpr(DeferredStatus){
            if(round_unknown_)throw GridDeclined{};
            return std::uint16_t(environment_.status|(std::fetestexcept(FE_INEXACT)?fp::Inexact:0));
        }
        return environment_.status;
    }
    template<unsigned Opcode,unsigned Encoding,bool Memory>
    void execute(void* operand,unsigned opcode,bool control=false,bool integer_flags=false,wr::U32 data=0,bool has_memory=false){
        if(integer_flags)throw GridDeclined{};
        if constexpr(Memory){
            if constexpr(Opcode==216 || Opcode==220){
                const auto right=Opcode==216?single(std::uint32_t(fp::detail::read_word(operand,4))):binary64(fp::detail::read_word(operand,8));
                set(0,binary(Encoding,get(0),right));
            }else if constexpr((Opcode==217 || Opcode==221) && Encoding==0){
                clear_round();push(Opcode==217?single(std::uint32_t(fp::detail::read_word(operand,4))):binary64(fp::detail::read_word(operand,8)));
            }else if constexpr((Opcode==217 || Opcode==221) && (Encoding==2 || Encoding==3)){
                clear_round();const auto value=get(0);
                if constexpr(Opcode==217)fp::detail::write_word(operand,std::bit_cast<std::uint32_t>(value),4);
                else fp::detail::write_word(operand,std::bit_cast<std::uint64_t>(double(value)),8);
                if constexpr(Encoding==3)pop();
            }else if constexpr(Opcode==219 && Encoding==0){
                clear_round();const auto value=std::bit_cast<std::int32_t>(std::uint32_t(fp::detail::read_word(operand,4)));
                const float converted=float(value);if(double(converted)!=double(value))throw GridDeclined{};push(converted);
            }else if constexpr(Opcode==219 && Encoding==3){
                if(environment_.rounding()!=3)throw GridDeclined{};const auto value=get(0);
                if(double(value)<-2147483648. || double(value)>=2147483648.)throw GridDeclined{};
                const auto converted=std::int32_t(value);clear_round();if(double(converted)!=double(value))environment_.raise(fp::Inexact);
                fp::detail::write_word(operand,std::bit_cast<std::uint32_t>(converted),4);pop();
            }else if constexpr(Opcode==217 && Encoding==5){
                environment_.control=std::uint16_t((fp::detail::read_word(operand,2)&0x1f3f)|0x40);
                if((environment_.control&0x33f)!=0x3f)throw GridDeclined{};
                if(environment_.status&~environment_.control&0x3f)environment_.status|=0x8080;else environment_.status&=~0x8080;
            }else static_assert(Opcode==0,"unadmitted native grid memory operation");
        }else{
            constexpr unsigned digit=(Encoding>>3)&7,index=Encoding&7;
            if constexpr(Opcode==216 && (digit==2 || digit==3)){
                const auto left=get(0),right=get(index);clear_round();environment_.status&=~0x4700;
                if(left<right)environment_.status|=0x100;else if(left==right)environment_.status|=0x4000;
                if constexpr(digit==3)pop();
            }
            else if constexpr(Opcode==216)set(0,binary(digit,get(0),get(index)));
            else if constexpr(Opcode==222){set(index,binary(digit>=4?digit^1:digit,get(index),get(0)));pop();}
            else if constexpr(Opcode==217 && digit==0){clear_round();const auto value=get(index);push(value);}
            else if constexpr(Opcode==217 && digit==1){clear_round();const auto first=get(0),second=get(index);set(0,second);set(index,first);}
            else if constexpr(Opcode==217 && (Encoding==238 || Encoding==232)){clear_round();push(Encoding==238?0.f:1.f);}
            else if constexpr(Opcode==221 && digit==3){clear_round();set(index,get(0));pop();}
            else static_assert(Opcode==0,"unadmitted native grid register operation");
        }
        if(!control){instruction_=cpu_.eip;opcode_=std::uint16_t(opcode&0x7ff);if(has_memory)data_=data;}
    }
    template<unsigned Opcode,unsigned Encoding,bool Memory>
    void memory(wr::U32 address,unsigned size,bool store,unsigned opcode,bool control=false){
        alignas(16) std::array<std::uint8_t,16> operand{};
        if(!size || size>10)throw GridDeclined{};
        auto* destination=store?frame_.writable(address,size):nullptr;
        if(!store)std::memcpy(operand.data(),frame_.readable(address,size),size);
        execute<Opcode,Encoding,Memory>(operand.data(),opcode,control,false,address,true);
        if(store)std::memcpy(destination,operand.data(),size);
    }
    template<unsigned Depth>[[gnu::always_inline]] std::uint16_t word_at(unsigned offset){
        if constexpr(Scalar){top_=(8-Depth)&7;update_top();}return word(offset);
    }
    template<unsigned Depth,unsigned Opcode,unsigned Encoding,bool Memory>
    [[gnu::always_inline]] void execute_at(void* operand,unsigned opcode,bool control=false,bool integer_flags=false,wr::U32 data=0,bool has_memory=false){
        if constexpr(!Scalar){execute<Opcode,Encoding,Memory>(operand,opcode,control,integer_flags,data,has_memory);return;}
        else{
            if(integer_flags)throw GridDeclined{};
            if constexpr(Memory){
                if constexpr((Opcode==216 || Opcode==220) && (Encoding==2 || Encoding==3)){
                    const auto right=Opcode==216?single(std::uint32_t(fp::detail::read_word(operand,4))):binary64(fp::detail::read_word(operand,8));
                    const auto left=scalar_get<Depth>();clear_round();environment_.status&=~0x4700;
                    if(left<right)environment_.status|=0x100;else if(left==right)environment_.status|=0x4000;
                }else if constexpr(Opcode==216 || Opcode==220){
                    const auto right=Opcode==216?single(std::uint32_t(fp::detail::read_word(operand,4))):binary64(fp::detail::read_word(operand,8));
                    scalar_set<Depth>(binary(Encoding,scalar_get<Depth>(),right));
                }else if constexpr((Opcode==217 || Opcode==221) && Encoding==0){
                    clear_round();scalar_push<Depth>(Opcode==217?single(std::uint32_t(fp::detail::read_word(operand,4))):binary64(fp::detail::read_word(operand,8)));
                }else if constexpr((Opcode==217 || Opcode==221) && (Encoding==2 || Encoding==3)){
                    clear_round();const auto value=scalar_get<Depth>();
                    if constexpr(Opcode==217)fp::detail::write_word(operand,std::bit_cast<std::uint32_t>(value),4);
                    else fp::detail::write_word(operand,std::bit_cast<std::uint64_t>(double(value)),8);
                }else if constexpr(Opcode==219 && Encoding==0){
                    clear_round();const auto value=std::bit_cast<std::int32_t>(std::uint32_t(fp::detail::read_word(operand,4)));
                    const float converted=float(value);if(double(converted)!=double(value))throw GridDeclined{};scalar_push<Depth>(converted);
                }else if constexpr(Opcode==219 && Encoding==3){
                    if(environment_.rounding()!=3)throw GridDeclined{};const auto value=scalar_get<Depth>();
                    if(double(value)<-2147483648. || double(value)>=2147483648.)throw GridDeclined{};
                    const auto converted=std::int32_t(value);clear_round();if(double(converted)!=double(value))environment_.raise(fp::Inexact);
                    fp::detail::write_word(operand,std::bit_cast<std::uint32_t>(converted),4);
                }else if constexpr(Opcode==217 && Encoding==5){
                    environment_.control=std::uint16_t((fp::detail::read_word(operand,2)&0x1f3f)|0x40);
                    if((environment_.control&0x33f)!=0x3f)throw GridDeclined{};
                    if(environment_.status&~environment_.control&0x3f)environment_.status|=0x8080;else environment_.status&=~0x8080;
                }else static_assert(Opcode==0,"unadmitted scalar grid memory operation");
            }else{
                constexpr unsigned digit=(Encoding>>3)&7,index=Encoding&7;
                if constexpr(Opcode==216 && (digit==2 || digit==3)){
                    const auto left=scalar_get<Depth>(),right=scalar_get<Depth,index>();clear_round();environment_.status&=~0x4700;
                    if(left<right)environment_.status|=0x100;else if(left==right)environment_.status|=0x4000;
                }else if constexpr(Opcode==216)scalar_set<Depth>(binary(digit,scalar_get<Depth>(),scalar_get<Depth,index>()));
                else if constexpr(Opcode==222)scalar_set<Depth,index>(binary(digit>=4?digit^1:digit,scalar_get<Depth,index>(),scalar_get<Depth>()));
                else if constexpr(Opcode==217 && Encoding==224){clear_round();scalar_set<Depth>(-scalar_get<Depth>());}
                else if constexpr(Opcode==217 && digit==0){clear_round();scalar_push<Depth>(scalar_get<Depth,index>());}
                else if constexpr(Opcode==217 && digit==1){clear_round();const auto first=scalar_get<Depth>(),second=scalar_get<Depth,index>();scalar_set<Depth>(second);scalar_set<Depth,index>(first);}
                else if constexpr(Opcode==217 && (Encoding==238 || Encoding==232)){clear_round();scalar_push<Depth>(Encoding==238?0.f:1.f);}
                else if constexpr(Opcode==221 && digit==3){clear_round();scalar_set<Depth,index>(scalar_get<Depth>());}
                else static_assert(Opcode==0,"unadmitted scalar grid register operation");
            }
            if(!control){instruction_=cpu_.eip;opcode_=std::uint16_t(opcode&0x7ff);if(has_memory)data_=data;}
        }
    }
    template<unsigned Depth,unsigned Opcode,unsigned Encoding,bool Memory>
    [[gnu::always_inline]] void memory_at(wr::U32 address,unsigned size,bool store,unsigned opcode,bool control=false){
        if constexpr(!Scalar){memory<Opcode,Encoding,Memory>(address,size,store,opcode,control);return;}
        else{
            if(!size || size>10)throw GridDeclined{};
            auto* operand=store?frame_.writable(address,size):const_cast<std::uint8_t*>(frame_.readable(address,size));
            execute_at<Depth,Opcode,Encoding,Memory>(operand,opcode,control,false,address,true);
        }
    }
    template<unsigned Depth>void finish_at(){
        if constexpr(Scalar){static_assert(Depth<=8);top_=(8-Depth)&7;tags_|=std::uint8_t(std::rotl(std::uint8_t((1u<<Depth)-1),int(top_))&~entry_holes_);update_top();}
    }
    void commit(){
        if constexpr(DeferredStatus)environment_.status=word(2);
        for(unsigned n=0;n<8;++n){const auto at=slot(n);const auto value=changed_&(1u<<at)?representation(values_[at]):original_[at];
            cpu_.fp.put64(32+n*16,value.significand);cpu_.fp.put16(40+n*16,value.sign_exponent);}
        cpu_.fp.bytes[4]=std::rotl(tags_,int(source_top_));cpu_.fp.put16(0,environment_.control);cpu_.fp.put16(2,environment_.status);
        cpu_.fp.clear_x87_padding();cpu_.fp.put32(28,0xffff);cpu_.fp.put64(8,instruction_);cpu_.fp.put64(16,data_);cpu_.fp.put16(6,opcode_);
    }
};
