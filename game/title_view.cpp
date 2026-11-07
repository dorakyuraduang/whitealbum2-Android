#include "title_view.hpp"
#include <algorithm>
#include <cmath>

namespace wa2::game {
namespace {
constexpr std::array<std::string_view,8> names{"T0000.tga","T0100.tga","T0010.tga","T0020.tga","T0050.tga","T0101.tga","T0200.tga","T0201.tga"};
constexpr std::array<unsigned,8> widths{1280,640,752,264,408,320,288,288},heights{856,640,448,24,24,80,320,160};
std::uint8_t opacity(std::int32_t value){return std::uint8_t(std::clamp(value,0,255));}
float ease(std::int32_t remaining,std::int32_t duration){return float(1.0+(double(remaining)*remaining)/(double(duration)*duration)/5.0);}
}
TitleView::TitleView(Loader loader,Random random,Sound sound,Music music)
    :loader_(std::move(loader)),sound_(std::move(sound)),music_(std::move(music)),weather_(loader_,std::move(random)){
    if(!loader_)throw script::Error("Missing native title image loader");rows_.reserve(6);sprites_.reserve(432);
}
void TitleView::clear(){
    weather_.stop(true);requests_={};images_={};rows_.clear();sprites_.clear();focus_.reset();command_.reset();leaving_.reset();
    elapsed_=0;pan_=0;phase_=TitlePhase::finished;section_=TitleSection::root;music_started_=prepared_=restored_extras_=restored_modal_=false;
}
void TitleView::access(const progress::State& state){
    introductory_clear_=state.flag(0)!=0;closing_clear_=state.flag(3)!=0;coda_unlocked_=state.flag(5)!=0;
    std::int32_t all=1;for(unsigned n:{20,21,22,23,24,25,30,31,32,33})all&=state.flag(n);all_clear_=all!=0;
}
void TitleView::prepare(TitleContent content,const progress::State& progress){
    // Resolve all names before replacing an existing prepared view. An absent
    // member cannot leave it with a mixture of old and new image requests.
    std::array<std::shared_ptr<image::ImageRequest>,8> next;
    for(unsigned n=0;n<next.size();++n){next[n]=loader_(n==2 && content.local_introductory?"T0011.tga":names[n]);if(!next[n])throw script::Error("Missing native title image request");}
    clear();requests_=std::move(next);content_=content;access(progress);phase_=TitlePhase::loading;prepared_=true;
}
bool TitleView::poll(){
    for(unsigned n=0;n<requests_.size();++n)if(requests_[n])if(auto image=requests_[n]->get()){
        if(image->width<widths[n] || image->height<heights[n])throw script::Unsupported("Native title atlas dimensions are not supported for "+std::string(names[n]));
        images_[n]=std::move(image);requests_[n].reset();
    }
    return std::none_of(requests_.begin(),requests_.end(),[](const auto& request){return bool(request);});
}
void TitleView::menu(TitleSection section,std::uint32_t elapsed){
    section_=section;phase_=TitlePhase::menu;elapsed_=elapsed;focus_.reset();rows_.clear();
    const auto add=[&](unsigned row,int cell,int width,int height=32){rows_.push_back({row,cell,float(360+row*40),float(width),float(height)});};
    switch(section){
    case TitleSection::root:
        for(unsigned row=0;row<6;++row)if(row!=4 && (row!=3 || introductory_clear_ || !content_.local_introductory))add(row,row==5?4:int(row),140);break;
    case TitleSection::chapters:
        if(!content_.local_introductory && !content_.external_introductory){add(1,6,240);rows_.back().y=360;if(coda_unlocked_){add(2,7,100);rows_.back().y=400;}}
        else{if(introductory_clear_ || !content_.local_introductory)add(0,5,300);add(1,6,240);if(coda_unlocked_)add(2,7,100);}
        add(5,8,65);break;
    case TitleSection::extras:
        if(introductory_clear_ || !content_.local_introductory)add(0,9,180,40);
        if(closing_clear_){add(1,10,140,40);add(2,11,180,40);}
        if(introductory_clear_ || !content_.local_introductory)add(3,12,140,40);
        if(all_clear_)add(4,13,200,40);add(5,8,65,40);break;
    case TitleSection::special_stories:add(0,14,300);add(1,15,240);add(5,8,65);break;
    }
}
void TitleView::leave(TitleCommand command){
    leaving_=std::move(command);phase_=leaving_->kind==TitleCommand::Kind::quit?TitlePhase::quit_out:TitlePhase::story_out;elapsed_=0;
}
void TitleView::emit(TitleCommand command){
    if(command_)throw script::Error("Native title command was not consumed");command_=std::move(command);phase_=TitlePhase::modal;
}
void TitleView::activate(std::uint32_t row){
    const auto cue=[&](audio::SystemCue value){if(sound_)sound_(value);};
    switch(section_){
    case TitleSection::root:
        switch(row){
        case 0:
            if(content_.local_introductory && !introductory_clear_)leave({TitleCommand::Kind::start,"1001"});
            else if(!content_.local_introductory && !content_.external_introductory && !coda_unlocked_)leave({TitleCommand::Kind::start,"2001"});
            else{cue(audio::SystemCue::accept);menu(TitleSection::chapters);}break;
        case 1:cue(audio::SystemCue::accept);emit({TitleCommand::Kind::load,{}});break;
        case 2:cue(audio::SystemCue::accept);emit({TitleCommand::Kind::options,{}});break;
        case 3:cue(audio::SystemCue::accept);menu(TitleSection::extras);break;
        case 5:cue(audio::SystemCue::accept);leave({TitleCommand::Kind::quit,{}});break;
        default:throw script::Error("Invalid native root title row");
        }break;
    case TitleSection::chapters:
        if(row==5){cue(audio::SystemCue::cancel);menu(TitleSection::root);}else if(row<3)leave({TitleCommand::Kind::start,row==0?"1001":row==1?"2001":"3001"});else throw script::Error("Invalid native chapter row");break;
    case TitleSection::extras:
        if(row==5){cue(audio::SystemCue::cancel);menu(TitleSection::root);}else if(row==0){cue(audio::SystemCue::accept);menu(TitleSection::special_stories);}else if(row<5){cue(audio::SystemCue::accept);emit({TitleCommand::Kind::extra,{},std::int32_t(row)});}else throw script::Error("Invalid native extra row");break;
    case TitleSection::special_stories:
        if(row==5){cue(audio::SystemCue::cancel);menu(TitleSection::extras);}else if(row<2)leave({TitleCommand::Kind::start,row==0?"5000":"5100"});else throw script::Error("Invalid native special story row");break;
    }
}
void TitleView::input(TitleInput input){
    if(input.cancel && section_!=TitleSection::root){menu(section_==TitleSection::special_stories?TitleSection::extras:TitleSection::root);if(sound_)sound_(audio::SystemCue::cancel);return;}
    const auto old=focus_;const auto& in=input.selection;
    if(in.pointer){focus_.reset();for(const auto& row:rows_)if(in.x>=640-row.hit_width*.5f && in.x<640+row.hit_width*.5f && in.y>=row.y && in.y<row.y+row.hit_height){focus_=row.ordinal;break;}}
    if(in.navigation){const auto it=std::find_if(rows_.begin(),rows_.end(),[&](const auto& row){return focus_==row.ordinal;});const auto count=int(rows_.size());
        const auto n=it==rows_.end()?(in.navigation>0?0:count-1):(int(it-rows_.begin())+in.navigation+count)%count;focus_=rows_[unsigned(n)].ordinal;}
    if(focus_ && focus_!=old && sound_)sound_(audio::SystemCue::focus);
    if(focus_ && (in.confirm || (in.pointer && in.activate)))activate(*focus_);
}
void TitleView::snow(){
    if(pan_!=136 || weather_.active())return;
    std::array<script::Value,6> args{script::Value(content_.perspective_snow?0xe03:0x603),script::Value(0),script::Value(content_.perspective_snow?3:5),script::Value(content_.perspective_snow?400:200),script::Value(1),script::Value(1)};
    script::Command command{173,args};weather_.dispatch(command);
}
void TitleView::update(TitleInput in){
    const auto& selected=in.selection;if(!std::isfinite(selected.x) || !std::isfinite(selected.y) || selected.navigation<-1 || selected.navigation>1)throw script::Error("Native title input is out of range");
    if(!prepared_)throw script::Error("Native title is not prepared");
    if(phase_==TitlePhase::loading){if(!poll())return;if(restored_extras_){const auto modal=restored_modal_;pan_=136;music_started_=true;menu(TitleSection::extras,64);if(modal)phase_=TitlePhase::modal;restored_extras_=restored_modal_=false;snow();}else phase_=TitlePhase::background_in;}
    switch(phase_){
    case TitlePhase::background_in:
        ++elapsed_;pan_=float(double(pan_)+double(1.2f));if(elapsed_>30){phase_=TitlePhase::logo_in;elapsed_=0;}break;
    case TitlePhase::logo_in:
        if(!music_started_){if(music_)music_(31,0);music_started_=true;}
        ++elapsed_;pan_=float(double(pan_)+double(1.2f));
        if(elapsed_>80 || selected.confirm || selected.activate){if(selected.confirm || selected.activate)pan_=136;menu(TitleSection::root,30);}break;
    case TitlePhase::menu:
        pan_=std::min(136.f,float(double(pan_)+double(1.2f)));if(selected.activate || selected.confirm)pan_=136;snow();
        if(elapsed_<64)++elapsed_;input(in);break;
    case TitlePhase::story_out:case TitlePhase::quit_out:
        if(!elapsed_ && music_)music_({},phase_==TitlePhase::story_out?29:30);
        ++elapsed_;if(elapsed_>=(phase_==TitlePhase::story_out?45u:120u)){
            if(command_ || !leaving_)throw script::Error("Invalid native title exit command");command_=std::move(leaving_);leaving_.reset();phase_=TitlePhase::finished;
        }break;
    case TitlePhase::loading:case TitlePhase::modal:case TitlePhase::finished:break;
    }
    weather_.update();draw();
}
void TitleView::resume(const progress::State& progress){
    if(prepared_ && phase_==TitlePhase::loading && restored_extras_ && restored_modal_){access(progress);restored_modal_=false;return;}
    if(!prepared_ || phase_!=TitlePhase::modal || command_)throw script::Error("Native title has no consumed modal command to resume");access(progress);menu(section_,64);draw();
}
void TitleView::restore_extras_parent(){if(!prepared_ || phase_!=TitlePhase::loading)throw script::Error("Native extra parent must be prepared before title images finish loading");restored_extras_=restored_modal_=true;}
std::optional<TitleCommand> TitleView::take_command(){auto result=std::move(command_);command_.reset();return result;}
void TitleView::draw(){
    sprites_.clear();if(!images_[0] || phase_==TitlePhase::finished)return;
    const auto global=phase_==TitlePhase::background_in?std::clamp(int(elapsed_)*128/30,0,128):phase_==TitlePhase::story_out?std::clamp(128-int(elapsed_)*128/30,0,128):phase_==TitlePhase::quit_out?std::clamp((100-int(elapsed_))*128/20,0,128):128;
    const auto tint=std::uint8_t(std::min(global*2,255));
    const auto push=[&](unsigned texture,float x,float y,float w,float h,float sx,float sy,std::uint8_t alpha,int layer,int order,float scale=1,float px=0,float py=0,image::SpriteBlend blend=image::SpriteBlend::alpha){
        sprites_.push_back({images_[texture],px+(x-px)*scale,py+(y-py)*scale,w*scale,h*scale,sx,sy,w,h,tint,tint,tint,alpha});auto& sprite=sprites_.back();sprite.layer=layer;sprite.order=order;sprite.blend=blend;
    };
    push(0,0,0,1280,720,0,pan_,255,2,1);
    for(auto sprite:weather_.sprites()){sprite.red=std::uint8_t(unsigned(sprite.red)*tint/255);sprite.green=std::uint8_t(unsigned(sprite.green)*tint/255);sprite.blue=std::uint8_t(unsigned(sprite.blue)*tint/255);sprites_.push_back(std::move(sprite));}
    if(phase_==TitlePhase::background_in)return;
    const auto logo=phase_==TitlePhase::logo_in?std::clamp(int(elapsed_),0,60):60;
    const auto alpha=phase_==TitlePhase::quit_out?opacity((16-int(elapsed_))*16):opacity(logo*256/60);
    push(2,264,0,752,448,0,0,alpha,0x104e,2,phase_==TitlePhase::logo_in?ease(60-logo,60):1,640,224);
    push(3,992,684,264,24,0,0,alpha,0x104e,3);
    if(content_.external_introductory && !content_.local_introductory)push(4,432,684,408,24,0,0,alpha,0x104e,4);
    if(phase_==TitlePhase::logo_in && rows_.empty()){
        // Loading the root's hit regions does not permit clicks during the
        // logo animation. Their source rows still participate in its fade.
        for(unsigned n=0;n<6;++n)if(n!=4 && (n!=3 || introductory_clear_ || !content_.local_introductory))rows_.push_back({n,n==5?4:int(n),float(360+n*40),140,32});
    }
    for(const auto& row:rows_){
        const auto local=phase_==TitlePhase::logo_in?std::clamp(int(elapsed_)-int(row.ordinal)*10-10,0,20):20;
        const auto delay=section_==TitleSection::chapters && !content_.local_introductory && !content_.external_introductory?(row.ordinal==5?4:int(row.ordinal)-1):int(row.ordinal);
        const auto row_alpha=phase_==TitlePhase::logo_in?opacity(local*256/20):phase_==TitlePhase::quit_out?alpha:phase_==TitlePhase::story_out?std::uint8_t(255):opacity((int(elapsed_)-delay*2)*16);
        const auto scale=phase_==TitlePhase::logo_in?ease(20-local,20):1.f;
        push(5,480,row.y,320,40,0,0,row_alpha,0x104e,21+int(row.ordinal),scale,640,row.y+20);
        if(focus_==row.ordinal)push(5,480,row.y,320,40,0,40,row_alpha,0x104f,31+int(row.ordinal),1,0,0,image::SpriteBlend::add);
        push(1,480,row.y,320,40,focus_==row.ordinal?320:0,float(row.cell*40),row_alpha,0x1050,11+int(row.ordinal),scale,640,row.y+20);
    }
    if(phase_!=TitlePhase::logo_in){
        const auto header=section_==TitleSection::root?0:section_==TitleSection::chapters?40:section_==TitleSection::extras?80:120;
        const auto header_alpha=phase_==TitlePhase::quit_out?alpha:phase_==TitlePhase::story_out?std::uint8_t(255):opacity(int(elapsed_)*16);
        push(7,496,608,288,40,0,float(header),header_alpha,0x104f,41);
        if(focus_){const auto found=std::find_if(rows_.begin(),rows_.end(),[&](const auto& row){return row.ordinal==*focus_;});if(found!=rows_.end())push(6,496,608,288,20,0,float(found->cell*20),header_alpha,0x104f,42);}
    }
    std::stable_sort(sprites_.begin(),sprites_.end(),[](const auto& a,const auto& b){return a.layer!=b.layer?a.layer<b.layer:a.order<b.order;});
}
}
