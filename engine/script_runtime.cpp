#include "script_runtime.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <locale>
#include <sstream>
#include <cmath>
#include <limits>

namespace wa2::script {
namespace {
std::uint32_t u32(std::span<const std::uint8_t> bytes,std::size_t at) {
    if(at>bytes.size() || bytes.size()-at<4)throw Error("truncated LSCR data");
    return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|
        (std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);
}
std::int32_t signed_word(std::uint32_t value){return std::bit_cast<std::int32_t>(value);}
std::string bytes_string(std::span<const std::uint8_t> value) {
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}
bool sjis_lead(std::uint8_t value){return (value>=0x81 && value<=0x9f)||(value>=0xe0 && value<=0xfc);}
bool game_lead(std::uint8_t value){return (value>=0x81 && value<=0x9f)||(value>=0xe0 && value<=0xea)||value==0xf0;}
std::vector<std::string> split(std::span<const std::uint8_t> bytes,std::string_view delimiters,bool empty,bool game_text=false) {
    std::vector<std::string> result;std::size_t begin=0;
    for(std::size_t at=0;at<bytes.size() && bytes[at];++at){
        if((game_text?game_lead(bytes[at]):sjis_lead(bytes[at])) && at+1<bytes.size() && bytes[at+1]){++at;continue;}
        if(delimiters.find(char(bytes[at]))!=std::string_view::npos){
            if(empty || at!=begin)result.push_back(bytes_string(bytes.subspan(begin,at-begin)));
            begin=at+1;
        }
    }
    auto end=std::find(bytes.begin()+std::ptrdiff_t(begin),bytes.end(),std::uint8_t(0));
    const auto length=std::size_t(end-bytes.begin())-begin;
    if(empty || length)result.push_back(bytes_string(bytes.subspan(begin,length)));
    return result;
}
std::int32_t decimal(std::string_view value) {
    // The assets use the CRT's decimal conversion, including leading spaces
    // and signs. Do not use the host locale or treat a leading zero as octal.
    while(!value.empty() && (value.front()==' '||value.front()=='\t'))value.remove_prefix(1);
    bool negative=false;
    if(!value.empty() && (value.front()=='+'||value.front()=='-')){negative=value.front()=='-';value.remove_prefix(1);}
    std::uint32_t number=0;bool digit=false;
    for(const char c:value){if(c<'0'||c>'9')break;digit=true;number=number*10+std::uint32_t(c-'0');}
    return digit?signed_word(negative?0-number:number):0;
}
Type type(std::uint32_t value){if(value<1 || value>5)throw Error("invalid LSCR value type");return Type(value);}
std::int32_t truncation(float value) {
    // CVTT/FIST conversion's indefinite result, without a C++ out-of-range cast.
    if(!std::isfinite(value) || double(value)<-2147483648.0 || double(value)>=2147483648.0)
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}
std::int32_t wrap_add(std::int32_t a,std::int32_t b){return signed_word(std::uint32_t(a)+std::uint32_t(b));}
std::int32_t wrap_sub(std::int32_t a,std::int32_t b){return signed_word(std::uint32_t(a)-std::uint32_t(b));}
std::int32_t wrap_mul(std::int32_t a,std::int32_t b){return signed_word(std::uint32_t(a)*std::uint32_t(b));}
std::int32_t divide(std::int32_t a,std::int32_t b,bool remainder=false){
    if(!b || (a==std::numeric_limits<std::int32_t>::min() && b==-1))throw Error("LSCR integer division fault");
    return remainder?a%b:a/b;
}
}
Type Value::type() const noexcept {
    return std::holds_alternative<std::int32_t>(data_)?Type::integer:
        std::holds_alternative<float>(data_)?Type::real:Type::text;
}
std::int32_t Value::integer() const {
    if(auto* p=std::get_if<std::int32_t>(&data_))return *p;
    if(auto* p=std::get_if<float>(&data_))return truncation(*p);
    throw Error("LSCR string used as a number");
}
float Value::real() const {
    if(auto* p=std::get_if<float>(&data_))return *p;
    if(auto* p=std::get_if<std::int32_t>(&data_))return float(*p);
    throw Error("LSCR string used as a real number");
}
const std::string& Value::text() const {
    if(auto* p=std::get_if<std::string>(&data_))return *p;
    throw Error("LSCR number used as a string");
}
Value Value::default_text(){Value value(std::string{});value.default_text_=true;return value;}
Globals Globals::parse(std::span<const std::uint8_t> bytes) {
    auto fields=split(bytes,",\r\n",false);
    if(fields.size()%3)throw Error("truncated Global.vrb record");
    Globals result;
    for(std::size_t i=0;i<fields.size();i+=3){
        if(result.variables_.size()>=1024)throw Error("too many LSCR global variables");
        Variable variable{fields[i+1],type(std::uint32_t(decimal(fields[i]))),Value{}};
        switch(variable.storage){
        case Type::text:variable.value=Value(fields[i+2].starts_with('0')?std::string{}:fields[i+2]);break;
        case Type::real:{
            float value{};const auto& field=fields[i+2];
            // NDK r27's libc++ has no floating-point from_chars. This path runs
            // only while loading globals, never in the script execution loop.
            // The classic locale keeps the original dot decimal separator.
            std::istringstream input(field);input.imbue(std::locale::classic());
            input>>std::noskipws>>value;
            if(input.fail() || field.starts_with('+'))throw Error("invalid LSCR real initializer");
            variable.value=Value(value);break;
        }
        default:variable.value=Value(decimal(fields[i+2]));break;
        }
        result.variables_.push_back(std::move(variable));
        result.set(std::uint32_t(result.variables_.size()-1),result.variables_.back().value);
    }
    return result;
}
const Value& Globals::get(std::uint32_t index) const {
    if(index>=variables_.size())throw Error("LSCR global index out of range");
    return variables_[index].value;
}
void Globals::set(std::uint32_t index,const Value& value) {
    if(index>=variables_.size())throw Error("LSCR global index out of range");
    auto& variable=variables_[index];
    switch(variable.storage){
    case Type::byte:variable.value=Value(std::int32_t(std::uint8_t(value.integer())));break;
    case Type::short_integer:variable.value=Value(std::int32_t(std::bit_cast<std::int16_t>(std::uint16_t(value.integer()))));break;
    case Type::integer:variable.value=Value(value.integer());break;
    case Type::real:variable.value=Value(value.real());break;
    case Type::text:variable.value=Value(value.text());break;
    }
}
void Functions::load(std::span<const std::uint8_t> bytes) {
    std::uint32_t index=u32(bytes,0);std::size_t at=4;
    // Parse into temporary storage: malformed additions must not half-update
    // the catalogue already used by a running scene.
    auto parsed=functions_;
    while(at<bytes.size()){
        if(index>=512 || bytes.size()-at<120)throw Error("invalid LSCR function catalogue");
        const auto record=bytes.subspan(at,120);const auto count=record[32];
        if(count>16)throw Error("too many LSCR command parameters");
        const auto name_end=std::find(record.begin(),record.begin()+32,std::uint8_t(0));
        Function function;function.index=index;function.name=bytes_string(record.first(std::size_t(name_end-record.begin())));
        function.result=type(u32(record,36));at+=120;
        for(unsigned n=0;n<count;++n){
            const auto parameter=record[40+n];
            if(parameter<1 || parameter>7)throw Error("invalid LSCR command parameter type");
            function.parameters.push_back(parameter);
            const auto end=std::find(bytes.begin()+std::ptrdiff_t(at),bytes.end(),std::uint8_t(','));
            if(end==bytes.end())throw Error("truncated LSCR default parameter");
            function.default_text.push_back(bytes_string(bytes.subspan(at,std::size_t(end-bytes.begin())-at)));
            at=std::size_t(end-bytes.begin())+1;
        }
        if(parsed.size()<=index)parsed.resize(std::size_t(index)+1);
        parsed[index++]=std::move(function);
    }
    functions_=std::move(parsed);
}
const Function& Functions::at(std::uint32_t index) const {
    if(index>=functions_.size() || !functions_[index])throw Error("undefined LSCR command "+std::to_string(index));
    return *functions_[index];
}
Program Program::parse(std::string name,std::vector<std::uint8_t> binary,std::span<const std::uint8_t> text) {
    if(binary.size()<12 || !std::equal(binary.begin(),binary.begin()+4,"LSCR"))throw Error("invalid LSCR header");
    const auto count=u32(binary,8);
    if(count>256 || count>(binary.size()-12)/8)throw Error("invalid LSCR label table");
    Program program;program.name_=std::move(name);program.version_=u32(binary,4);
    for(std::uint32_t i=0;i<count;++i){
        const auto id=signed_word(u32(binary,12+i*8));const auto offset=u32(binary,16+i*8);
        if(offset>=binary.size() || offset%4 || offset<12+count*8)throw Error("invalid LSCR label offset");
        program.labels_.push_back({id,offset});
    }
    program.strings_=split(text,",",true,true);
    if(program.strings_.size()>4196)throw Error("too many LSCR text entries");
    program.binary_=std::move(binary);return program;
}
std::uint32_t Program::label(std::int32_t id) const {
    // Retain physical order and the first matching label, as the game does.
    for(const auto& item:labels_)if(item.id==id)return item.offset;
    throw Error("missing LSCR label "+std::to_string(id));
}
std::uint32_t Program::word(std::uint32_t offset) const {if(offset%4)throw Error("unaligned LSCR offset");return u32(binary_,offset);}
std::vector<std::uint8_t> Library::member(std::string_view name) const {
    const auto index=archive_.find_first(name);
    if(index==resource::Archive::missing)throw Error("missing script resource "+std::string(name));
    return archive_.load(index,16u*1024*1024);
}
Library::Library(resource::Archive archive):archive_(std::move(archive)) {
    globals_=Globals::parse(member("Global.vrb"));
    functions_.load(member("LFScriptFunc.fnc"));
    functions_.load(member("LFScriptFuncEx.fnc"));
}
std::shared_ptr<const Program> Library::load(std::string_view scene) const {
    if(scene.empty() || scene.size()>240 || scene.find_first_of("/\\")!=std::string_view::npos || scene.find('\0')!=std::string_view::npos)
        throw Error("invalid script scene name");
    const auto text=member(std::string(scene)+".txt");
    return std::make_shared<const Program>(Program::parse(std::string(scene),member(std::string(scene)+".bnr"),text));
}
Runtime::Runtime(std::shared_ptr<const Program> program,const Functions& functions,Globals& globals)
    :program_(std::move(program)),functions_(functions),globals_(globals){if(!program_)throw Error("missing LSCR program");}
bool Runtime::start(std::int32_t label,std::int32_t caller) {
    std::uint32_t offset{};
    try{offset=program_->label(label);}catch(const Error&){return false;}
    offset_=offset;caller_=caller;depth_=flow_depth_=0;pending_.reset();
    stack_={};flows_={};integers_.fill(0);reals_.fill(0);running_=true;operations_=0;return true;
}
std::uint32_t Runtime::read(){const auto result=program_->word(offset_);offset_+=4;return result;}
void Runtime::jump(std::uint32_t offset){program_->word(offset);offset_=offset;}
void Runtime::push(Slot value){if(depth_>=255)throw Error("LSCR value stack overflow");stack_[++depth_]=std::move(value);}
void Runtime::push_value(Value value){Slot slot;slot.kind=5;slot.value=std::move(value);push(std::move(slot));}
Value Runtime::pop() {
    if(!depth_)return Value{};
    auto& slot=stack_[depth_--];
    switch(slot.kind){
    case 1:return globals_.get(slot.reference);
    case 2:{
        if(slot.local_index<0)return Value{};
        if(slot.reference>=52 || slot.local_index>=16)throw Error("LSCR local index out of range");
        const auto at=(slot.reference%26)*16+std::uint32_t(slot.local_index);
        return slot.reference/26?Value(reals_[at]):Value(integers_[at]);
    }
    case 3:if(slot.reference>=program_->strings().size())throw Error("LSCR text index out of range");return Value(program_->strings()[slot.reference]);
    case 5:{auto value=std::move(slot.value);slot=Slot{};return value;}
    default:throw Error("invalid LSCR value stack slot");
    }
}
void Runtime::assign(Value value,bool restore_reference) {
    if(restore_reference){if(depth_>=255)throw Error("LSCR value stack overflow");++depth_;}
    auto& slot=stack_[depth_];
    if(slot.kind==1){globals_.set(slot.reference,value);return;}
    if(slot.kind!=2)throw Error("LSCR assignment requires a variable reference");
    if(slot.local_index<0)return;
    if(slot.reference>=52 || slot.local_index>=16)throw Error("LSCR local index out of range");
    const auto at=(slot.reference%26)*16+std::uint32_t(slot.local_index);
    if(slot.reference/26)reals_[at]=value.real();else integers_[at]=value.integer();
}
void Runtime::expression() {
    const auto operation=read();
    if(operation==0){auto value=pop();assign(std::move(value),false);return;}
    if(operation==27){const auto index=pop();stack_[depth_].local_index=index.integer();return;}
    if(operation==30){pop();return;}
    if(operation==28 || operation==29)return;
    if(operation>=23 && operation<=26){
        const auto value=pop();if(value.type()==Type::text)throw Error("LSCR string in unary expression");
        if(operation==24){push_value(Value(std::int32_t(value.type()==Type::real?value.real()==0:value.integer()==0)));return;}
        if(operation==23){push_value(value.type()==Type::real?Value(-value.real()):Value(wrap_sub(0,value.integer())));return;}
        const auto delta=operation==25?1:-1;
        assign(value.type()==Type::real?Value(value.real()+float(delta)):Value(wrap_add(value.integer(),delta)),true);return;
    }
    if(operation>22)throw Error("unknown LSCR expression "+std::to_string(operation));
    const auto right=pop(),left=pop();const bool compound=operation>=1 && operation<=7;
    const auto op=compound?operation:operation>=16?operation-15:0;
    if(left.type()==Type::text || right.type()==Type::text){
        if(left.type()!=Type::text || right.type()!=Type::text)throw Error("mixed LSCR string expression");
        if(operation==8)push_value(Value(std::int32_t(left.text()==right.text())));
        else if(operation==15){
            // The existing CRT multibyte comparator returns a signed ordering,
            // rather than normalizing this particular string operation to bool.
            const auto relation=right.text().compare(left.text());
            push_value(Value(std::int32_t((relation>0)-(relation<0))));
        }else if(operation==16)push_value(Value(left.text()+right.text()));
        else throw Error("unsupported LSCR string expression");
        return;
    }
    const bool real=left.type()==Type::real || right.type()==Type::real;
    Value result;
    if(op){
        if(real){
            const auto a=left.real(),b=right.real();
            switch(op){case 1:result=Value(a+b);break;case 2:result=Value(a-b);break;
            case 3:result=Value(a*b);break;case 4:result=Value(a/b);break;
            default:throw Error("unsupported LSCR real arithmetic");}
        }else{
            const auto a=left.integer(),b=right.integer();
            switch(op){case 1:result=Value(wrap_add(a,b));break;case 2:result=Value(wrap_sub(a,b));break;
            case 3:result=Value(wrap_mul(a,b));break;case 4:result=Value(divide(a,b));break;
            case 5:result=Value(divide(a,b,true));break;case 6:result=Value(signed_word(std::uint32_t(a)&std::uint32_t(b)));break;
            case 7:result=Value(signed_word(std::uint32_t(a)|std::uint32_t(b)));break;default:throw Error("unknown LSCR arithmetic");}
        }
        if(compound)assign(std::move(result),true);else push_value(std::move(result));return;
    }
    bool comparison{};
    if(real){
        const auto a=left.real(),b=right.real();
        // Preserve the existing language's supported real comparisons. The
        // legacy code rejects 8, 11, 12 and 15 for real operands.
        switch(operation){case 9:comparison=b>a;break;case 10:comparison=a>b;break;
        case 13:comparison=a!=0 && b!=0;break;case 14:comparison=a!=0 || b!=0;break;
        default:throw Error("unsupported LSCR real comparison");}
    }else{
        const auto a=left.integer(),b=right.integer();
        switch(operation){case 8:comparison=a==b;break;case 9:comparison=a<b;break;
        case 10:comparison=a>b;break;case 11:comparison=a<=b;break;case 12:comparison=a>=b;break;
        case 13:comparison=a!=0 && b!=0;break;case 14:comparison=a!=0 || b!=0;break;
        case 15:comparison=a!=b;break;default:throw Error("unknown LSCR comparison");}
    }
    push_value(Value(std::int32_t(comparison)));
}
bool Runtime::control() {
    const auto operation=read();
    auto enter=[&](std::uint32_t kind)->Flow&{
        if(flow_depth_>=15)throw Error("LSCR flow stack overflow");
        auto& flow=flows_[++flow_depth_];flow=Flow{};flow.kind=kind;return flow;
    };
    auto& current=flows_[flow_depth_];
    switch(operation){
    case 0:return false;
    case 1:running_=false;return true;
    case 2:{auto& f=enter(2);f.alternate=read();f.end=read();break;}
    case 3:if(!current.condition){current.kind=3;current.alternate=read();}else jump(current.end);break;
    case 4:if(current.condition)jump(current.end);break;
    case 5:{auto& f=enter(5);f.end=read();f.alternate=read();f.again=read();f.begin=read();break;}
    case 6:{auto& f=enter(6);f.end=read();break;}
    case 7:{
        auto& f=enter(7);const auto count=read();if(count>64)throw Error("too many LSCR switch cases");
        for(std::uint32_t i=0;i<count;++i){const auto destination=read();const auto value=signed_word(read());f.cases.emplace_back(destination,value);}
        f.end=read();break;
    }
    case 16:{
        current.condition=pop().integer();if(current.kind!=7)break;
        std::uint32_t fallback{};bool found=false;
        for(const auto& [destination,value]:current.cases){
            if(value==current.condition){jump(destination);found=true;break;}
            if(value==std::numeric_limits<std::int32_t>::min())fallback=destination;
        }
        if(!found)jump(fallback?fallback:current.end);break;
    }
    case 8:case 9:break; // Existing debug-print control instructions.
    case 10:case 11:{
        std::size_t level=flow_depth_;
        for(;;){const auto kind=flows_[level].kind;
            if(kind==5 || kind==6 || (operation==10 && kind==7))break;
            if(!level)throw Error("LSCR loop control outside a loop");--level;}
        flow_depth_=level;const auto& f=flows_[level];jump(operation==10?f.end:f.kind==5?f.begin:f.alternate);break;
    }
    case 12:jump(read());break;
    case 13:current.condition=pop().integer();if(!current.condition)jump(current.alternate?current.alternate:current.end);break;
    case 14:current.condition=pop().integer();if(current.condition)jump(current.begin);else jump(current.end);break;
    case 15:current.condition=pop().integer();if(!current.condition)jump(current.end);break;
    default:throw Error("unknown LSCR flow operation "+std::to_string(operation));
    }
    return false;
}
bool Runtime::command(const Host& host) {
    if(!pending_){
        const auto index=read();const auto& function=functions_.at(index);
        Pending pending;pending.index=index;pending.arguments.resize(function.parameters.size());
        for(std::size_t n=function.parameters.size();n>0;--n){
            auto value=pop();const auto parameter=function.parameters[n-1];
            if(parameter==1 || parameter==2 || parameter==3 || parameter==7)value=Value(value.integer());
            else if(parameter==4)value=Value(value.real());
            else if(parameter==5){
                if(value.type()!=Type::text)throw Error("LSCR command requires a string");
                if(value.uses_default_text())value=Value(function.default_text[n-1]);
            }
            pending.arguments[n-1]=std::move(value);
        }
        pending_=std::move(pending);
    }
    if(!host)throw Error("missing native LSCR command handler");
    auto& pending=*pending_;Command call{pending.index,pending.arguments,pending.continuation,caller_};
    const auto result=host(call);pending.continuation=call.continuation;
    if(!result.complete)return true;
    const auto& function=functions_.at(pending.index);
    Value value=result.value.value_or(function.result==Type::real?Value(0.0f):function.result==Type::text?Value(std::string{}):Value{});
    if(function.result==Type::real)value=Value(value.real());
    else if(function.result==Type::text)value=Value(value.text());
    else value=Value(value.integer());
    pending_.reset();push_value(std::move(value));return result.yield_frame;
}
void Runtime::close_flows() {
    while(flow_depth_ && flows_[flow_depth_].kind>=2 && flows_[flow_depth_].kind<=7 && flows_[flow_depth_].end==offset_)
        flows_[flow_depth_--]=Flow{};
}
RunState Runtime::advance(const Host& host,std::uint32_t maximum_operations) {
    if(!running_)return RunState::stopped;
    if(!maximum_operations)throw Error("zero LSCR operation slice");
    for(std::uint32_t n=0;n<maximum_operations;++n){
        bool yield=false;
        if(pending_)yield=command(host);
        else {
            const auto kind=read();
            switch(kind){
            case 0:yield=control();break;
            case 1:case 2:case 3:{Slot slot;slot.kind=kind;slot.reference=read();push(std::move(slot));break;}
            case 4:yield=command(host);break;
            case 5:{
                const auto value_type=read(),word=read();
                if(value_type==5){if(word!=0xffffffffu)throw Error("invalid LSCR string constant");push_value(Value::default_text());}
                else if(value_type==4)push_value(Value(std::bit_cast<float>(word)));
                else if(value_type>=1 && value_type<=3)push_value(Value(signed_word(word)));
                else throw Error("invalid LSCR constant type");
                break;
            }
            case 6:expression();break;
            default:throw Error("unknown LSCR instruction "+std::to_string(kind));
            }
        }
        ++operations_;close_flows();
        if(!running_)return RunState::stopped;
        if(yield)return RunState::yielded;
    }
    return RunState::slice_exhausted;
}
}
