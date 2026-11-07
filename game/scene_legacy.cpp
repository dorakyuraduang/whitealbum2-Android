#include "scene.hpp"
#include "legacy_save.hpp"
#include "script_legacy.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <set>

namespace wa2::game {
namespace {
// These offsets describe a serialized save format, never an address space or
// an object layout used by the running game. No source object is constructed.
class LegacyFields {
    std::span<const std::uint8_t> bytes_;
public:
    explicit LegacyFields(std::span<const std::uint8_t> bytes):bytes_(bytes){}
    unsigned byte(std::size_t at)const{if(at>=bytes_.size())throw script::Error("Truncated original scene byte");return bytes_[at];}
    std::int32_t signed_byte(std::size_t at)const{return std::bit_cast<std::int8_t>(std::uint8_t(byte(at)));}
    unsigned word(std::size_t at)const{return byte(at)|(byte(at+1)<<8);}
    std::int32_t signed_word(std::size_t at)const{return std::bit_cast<std::int16_t>(std::uint16_t(word(at)));}
    std::uint32_t dword(std::size_t at)const{return word(at)|(word(at+2)<<16);}
    std::int32_t integer(std::size_t at)const{return std::bit_cast<std::int32_t>(dword(at));}
    float real(std::size_t at)const{const auto value=std::bit_cast<float>(dword(at));if(!std::isfinite(value))throw script::Error("Nonfinite original scene coordinate");return value;}
    std::string text(std::size_t at,std::size_t count)const{
        if(at>bytes_.size() || count>bytes_.size()-at)throw script::Error("Truncated original scene text");const auto field=bytes_.subspan(at,count);const auto end=std::find(field.begin(),field.end(),std::uint8_t(0));
        if(end==field.end())throw script::Error("Unterminated original scene text");return {reinterpret_cast<const char*>(field.data()),std::size_t(end-field.begin())};
    }
};
template<class Controller> void apply(Controller& controller,unsigned index,std::initializer_list<script::Value> values){
    script::Command command{index,values};if(!controller.dispatch(command))throw script::Error("Missing native scene reconstruction operation");
}
}
std::unique_ptr<Scene> Scene::restore_legacy(resource::BankOpener opener,std::shared_ptr<progress::Store> progress,std::span<const std::uint8_t> bytes,std::int64_t now,const SavedImageResolver& saved_images,const Preferences& preferences,SceneServices services,std::uint32_t seed){
    const auto header=inspect_legacy_save_header(bytes);if(header.version!=2)throw script::Unsupported("Original version-1 manual scene conversion is not established");
    constexpr std::size_t prefix=0x134d4,scene_offset=0x110a0;
    if(bytes.size()<=prefix || bytes.size()>checkpoint::maximum_bytes || now<0 || !progress || !saved_images)throw script::Error("Invalid original manual scene size, storage or restore services");preferences.validate();
    const auto record=bytes.subspan(scene_offset,original_scene_v2_bytes);const LegacyFields data(record);const auto audio=import_legacy_scene_audio(record);
    const auto raw=data.byte(0x210)?data.text(0x222,1024):std::string{};
    auto next=std::make_unique<Scene>(std::move(opener),seed,std::move(progress),std::move(services));next->preferences(preferences);auto resolve=saved_images(next->resources_);if(!resolve)throw script::Error("Missing original scene image resolver");
    const auto translated=script::import_legacy_script(bytes.subspan(prefix),next->library_.functions(),next->library_.globals(),[&](auto name){return next->library_.load(name);},
        [&](const auto&,unsigned command,unsigned parameter)->std::optional<std::string>{if((command==130 || command==131) && parameter==0 && data.byte(0x210))return raw;return {};});
    if(translated.consumed!=bytes.size()-prefix)throw script::Error("Unexpected trailing data in original manual scene");
    if(translated.pending_command && *translated.pending_command!=130 && *translated.pending_command!=131 && *translated.pending_command!=134 && *translated.pending_command!=209)
        throw script::Unsupported("Original manual scene has an unrecoverable pending command: "+std::to_string(*translated.pending_command));
    next->session_.restore(translated.checkpoint);if(!next->session_.active() || !next->session_.active()->running())throw script::Error("Original save has no running scene");
    next->timeline_.rebase(std::int64_t(data.dword(0xef4))*1000,now);next->milliseconds_=next->timeline_.milliseconds();next->statistics_.scene=next->session_.active()->program().name();next->statistics_.command=translated.pending_command.value_or(0);
    // Use current preferences and fresh automatic/timer state. The source load
    // restores neither a skip latch nor a serialized source wait counter.
    checkpoint::Writer control;for(auto value:{data.integer(0x4c),data.integer(0x54),data.integer(0x58),0,data.integer(0x110c),data.integer(0x1114),0,0,preferences.auto_ticks})control.i32(value);
    control.string(data.text(0x7c,32));control.string(data.text(0x9c,32));for(unsigned n=0;n<3;++n)control.boolean(false);for(unsigned n=0;n<4;++n)control.u32(0);control.i32(data.integer(0xef0));next->controls_.restore(control.seal("LSCT"));
    next->controls_.automatic_palette(data.text(0x5c,32));
    next->reading_.enter(next->statistics_.scene);const auto reading_bytes=next->reading_.checkpoint();auto reading=checkpoint::Reader::open("LSRH",reading_bytes);auto reading_scene=reading.string(240);const auto reading_index=reading.u32();checkpoint::Writer position;position.string(reading_scene);position.u32(reading_index);position.i32(data.signed_word(0x220));position.i32(0);next->reading_.restore(position.seal("LSRH"));
    // Resolve immutable pixels through this final owner's catalogue. Temporary
    // visual controllers have no gallery/reading callbacks and never run LSCR.
    const auto load=[&](auto group,auto name,int chroma=256,std::string_view map={}){auto request=next->resources_.image(group,name,chroma,map);image::resolve_reference(next->resources_.reference(request),next->resources_,resolve);if(!request->get())throw resource::Error("Original scene resolver left an image pending");return request;};
    const auto& modes=next->controls_.modes();
    if(data.dword(0x34)){
        const auto identity=data.integer(0x18),kind=data.integer(0x2c);if(identity<0 || identity>(kind?999999:99999) || kind<0 || kind>2 || modes.time<0 || modes.time>9)throw script::Unsupported("Invalid original saved background identity");
        std::array<char,32> name{};if(kind)std::snprintf(name.data(),name.size(),"%c%06d.tga",kind==1?'v':'h',int(identity));else std::snprintf(name.data(),name.size(),"B%04d%d%d.tga",int(identity/10),int(identity%10),int(modes.time));
        const auto request=load(kind?"grp":"bak",name.data(),modes.background_chroma,modes.background_effect);auto raster=request->get();script::CameraPose pose;
        // The source loader passes its stored pan to the ordinary setter, which
        // adds the ordinary-image centering offset again. CGs use direct pan.
        pose.x=float(data.integer(0x1c))+float(kind?0:(1280-std::int64_t(raster->width))/2);pose.y=float(data.integer(0x20))+float(kind?0:(720-std::int64_t(raster->height))/2);
        pose.scale_x=data.real(0x24);pose.scale_y=data.real(0x28);pose.angle=float(data.integer(0x44));next->backgrounds_.restore_still(std::move(raster),pose,kind!=0);
    }
    std::set<int> character_ids;
    for(unsigned n=0;n<8;++n)if(data.dword(0xc0+n*4)){
        const auto id=data.integer(0xe0+n*4);if(!character_ids.insert(id).second)throw script::Error("Duplicate original saved character identity");const auto placement=data.word(0x120+n*2)?script::Value(data.real(0x150+n*4)):script::Value(data.integer(0x130+n*4));
        // Native set/update is typed resource reconstruction, not replay of the
        // suspended source command; neither progression nor text is executed.
        apply(next->characters_,155,{script::Value(id),script::Value(data.integer(0x100+n*4)),placement,script::Value(data.integer(0x170+n*4)),script::Value(data.integer(0x190+n*4)),script::Value(data.integer(0x1b0+n*4))});
    }
    // Resolve the ordinary async character requests before publishing this
    // candidate, without ticking a story or a device.
    next->characters_.prepare_restored_assets(next->resources_,resolve);next->characters_.update([](auto){return 0;});
    script::SceneBitmaps bitmaps([&](auto name,auto group,auto){return load(group,name);},[&](auto name,auto group){return image::Animation::parse(next->resources_.data(group,name));});
    for(unsigned n=0;n<32;++n){const auto at=0x111c+n*0x98;if(!data.byte(at+0x4c))continue;const auto type=data.byte(at+0x4e);const auto layer=data.signed_byte(at+0x71);const auto name=data.text(at,32),palette=data.text(at+0x24,32),group=data.text(at+0x44,8);const auto independent=data.signed_byte(at+0x72),count=data.signed_word(at+0x58);
        if(type==0)apply(bitmaps,176,{script::Value(int(n)),script::Value(name),script::Value(layer),script::Value(data.integer(at+0x20)),script::Value(palette),script::Value(count),script::Value(group),script::Value(independent)});
        else if(type==1)apply(bitmaps,177,{script::Value(int(n)),script::Value(name),script::Value(layer),script::Value(palette),script::Value(count),script::Value(group),script::Value(independent)});
        else throw script::Unsupported("Original saved bitmap kind "+std::to_string(type));
        bitmaps.update();if(bitmaps.loading())throw resource::Error("Original saved bitmap remains pending");
        apply(bitmaps,183,{script::Value(int(n)),script::Value(int(data.byte(at+0x4d)))});
        apply(bitmaps,185,{script::Value(int(n)),script::Value(data.integer(at+0x50)),script::Value(data.signed_word(at+0x5e)),script::Value(0)});
        apply(bitmaps,186,{script::Value(int(n)),script::Value(data.integer(at+0x54))});apply(bitmaps,187,{script::Value(int(n)),script::Value(int(data.byte(at+0x5a))),script::Value(int(data.byte(at+0x5b))),script::Value(int(data.byte(at+0x5c)))});
        apply(bitmaps,189,{script::Value(int(n)),script::Value(data.signed_word(at+0x78)),script::Value(data.signed_word(at+0x7a)),script::Value(data.signed_word(at+0x80)),script::Value(data.signed_word(at+0x82)),script::Value(data.signed_word(at+0x7c)),script::Value(data.signed_word(at+0x7e))});
        const auto scale=data.real(at+0x88);if(scale)apply(bitmaps,191,{script::Value(int(n)),script::Value(data.signed_word(at+0x90)),script::Value(data.signed_word(at+0x92)),script::Value(scale)});
        // The zero-scale source branch passes its crop size again, even when
        // the saved destination size differs. The original manual loader also
        // omits its saved rotation field. Preserve these load semantics here.
        // Ordinary native checkpoints continue preserving rotation exactly.
    }
    next->bitmaps_.restore(bitmaps.checkpoint(next->resources_),next->resources_,resolve);
    for(unsigned at:{0x1100u,0x1104u,0x1108u})if(data.integer(at)<0 || data.integer(at)>255)throw script::Error("Original saved world colour exceeds its range");
    apply(next->colour_,153,{script::Value(0),script::Value(data.integer(0x1100)),script::Value(data.integer(0x1104)),script::Value(data.integer(0x1108))});next->colour_.update();
    if(data.dword(0))apply(next->shake_,150,{script::Value(data.integer(0xc)),script::Value(data.integer(0x8)),script::Value(0),script::Value(data.integer(0x4)),script::Value(data.integer(0x14))});
    const bool choices=data.dword(0x668)!=0;if(choices!=(translated.pending_command==209))throw script::Unsupported("Original saved choice owner does not match its suspended command");
    if(choices){const auto count=data.integer(0x670);if(count<1 || count>8)throw script::Error("Original saved choice count exceeds eight");const auto choice_scene=std::to_string(data.integer(0xed0));script::SceneChoices temporary(next->progress_store_,[&]()->std::string_view{return choice_scene;});
        for(int n=0;n<count;++n)apply(temporary,208,{script::Value(data.text(0x674+n*256,256)),script::Value(data.signed_byte(0xeb4+n)),script::Value(data.signed_byte(0xebc+n)),script::Value(data.signed_byte(0xec4+n))});
        apply(temporary,209,{script::Value(data.integer(0xecc))});next->choices_.restore(temporary.checkpoint());next->selection_.sync(next->choices_);
    }
    if(data.dword(0xed4)){
        // Source saves settings rather than individual particles, so load starts
        // a newly prefilled effect attached to its independent scope.
        script::SceneWeather weather([&](auto name){return load("grp",name);},[&]{return next->random_.next();},[&](auto name){return image::Animation::parse(next->resources_.data("grp",name));});
        apply(weather,229,{script::Value(data.integer(0xed8)|0x100),script::Value(data.integer(0xedc)),script::Value(data.integer(0xee0)),script::Value(data.integer(0xee4)),script::Value(data.integer(0xee8)),script::Value(2),script::Value(data.integer(0xeec))});weather.update();if(weather.loading())throw resource::Error("Original saved weather remains pending");next->weather_.restore(weather.checkpoint(next->resources_),next->resources_,resolve);
    }
    const auto year=data.integer(0x2424),month=data.integer(0x2428),day=data.integer(0x242c),weekday=data.integer(0x2430);if(year || month || day || weekday)apply(next->calendar_,203,{script::Value(year),script::Value(month),script::Value(day),script::Value(weekday),script::Value(0)});
    script::DialogueContent dialogue;dialogue.open=data.byte(0x210)!=0;dialogue.picture=data.byte(0x218)!=0;dialogue.novel=data.integer(0x664)!=0;dialogue.line=data.signed_word(0x220);
    if(dialogue.open){dialogue.raw=raw;dialogue.page=data.signed_byte(0x21a);dialogue.budget=data.signed_word(0x21c);if(data.word(0x622) && !dialogue.novel){dialogue.name=data.text(0x624,64);dialogue.speaker=data.signed_byte(0x212);}}
    if(translated.pending_command && (*translated.pending_command==130 || *translated.pending_command==131 || *translated.pending_command==134) && !dialogue.open)throw script::Error("Original saved message command has no open dialogue");
    next->dialogue_.restore_content(dialogue);next->dialogue_.prepare_restored_assets(next->resources_,resolve);
    next->captions_.reset();next->voices_.adult_mode(modes.adult_mode!=0);next->music_.restore(audio.music);next->effects_.restore(audio.effects);next->voices_.restore(audio.voices);
    // No device owns this mixer yet. The application's ordinary transactional
    // replacement path publishes this candidate only after every stage passes.
    return next;
}
}
