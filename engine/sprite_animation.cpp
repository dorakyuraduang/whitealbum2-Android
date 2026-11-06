#include "sprite_animation.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>
#include <cstring>

namespace wa2::image {
namespace {
class Reader {
    std::span<const std::uint8_t> bytes_;
    std::size_t at_{};
public:
    explicit Reader(std::span<const std::uint8_t> bytes):bytes_(bytes){}
    auto take(std::size_t n){if(n>bytes_.size()-at_)throw Error("Truncated INAS sprite animation");auto out=bytes_.subspan(at_,n);at_+=n;return out;}
    static std::uint16_t half(std::span<const std::uint8_t> b,unsigned at){return std::uint16_t(unsigned(b[at])|(unsigned(b[at+1])<<8));}
    static std::uint32_t word(std::span<const std::uint8_t> b,unsigned at){return unsigned(half(b,at))|(unsigned(half(b,at+2))<<16);}
    std::string name(){std::string out;for(unsigned n=0;n<256;++n){const auto c=take(1)[0];if(!c){if(out.empty())throw Error("Empty INAS texture name");return out;}if(c=='/' || c=='\\')throw Error("INAS texture name must be an archive member");out.push_back(char(c));}throw Error("INAS texture name exceeds limit");}
    void finish()const{if(at_!=bytes_.size())throw Error("Trailing INAS sprite animation bytes");}
};
void neutral(std::span<const std::uint8_t> bytes){
    if(bytes[0]!=1 || bytes[1]!=128 || bytes[2]!=128 || bytes[3]!=128)throw Error("Unsupported INAS active/colour transform");
}
}
std::shared_ptr<const Animation> Animation::parse(std::span<const std::uint8_t> bytes){
    if(bytes.size()>32u*1024*1024)throw Error("INAS animation exceeds size limit");Reader input(bytes);const auto head=input.take(36);
    if(std::memcmp(head.data(),"INAS",4))throw Error("Invalid INAS sprite animation header");
    if(head[16]!=1)throw Error("Unsupported inactive INAS resource");
    auto result=std::make_shared<Animation>();result->rate_=Reader::word(head,20);
    const auto frames=Reader::word(head,24),clips=Reader::word(head,28),textures=Reader::word(head,32);
    if(!result->rate_ || result->rate_>1000 || !frames || frames>4096 || !clips || clips>256 || !textures || textures>32)throw Error("INAS animation catalogue exceeds limits");
    result->frames_.reserve(frames);std::size_t total_pieces{};
    for(unsigned n=0;n<frames;++n){const auto frame=input.take(24);neutral(frame);
        if(Reader::word(frame,4) || Reader::word(frame,8) || frame[12] || Reader::half(frame,14) || Reader::word(frame,16))throw Error("Unsupported INAS group transform");
        const auto count=Reader::word(frame,20);if(count>256 || total_pieces+count>65536)throw Error("INAS piece catalogue exceeds limit");total_pieces+=count;
        AnimationFrame group;group.pieces.reserve(count);
        for(unsigned p=0;p<count;++p){const auto piece=input.take(40);neutral(piece);
            // Uninitialized padding in the source files is deliberately ignored.
            if(piece[4]>=textures || Reader::half(piece,6) || Reader::half(piece,8) || Reader::word(piece,12) || piece[16] || Reader::half(piece,18) ||
               std::bit_cast<std::int32_t>(Reader::word(piece,20))!=-2 || Reader::word(piece,36))throw Error("Unsupported INAS piece transform/blend");
            AnimationPiece out;out.texture=piece[4];
            // Assign named fields rather than depending on a C++ struct layout.
            const auto value=[&](unsigned at){return std::bit_cast<std::int16_t>(Reader::half(piece,at));};
            out.source_x=value(24);out.source_y=value(26);out.width=value(28);out.height=value(30);out.x=value(32);out.y=value(34);
            if(out.source_x<0 || out.source_y<0 || out.width<0 || out.height<0)throw Error("Invalid INAS source rectangle");group.pieces.push_back(out);
        }result->frames_.push_back(std::move(group));
    }
    for(unsigned n=0;n<clips;++n){const auto header=input.take(8);const auto count=Reader::word(header,4);
        if(header[0]!=1 || count>65535)throw Error("Invalid INAS sequence header");AnimationClip clip;clip.steps.reserve(count+1);
        struct Loop {bool timed{};};std::vector<Loop> loops;bool timed=false;
        for(unsigned j=0;j<=count;++j){const auto raw=input.take(8);AnimationStep step{Reader::half(raw,0),Reader::half(raw,2),Reader::half(raw,4),Reader::half(raw,6)};
            if(step.operation>3 || step.event)throw Error("Unsupported INAS sequence operation/event");
            switch(step.operation){
            case 0:if(j!=count || step.argument || step.duration || !loops.empty())throw Error("Invalid INAS sequence terminator");break;
            case 1:if(j==count || step.argument>=frames || !step.duration)throw Error("Invalid or zero-duration INAS frame");timed=true;for(auto& loop:loops)loop.timed=true;break;
            // The source loop counter uses the high half of the first word.
            // The second word's low half is ignored for this operation; real
            // files retain editor values there even for continuous loops.
            case 2:if(j==count || step.argument>254 || loops.size()>=16)throw Error("Invalid INAS sequence loop");loops.push_back({});break;
            case 3:if(j==count || loops.empty() || step.argument || step.duration || !loops.back().timed)throw Error("INAS loop has no timed frame or matching start");loops.pop_back();break;
            }clip.steps.push_back(step);
        }
        if(clip.steps.back().operation!=0 || !timed)throw Error("INAS sequence has no finite timing boundary");result->clips_.push_back(std::move(clip));
    }
    for(unsigned n=0;n<textures;++n)result->textures_.push_back(input.name());input.finish();
    std::uint64_t fingerprint=14695981039346656037ull;for(auto byte:bytes){fingerprint^=byte;fingerprint*=1099511628211ull;}result->fingerprint_=fingerprint;
    return result;
}
AnimationPlayer::AnimationPlayer(std::shared_ptr<const Animation> data,std::uint32_t clip,std::uint32_t repeat,bool hold)
    :data_(std::move(data)),clip_(clip),repeat_(repeat),remaining_(repeat),hold_(hold){
    if(!data_ || clip>=data_->clips().size() || repeat>1000000)throw Error("Invalid native animation playback parameters");loops_.reserve(16);
}
void AnimationPlayer::advance(){
    const auto& steps=data_->clips()[clip_].steps;
    for(unsigned operations=0;operations<1024;++operations){
        if(cursor_>=steps.size())throw Error("Native animation sequence position out of range");const auto& step=steps[cursor_];
        switch(step.operation){
        case 0:
            if(remaining_==1){finished_=true;if(!hold_)frame_.reset();return;}
            if(remaining_>1)--remaining_;cursor_=elapsed_=0;loops_.clear();break;
        case 1:if(elapsed_<step.duration){++elapsed_;frame_=step.argument;return;}elapsed_=0;++cursor_;break;
        case 2:if(loops_.size()>=16)throw Error("Native animation loop stack overflow");loops_.push_back({cursor_,step.argument?unsigned(step.argument)+1:0});++cursor_;break;
        case 3:
            if(loops_.empty())throw Error("Native animation loop stack underflow");
            if(!loops_.back().remaining || --loops_.back().remaining)cursor_=loops_.back().start+1;
            else{loops_.pop_back();++cursor_;}break;
        default:throw Error("Unsupported native animation sequence operation");
        }
    }throw Error("Native animation exceeded control-step limit");
}
void AnimationPlayer::tick(std::uint32_t logic_rate){
    if(!logic_rate || logic_rate>1000 || (clock_rate_ && clock_rate_!=logic_rate))throw Error("Invalid or changed native animation clock rate");
    clock_rate_=logic_rate;if(finished_)return;fraction_+=data_->rate();while(fraction_>=logic_rate && !finished_){fraction_-=logic_rate;advance();}if(finished_)fraction_=0;
}
std::vector<std::uint8_t> AnimationPlayer::checkpoint()const{
    checkpoint::Writer out;out.u64(data_->fingerprint());for(auto value:{clip_,repeat_,remaining_,cursor_,elapsed_,fraction_,clock_rate_})out.u32(value);
    out.boolean(hold_);out.boolean(finished_);out.boolean(frame_.has_value());if(frame_)out.u32(*frame_);out.u32(std::uint32_t(loops_.size()));for(const auto& loop:loops_){out.u32(loop.start);out.u32(loop.remaining);}return out.seal("LSAN");
}
void AnimationPlayer::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSAN",bytes);if(in.u64()!=data_->fingerprint())throw Error("Saved native animation resource mismatch");
    const auto clip=in.u32(),repeat=in.u32(),remaining=in.u32(),cursor=in.u32(),elapsed=in.u32(),fraction=in.u32(),clock=in.u32();
    const auto hold=in.boolean(),finished=in.boolean(),has_frame=in.boolean();std::optional<std::uint32_t> frame;if(has_frame)frame=in.u32();const auto count=in.u32();
    if(count>16 || clip!=clip_ || repeat!=repeat_ || hold!=hold_ || remaining>repeat || (repeat && !remaining) || cursor>=data_->clips()[clip_].steps.size() ||
       clock>1000 || fraction>=(clock?clock:1) || (frame && *frame>=data_->frames().size()) || elapsed>65535)throw Error("Invalid saved native animation state");
    std::vector<Loop> loops;const auto& steps=data_->clips()[clip_].steps;
    for(unsigned n=0;n<count;++n){Loop loop{in.u32(),in.u32()};if(loop.start>=steps.size() || steps[loop.start].operation!=2 || loop.remaining>unsigned(steps[loop.start].argument)+1 || ((!steps[loop.start].argument)!=(!loop.remaining)) ||
            loop.start>=cursor || (!loops.empty() && loop.start<=loops.back().start))throw Error("Invalid saved native animation loop");loops.push_back(loop);}
    in.finish();if(finished && cursor<steps.size() && steps[cursor].operation!=0)throw Error("Invalid finished native animation state");
    remaining_=remaining;cursor_=cursor;elapsed_=elapsed;fraction_=fraction;clock_rate_=clock;finished_=finished;frame_=frame;loops_=std::move(loops);
}
}
