#include "progress.hpp"
#include "test_progress.hpp"
#include <algorithm>
#include <bit>

namespace wa2::progress {
namespace {
unsigned slot(Block block){const auto n=unsigned(block);if(n>=3)throw Error("Unknown progress block");return n;}
void flag_index(std::uint32_t index){if(index>=layout::flag_count)throw Error("Progress flag index out of range");}
std::size_t gallery_index(std::uint32_t bank,std::uint32_t index){
    if(bank>=2 || index>=layout::gallery_bank_bytes)throw Error("Progress gallery index out of range");
    return bank*layout::gallery_bank_bytes+index;
}
std::size_t history_index(std::uint32_t script,std::uint32_t line){
    if(script>=layout::script_count || line>=layout::lines_per_script)throw Error("Progress reading-history index out of range");
    return script*(layout::lines_per_script/8)+line/8;
}
}
std::size_t offset(Block block){switch(block){
    case Block::history:return 0;case Block::gallery:return layout::read_history_bytes;
    case Block::flags:return layout::system_flags_offset;default:throw Error("Unknown progress block");
}}
std::size_t size(Block block){switch(block){
    case Block::history:return layout::read_history_bytes;case Block::gallery:return layout::gallery_bytes;
    case Block::flags:return layout::system_flags_bytes;default:throw Error("Unknown progress block");
}}
State::State():history_(layout::read_history_bytes),gallery_(layout::gallery_bytes){}
State State::decode(std::span<const std::uint8_t> bytes){
    if(bytes.size()!=layout::system_save_bytes)throw Error("Unsupported Sys.sav length");
    State result;
    std::copy_n(bytes.begin(),result.history_.size(),result.history_.begin());
    std::copy_n(bytes.begin()+layout::read_history_bytes,result.gallery_.size(),result.gallery_.begin());
    std::copy_n(bytes.begin()+layout::system_flags_offset,result.flags_.size(),result.flags_.begin());
    result.dirty_.fill(false);return result;
}
Bytes State::encode()const{
    Bytes result;result.reserve(layout::system_save_bytes);
    for(auto kind:{Block::history,Block::gallery,Block::flags}){const auto data=block(kind);result.insert(result.end(),data.begin(),data.end());}
    return result;
}
std::span<const std::uint8_t> State::block(Block kind)const{switch(kind){
    case Block::history:return history_;case Block::gallery:return gallery_;case Block::flags:return flags_;
    default:throw Error("Unknown progress block");
}}
bool State::dirty(Block kind)const{return dirty_[slot(kind)];}
void State::clean(Block kind){dirty_[slot(kind)]=false;}
std::int32_t State::flag(std::uint32_t index)const{
    flag_index(index);std::uint32_t value{};
    for(unsigned n=0;n<4;++n)value|=std::uint32_t(flags_[index*4+n])<<(n*8);
    return std::bit_cast<std::int32_t>(value);
}
void State::set_flag(std::uint32_t index,std::int32_t value){
    flag_index(index);
    // The cached original setter reserves 99 for the choice system and
    // forces writes to zero. Decoding an existing save preserves its value.
    if(index==99)value=0;
    if(flag(index)==value)return;
    const auto bits=std::bit_cast<std::uint32_t>(value);
    for(unsigned n=0;n<4;++n)flags_[index*4+n]=std::uint8_t(bits>>(n*8));
    dirty_[slot(Block::flags)]=true;
}
std::uint8_t State::gallery(std::uint32_t bank,std::uint32_t index)const{return gallery_[gallery_index(bank,index)];}
void State::set_gallery(std::uint32_t bank,std::uint32_t index,std::uint8_t value){
    auto& saved=gallery_[gallery_index(bank,index)];if(saved==value)return;saved=value;dirty_[slot(Block::gallery)]=true;
}
bool State::read(std::uint32_t script,std::uint32_t line)const{return (history_[history_index(script,line)]&(0x80u>>(line%8)))!=0;}
void State::mark_read(std::uint32_t script,std::uint32_t line){
    auto& value=history_[history_index(script,line)];const auto mask=std::uint8_t(0x80u>>(line%8));
    if(value&mask)return;value|=mask;dirty_[slot(Block::history)]=true;
}
void State::unlock_all_for_testing(){
    auto bytes=encode();if(!testing::unlock_all_content(bytes))return;
    auto next=decode(bytes);
    for(auto kind:{Block::history,Block::gallery,Block::flags})
        next.dirty_[slot(kind)]=dirty(kind) || !std::equal(block(kind).begin(),block(kind).end(),next.block(kind).begin());
    *this=std::move(next);
}
Store::Store(std::shared_ptr<Storage> storage):storage_(std::move(storage)){
    if(!storage_)throw Error("Missing native progress storage");
    if(auto bytes=storage_->load())state_=State::decode(*bytes);
}
void Store::flush(){
    for(auto kind:{Block::history,Block::gallery,Block::flags})if(state_.dirty(kind)){
        storage_->write(kind,state_.block(kind));state_.clean(kind);
    }
}
}
