#include "music_view.hpp"
#include <algorithm>
#include <cmath>

namespace wa2::game {
namespace {
// Content metadata recovered from the existing cached 0x4c1dd0/0x4c1ed0
// tables. No machine addresses or recompiled functions enter the runtime.
constexpr std::array<int,63> songs{4,1,2,3,5,6,18,7,8,9,10,11,12,13,14,15,16,17,27,33,32,28,19,20,21,26,29,23,24,22,34,60,31,40,41,42,43,44,45,46,47,69,74,52,53,61,50,51,62,63,67,68,70,71,54,55,64,58,59,66,56,57,65};
constexpr std::array<unsigned,27> closing_indices{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,22,23,24,25,26,27,28};
constexpr std::array<std::string_view,9> names{"sys_04000.tga","sys_09000.tga","sys_04100.tga","sys_04101.tga","sys_04010.tga","sys_04021.tga","sys_09100.tga","sys_04030.tga","sys_04031.tga"};
constexpr unsigned widths[]{1280,1280,400,344,304,288,56,1024,512},heights[]{40,56,128,1512,168,40,72,56,1512};
}
MusicView::MusicView(Loader loader,Sound sound):loader_(std::move(loader)),sound_(std::move(sound)){
    if(!loader_)throw script::Error("Missing native music-browser image loader");auto black=std::make_shared<image::Raster>();black->width=black->height=1;black->rgba={0,0,0,255};black_=std::move(black);sprites_.reserve(108);
}
void MusicView::clear(){images_={};request_.reset();rows_.clear();sprites_.clear();focus_.reset();playing_.reset();command_.reset();phase_=MusicViewPhase::idle;page_=image_next_=0;}
void MusicView::prepare(TitleContent content,const progress::State& progress){
    std::array<bool,200> next{};for(unsigned n=100;n<next.size();++n)next[n]=progress.flag(n)!=0;if(next[104] && next[163])next[134]=true;
    auto request=loader_(names[0]);if(!request)throw script::Error("Missing native music-browser image request");clear();content_=content;unlocked_=next;request_=std::move(request);phase_=MusicViewPhase::loading;rebuild_rows();
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
void MusicView::turn(){page_^=1;rebuild_rows();focus_.reset();if(sound_)sound_(audio::SystemCue::focus);}
void MusicView::activate(unsigned id){
    if(id==50){command_=MusicCommand{MusicCommand::Kind::close,0,0,120};phase_=MusicViewPhase::closed;request_.reset();sprites_.clear();if(sound_)sound_(audio::SystemCue::cancel);return;}
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
    if(input.cancel){activate(50);return;}if(phase_==MusicViewPhase::loading){if(!images())return;phase_=MusicViewPhase::active;draw();return;}
    if(input.page_delta)turn();const auto previous=focus_;
    if(selected.pointer){focus_.reset();for(const auto& row:rows_){const float x=40.f+400*(row.ordinal/13),y=88.f+32*(row.ordinal%13);if(selected.x>=x && selected.x<x+400 && selected.y>=y && selected.y<y+32){focus_=row.ordinal;break;}}
        if(selected.x>=1164 && selected.x<1220 && selected.y>=688 && selected.y<712)focus_=50;
        if(selected.y>=600 && selected.y<664){if(selected.x>=440 && selected.x<488)focus_=51;if(selected.x>=792 && selected.x<840)focus_=52;}}
    if(selected.navigation){std::vector<unsigned> controls;for(const auto& row:rows_)if(row.enabled)controls.push_back(row.ordinal);controls.insert(controls.end(),{51,52,50});const auto at=std::find(controls.begin(),controls.end(),focus_.value_or(999));const int size=int(controls.size());const auto n=at==controls.end()?(selected.navigation>0?0:size-1):(int(at-controls.begin())+selected.navigation+size)%size;focus_=controls[unsigned(n)];}
    if(focus_ && focus_!=previous && sound_)sound_(audio::SystemCue::focus);if(focus_ && (selected.confirm || (selected.pointer && selected.activate)))activate(*focus_);draw();
}
void MusicView::draw(){
    sprites_.clear();if(image_next_<images_.size() || phase_==MusicViewPhase::closed)return;int order{};
    const auto add=[&](std::shared_ptr<const image::Raster> image,float x,float y,float w,float h,float sx,float sy,float sw,float sh,unsigned alpha=255){sprites_.push_back({std::move(image),x,y,w,h,sx,sy,sw,sh,255,255,255,std::uint8_t(alpha)});sprites_.back().layer=0x1060;sprites_.back().order=order++;};
    add(black_,0,0,1280,720,0,0,1,1,128);add(images_[0],0,0,1280,40,0,0,1280,40);add(images_[1],0,664,1280,56,0,0,1280,56);
    for(const auto& row:rows_){const float x=40.f+400*(row.ordinal/13),y=88.f+32*(row.ordinal%13);add(images_[2],x,y,400,32,0,row.enabled?(playing_==row.index?64:32):0,400,32);
        if(row.enabled)add(images_[3],x+40,y+4,344,24,0,float(row.index*24),344,24);}
    if(focus_ && *focus_<rows_.size() && rows_[*focus_].enabled){const auto& row=rows_[*focus_];add(images_[2],40.f+400*(row.ordinal/13),88.f+32*(row.ordinal%13),400,32,0,96,400,32);}
    add(images_[7],120,528,1040,56,0,0,1024,56);if(playing_)add(images_[8],156,552,512,24,0,float(*playing_*24),512,24);
    add(images_[4],440,600,48,64,focus_==51?96:0,0,48,64);add(images_[4],792,600,48,64,focus_==52?96:0,64,48,64);
    add(images_[5],496,608,288,40,0,0,288,40);add(images_[6],1164,688,56,24,0,focus_==50?24:0,56,24);
}
}
