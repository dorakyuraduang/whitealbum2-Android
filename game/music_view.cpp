#include "music_view.hpp"
#include <algorithm>
#include <cmath>

namespace wa2::game {
namespace {
// Content metadata recovered from the existing cached 0x4c1dd0/0x4c1ed0
// tables. No machine addresses or recompiled functions enter the runtime.
constexpr const auto& songs=music_songs;
constexpr std::array<unsigned,27> closing_indices{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,22,23,24,25,26,27,28};
constexpr std::array<std::string_view,11> names{"sys_04000.tga","sys_09000.tga","sys_04100.tga","sys_04101.tga","sys_04010.tga","sys_04021.tga","sys_09100.tga","sys_04030.tga","sys_04031.tga","sys_00301.tga","sys_00300.tga"};
constexpr unsigned widths[]{1280,1280,400,344,304,288,56,1024,512,192,264},heights[]{40,56,128,1512,168,40,72,56,1512,8,1008};
}
MusicView::MusicView(Loader loader,Sound sound,CurrentMusic music):loader_(std::move(loader)),sound_(std::move(sound)),current_music_(std::move(music)){
    if(!loader_)throw script::Error("Missing native music-browser image loader");auto black=std::make_shared<image::Raster>();black->width=black->height=1;black->rgba={0,0,0,255};black_=std::move(black);sprites_.reserve(108);
}
void MusicView::clear(){images_={};request_.reset();rows_.clear();sprites_.clear();focus_.reset();playing_.reset();command_.reset();phase_=MusicViewPhase::idle;page_=image_next_=elapsed_=0;timing_={};close_from_=1;}
void MusicView::prepare(TitleContent content,const progress::State& progress,MenuTiming timing){
    timing.validate();
    std::array<bool,200> next{};for(unsigned n=100;n<next.size();++n)next[n]=progress.flag(n)!=0;if(next[104] && next[163])next[134]=true;
    auto request=loader_(names[0]);if(!request)throw script::Error("Missing native music-browser image request");clear();timing_=timing;content_=content;unlocked_=next;request_=std::move(request);phase_=MusicViewPhase::loading;rebuild_rows();
}
void MusicView::rebuild_rows(){
    rows_.clear();const bool closing=!content_.local_introductory && !content_.external_introductory && !page_;
    const unsigned count=closing?27:page_?32:unlocked_[134]?31:30;
    for(unsigned ordinal=0;ordinal<count;++ordinal){const auto index=closing?closing_indices[ordinal]:page_*31+ordinal;const auto original=songs.at(index);
        const bool alternate=original==21 && unlocked_[135];rows_.push_back({ordinal,index,alternate?35:original,unlocked_[100+original] || alternate});}
}
bool MusicView::images(){
    for(unsigned issued=0;image_next_<images_.size();){if(!request_){if(issued==2)return false;request_=loader_(names[image_next_]);++issued;if(!request_)throw script::Error("Missing native music-browser image request");}
        auto pixels=request_->get();if(!pixels)return false;if(pixels->width<widths[image_next_] || pixels->height<heights[image_next_])throw script::Unsupported("Native music-browser atlas dimensions are not supported: "+std::string(names[image_next_]));images_[image_next_++]=std::move(pixels);request_.reset();}return true;
}
void MusicView::turn(){
    focus_.reset();elapsed_=0;if(timing_.page)phase_=MusicViewPhase::page_out;else{page_^=1;rebuild_rows();}if(sound_)sound_(audio::SystemCue::focus);
}
float MusicView::visibility()const noexcept{
    const auto fraction=[&](unsigned duration){return duration?std::min(1.f,float(elapsed_)/duration):1.f;};
    if(phase_==MusicViewPhase::loading || phase_==MusicViewPhase::idle || phase_==MusicViewPhase::closed)return 0;
    return phase_==MusicViewPhase::opening?fraction(timing_.open):phase_==MusicViewPhase::closing?close_from_*(1-fraction(timing_.close)):1.f;
}
void MusicView::activate(unsigned id){
    if(id==50){
        if(phase_==MusicViewPhase::closing)return;
        const bool immediate=phase_==MusicViewPhase::loading || !timing_.close;close_from_=visibility();elapsed_=0;request_.reset();focus_.reset();
        phase_=immediate?MusicViewPhase::closed:MusicViewPhase::closing;command_=MusicCommand{immediate?MusicCommand::Kind::close:MusicCommand::Kind::leaving,0,0,120};
        if(immediate)sprites_.clear();if(sound_)sound_(audio::SystemCue::cancel);return;
    }
    if(id==51 || id==52){turn();return;}if(id>=rows_.size())throw script::Error("Unknown native music-browser control");const auto& row=rows_[id];
    if(!row.enabled){if(sound_)sound_(audio::SystemCue::reject);return;}
    if(playing_==row.index)command_=MusicCommand{MusicCommand::Kind::stop,row.index,row.song,60};else command_=MusicCommand{MusicCommand::Kind::play,row.index,row.song,playing_?60:0};
    if(sound_)sound_(audio::SystemCue::accept);
}
void MusicView::played(std::optional<unsigned> index){if(index && *index>=songs.size())throw script::Error("Native music-browser playback index is out of range");playing_=index;if(phase_==MusicViewPhase::active)draw();}
std::optional<MusicCommand> MusicView::take_command(){auto result=command_;command_.reset();return result;}
void MusicView::update(MusicInput input){
    const auto& selected=input.selection;if(!std::isfinite(selected.x) || !std::isfinite(selected.y) || std::abs(std::int64_t(selected.navigation))>1 || std::abs(std::int64_t(input.page_delta))>1)throw script::Error("Native music-browser input is out of range");
    if(phase_==MusicViewPhase::idle)throw script::Error("Native music browser is not prepared");if(phase_==MusicViewPhase::closed || command_)return;
    if(input.cancel && phase_!=MusicViewPhase::closing){activate(50);draw();return;}
    if(phase_==MusicViewPhase::loading){if(!images())return;elapsed_=0;phase_=timing_.open?MusicViewPhase::opening:MusicViewPhase::active;draw();return;}
    if(phase_!=MusicViewPhase::active){
        if(phase_==MusicViewPhase::opening && ++elapsed_>=timing_.open){elapsed_=0;phase_=MusicViewPhase::active;}
        else if(phase_==MusicViewPhase::page_out && ++elapsed_>=timing_.page){page_^=1;rebuild_rows();elapsed_=0;phase_=MusicViewPhase::page_in;}
        else if(phase_==MusicViewPhase::page_in && ++elapsed_>=timing_.page){elapsed_=0;phase_=MusicViewPhase::active;}
        else if(phase_==MusicViewPhase::closing && ++elapsed_>=timing_.close){phase_=MusicViewPhase::closed;command_=MusicCommand{MusicCommand::Kind::close,0,0,120};}
        draw();return;
    }
    if(input.page_delta){turn();draw();return;}const auto previous=focus_;
    if(selected.pointer){focus_.reset();for(const auto& row:rows_){const float x=40.f+400*(row.ordinal/13),y=88.f+32*(row.ordinal%13);if(row.enabled && selected.x>=x && selected.x<x+400 && selected.y>=y && selected.y<y+32){focus_=row.ordinal;break;}}
        if(selected.x>=1164 && selected.x<1220 && selected.y>=688 && selected.y<712)focus_=50;
        if(selected.y>=600 && selected.y<664){if(selected.x>=440 && selected.x<488)focus_=51;if(selected.x>=792 && selected.x<840)focus_=52;}}
    if(selected.navigation){std::vector<unsigned> controls;for(const auto& row:rows_)if(row.enabled)controls.push_back(row.ordinal);controls.insert(controls.end(),{51,52,50});const auto at=std::find(controls.begin(),controls.end(),focus_.value_or(999));const int size=int(controls.size());const auto n=at==controls.end()?(selected.navigation>0?0:size-1):(int(at-controls.begin())+selected.navigation+size)%size;focus_=controls[unsigned(n)];}
    if(focus_ && focus_!=previous && sound_)sound_(audio::SystemCue::focus);if(focus_ && (selected.confirm || (selected.pointer && selected.activate)))activate(*focus_);draw();
}
void MusicView::draw(){
    sprites_.clear();if(image_next_<images_.size() || phase_==MusicViewPhase::closed)return;int order{};
    const auto whole=visibility(),fraction=timing_.page?std::min(1.f,float(elapsed_)/timing_.page):1.f;
    const auto content=phase_==MusicViewPhase::page_out?1-fraction:phase_==MusicViewPhase::page_in?fraction:1.f;
    const auto body_scale=1+.1f*(1-whole),row_scale=1+.1f*(1-whole*content);
    const auto add=[&](std::shared_ptr<const image::Raster> image,float x,float y,float w,float h,float sx,float sy,float sw,float sh,unsigned alpha=255,float scale=1){
        sprites_.push_back({std::move(image),640+(x-640)*scale,360+(y-360)*scale,w*scale,h*scale,sx,sy,sw,sh,255,255,255,std::uint8_t(std::min(255u,unsigned(float(alpha)*whole)))});sprites_.back().layer=0x1060;sprites_.back().order=order++;
    };
    add(black_,0,0,1280,720,0,0,1,1,128);add(images_[0],0,-32*(1-whole),1280,40,0,0,1280,40);add(images_[1],0,664+32*(1-whole),1280,56,0,0,1280,56);
    const auto row_alpha=unsigned(256*content);
    for(const auto& row:rows_){const float x=40.f+400*(row.ordinal/13),y=88.f+32*(row.ordinal%13);add(images_[2],x,y,400,32,0,row.enabled?(focus_==row.ordinal?96:playing_==row.index?64:32):0,400,32,row_alpha,row_scale);
        if(row.enabled)add(images_[3],x+40,y+4,344,24,0,float(row.index*24),344,24,row_alpha,row_scale);}
    add(images_[7],120,528,1040,56,0,0,1024,56,255,body_scale);if(playing_)add(images_[8],156,552,512,24,0,float(*playing_*24),512,24,255,body_scale);
    add(images_[4],440,600,48,64,focus_==51?96:0,0,48,64,255,body_scale);add(images_[4],792,600,48,64,focus_==52?96:0,64,48,64,255,body_scale);
    add(images_[5],496,608,288,40,0,0,288,40,255,body_scale);add(images_[6],1164,688+32*(1-whole),56,24,0,focus_==50?24:0,56,24);
    if(current_music_ && (phase_==MusicViewPhase::active || phase_==MusicViewPhase::page_out || phase_==MusicViewPhase::page_in))
        append_music_caption(sprites_,images_[9],images_[10],current_music_(),0x1060,order);
}
}
