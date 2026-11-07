#include "options_view.hpp"
#include <algorithm>
#include <cmath>

namespace wa2::game {
namespace {
constexpr unsigned count=unsigned(Option::count);
struct Point {int x{},y{};};
struct Row {int type;Option option;Point cells[6];};
constexpr Option absent=Option::count;
constexpr std::array<Row,30> rows{{
    {0,Option::display,{{456,12},{712,12}}},
    {1,Option::text_speed,{{456,76},{584,76},{712,76},{840,76}}},
    {0,Option::skip_policy,{{456,116},{712,116}}},
    {4,Option::auto_delay,{{480,156},{888,156},{544,158}}},
    {0,Option::transition_wait,{{456,220},{712,220}}},
    {4,Option::window_opacity,{{480,260},{888,260},{544,262}}},
    {4,Option::picture_opacity,{{480,300},{888,300},{544,302}}},
    {4,Option::novel_opacity,{{480,340},{888,340},{544,342}}},
    {4,Option::button_opacity,{{480,380},{888,380},{544,382}}},
    {2,Option::window_visibility,{{384,270},{384,300}}},
    {0,Option::confirmation,{{456,12},{712,12}}},
    {3,Option::right_click,{{456,52},{628,52},{800,52}}},
    {5,Option::master_volume,{{480,116},{888,116},{544,118},{390,118},{390,136}}},
    {5,Option::music_volume,{{480,156},{888,156},{544,158},{390,158},{390,176}}},
    {6,Option::effects_volume,{{480,196},{888,196},{544,198},{390,198},{390,216},{944,196}}},
    {6,Option::voice_volume,{{480,236},{888,236},{544,238},{390,238},{390,256},{944,236}}},
    {0,Option::voice_advance,{{456,300},{712,300}}},
    {0,Option::adult_voice,{{456,340},{712,340}}},
    {-1,absent,{}},{-1,absent,{}},
    {0,Option::character_0,{{456,12},{712,12}}},
    {0,Option::character_2,{{456,52},{712,52}}},
    {0,Option::character_1,{{456,92},{712,92}}},
    {0,Option::character_3,{{456,132},{712,132}}},
    {0,Option::character_4,{{456,172},{712,172}}},
    {0,Option::character_5,{{456,212},{712,212}}},
    {0,Option::character_10,{{456,252},{712,252}}},
    {0,Option::character_11,{{456,292},{712,292}}},
    {0,Option::character_99,{{456,332},{712,332}}},
    {0,Option::character_98,{{456,372},{712,372}}}
}};
constexpr std::array<std::string_view,7> common{
    "sys_03000.tga","sys_09000.tga","sys_03010.tga","sys_03011.tga","sys_03020.tga","sys_03030.tga","sys_09100.tga"};
constexpr std::array<std::array<std::string_view,4>,3> pages{{
    {"sys_03100.tga","sys_03200.tga","sys_03201.tga","sys_03040.tga"},
    {"sys_03101.tga","sys_03210.tga","sys_03211.tga","sys_03041.tga"},
    {"sys_03102.tga","sys_03220.tga","sys_03221.tga","sys_03042.tga"}
}};
constexpr std::array<unsigned,11> widths{1280,1280,756,576,432,208,56,1024,1024,1024,944};
constexpr std::array<unsigned,11> heights{36,56,96,96,128,72,72,464,464,464,24};
unsigned checked(Option option){if(unsigned(option)>=count)throw script::Error("Unknown native settings field");return unsigned(option);}
bool slider(Option option){return option==Option::auto_delay || (option>=Option::window_opacity && option<=Option::button_opacity) || (option>=Option::master_volume && option<=Option::voice_volume);}
int upper(Option option){return option==Option::auto_delay?600:slider(option)?256:option==Option::text_speed?3:option==Option::right_click?2:1;}
unsigned cells(int type){return type<0?0:type==1?4:type==3?3:type==4?3:type==5?5:type==6?6:2;}
std::pair<float,float> extent(int type,unsigned cell){
    if(type>=4)return cell==2?std::pair{336.f,24.f}:cell==3 || cell==4?std::pair{80.f,16.f}:std::pair{56.f,40.f};
    return type==1?std::pair{128.f,40.f}:type==2?std::pair{88.f,30.f}:type==3?std::pair{168.f,40.f}:std::pair{256.f,40.f};
}
int fill(Option option,int value){return option==Option::auto_delay?336*(value-60)/540:336*value/256;}
int quantize(Option option,int pixels){const auto bin=pixels<8?0:(pixels-2)/16+1;return option==Option::auto_delay?std::clamp(60+bin*540/21,60,600):std::clamp(bin*256/21,0,256);}
std::uint8_t opacity(float value){return std::uint8_t(std::clamp(int(value),0,255));}
}
std::int32_t OptionsState::value(Option option)const{return values[checked(option)];}
void OptionsState::value(Option option,std::int32_t next){const auto n=checked(option);if(next<(option==Option::auto_delay?60:0) || next>upper(option))throw script::Error("Native settings field is out of range");values[n]=next;}
void OptionsState::validate()const{for(unsigned n=0;n<count;++n){auto copy=*this;copy.value(Option(n),values[n]);}}
OptionsView::OptionsView(Loader loader,Apply apply,Sound sound):loader_(std::move(loader)),apply_(std::move(apply)),sound_(std::move(sound)){
    if(!loader_)throw script::Error("Missing native settings image loader");
    black_=std::make_shared<const image::Raster>(image::Raster{1,1,{0,0,0,255}});hits_.reserve(48);sprites_.reserve(64);
}
void OptionsView::clear(){assets_={};pending_.reset();hits_.clear();sprites_.clear();focus_.reset();drag_.reset();pressed_.reset();command_.reset();confirmation_.reset();phase_=OptionsPhase::idle;page_=elapsed_=0;}
void OptionsView::prepare(const OptionsState& state,unsigned page,OptionsTiming timing){
    state.validate();if(page>=3 || timing.open>600 || timing.page>600 || timing.close>600)throw script::Error("Native settings page or timing is out of range");
    Bundle next;next.page=page;next.request=loader_(common[0]);if(!next.request)throw script::Error("Missing native settings image request");
    clear();state_=state;timing_=timing;page_=page;pending_=std::move(next);phase_=OptionsPhase::loading;
}
bool OptionsView::poll(){
    auto& next=*pending_;
    // At most one outstanding decode and two new requests per logic tick.
    // Menu atlases cannot fill the eight-item shared image queue themselves.
    for(unsigned issued=0;next.next<next.images.size();){
        if(next.images[next.next]){++next.next;continue;}
        if(!next.request){if(issued==2)return false;next.request=loader_(next.next<7?common[next.next]:pages[next.page][next.next-7]);++issued;if(!next.request)throw script::Error("Missing native settings image request");}
        auto image=next.request->get();if(!image)return false;
        const auto height=next.next==10?next.page==0?216u:next.page==1?120u:24u:heights[next.next];
        if(image->width<widths[next.next] || image->height<height)throw script::Unsupported("Native settings atlas dimensions are not supported");
        next.images[next.next++]=std::move(image);next.request.reset();
    }
    return true;
}
void OptionsView::state(const OptionsState& state){state.validate();state_=state;draw();}
void OptionsView::timing(OptionsTiming timing){if(timing.open>600 || timing.page>600 || timing.close>600)throw script::Error("Native settings timing is out of range");timing_=timing;draw();}
void OptionsView::publish(const OptionsState& next){next.validate();if(next==state_)return;if(apply_)apply_(next);state_=next;if(sound_)sound_(audio::SystemCue::accept);}
void OptionsView::regions(){
    hits_.clear();
    for(unsigned n=0;n<3;++n)hits_.push_back({n,0,n,{},float(688+n*192),72,192,32});
    hits_.push_back({3,0,0,{},880,688,208,24});hits_.push_back({7,0,0,{},440,600,48,64});hits_.push_back({8,0,0,{},792,600,48,64});hits_.push_back({9,0,0,{},1164,688,56,24});
    for(unsigned n=0;n<10;++n){const auto& row=rows[page_*10+n];for(unsigned cell=0;cell<cells(row.type);++cell){const auto [w,h]=extent(row.type,cell);hits_.push_back({10+n*10+cell,n,cell,row.option,float(128+row.cells[cell].x),float(112+row.cells[cell].y),w,h});}}
}
void OptionsView::change_page(unsigned page){
    if(page==page_)return;Bundle next;next.page=page;for(unsigned n=0;n<7;++n)next.images[n]=assets_.images[n];next.next=7;
    // Keep the displayed page until all incoming page images are published.
    next.request=loader_(pages[page][0]);if(!next.request)throw script::Error("Missing native settings page image request");
    pending_=std::move(next);focus_.reset();drag_.reset();pressed_.reset();elapsed_=0;phase_=OptionsPhase::page_loading;if(sound_)sound_(audio::SystemCue::accept);
}
void OptionsView::emit(OptionsCommand::Kind kind){if(command_)throw script::Error("Native settings command was not consumed");command_=OptionsCommand{kind};}
std::optional<OptionsCommand> OptionsView::take_command(){auto result=command_;command_.reset();return result;}
void OptionsView::confirm_reset(int skin,ConfirmationView::Text text){
    if(phase_!=OptionsPhase::active || confirmation_)throw script::Error("Native settings cannot open another confirmation");
    auto next=std::make_unique<ConfirmationView>(loader_,std::move(text),sound_);next->prepare(options_reset_prompt(),skin,timing_.open,timing_.close);
    focus_.reset();drag_.reset();pressed_.reset();confirmation_=std::move(next);draw();
}
void OptionsView::activate(const OptionsHit& hit,float x){
    if(!hit.option){if(hit.id<3)change_page(hit.id);else if(hit.id==7 || hit.id==8)change_page((page_+(hit.id==7?2u:1u))%3);else if(hit.id==9){phase_=OptionsPhase::closing;elapsed_=0;focus_.reset();drag_.reset();if(sound_)sound_(audio::SystemCue::cancel);}else if(hit.id==3){emit(OptionsCommand::Kind::reset);if(sound_)sound_(audio::SystemCue::accept);}return;}
    const auto option=*hit.option;const auto& row=rows[page_*10+hit.row];
    if(row.type==6 && hit.cell==5){emit(option==Option::effects_volume?OptionsCommand::Kind::preview_effects:OptionsCommand::Kind::preview_voice);if(sound_)sound_(audio::SystemCue::accept);return;}
    auto next=state_;
    if(row.type>=5 && hit.cell>=3)next.muted[unsigned(option)-unsigned(Option::master_volume)]=hit.cell==3;
    else if(row.type>=4){const auto pixels=hit.cell==2?int(std::clamp(x-hit.x,0.f,336.f)):std::clamp(fill(option,next.value(option))+(hit.cell==0?-16:16),0,336);next.value(option,quantize(option,pixels));}
    else next.value(option,int(hit.cell));publish(next);
}
void OptionsView::input(OptionsInput input){
    const auto& in=input.selection;
    if(input.cancel){phase_=OptionsPhase::closing;elapsed_=0;focus_.reset();drag_.reset();if(sound_)sound_(audio::SystemCue::cancel);return;}
    if(input.page_delta){change_page((page_+(input.page_delta<0?2u:1u))%3);return;}
    const auto old=focus_;
    if(!input.held)drag_.reset();
    if(drag_ && in.pointer){const auto it=std::find_if(hits_.begin(),hits_.end(),[&](const auto& hit){return hit.id==*drag_;});if(it==hits_.end())throw script::Error("Invalid native settings slider capture");focus_=drag_;activate(*it,in.x);return;}
    if(in.pointer){focus_.reset();for(const auto& hit:hits_)if(in.x>=hit.x && in.x<hit.x+hit.width && in.y>=hit.y && in.y<hit.y+hit.height){focus_=hit.id;break;}}
    if(in.navigation){const auto it=std::find_if(hits_.begin(),hits_.end(),[&](const auto& hit){return focus_==hit.id;});const auto n=it==hits_.end()?(in.navigation>0?0:int(hits_.size())-1):(int(it-hits_.begin())+in.navigation+int(hits_.size()))%int(hits_.size());focus_=hits_[unsigned(n)].id;}
    if(focus_ && focus_!=old && sound_)sound_(audio::SystemCue::focus);
    if(focus_ && (in.confirm || (in.pointer && in.activate))){const auto it=std::find_if(hits_.begin(),hits_.end(),[&](const auto& hit){return hit.id==*focus_;});const auto hit=*it;pressed_=hit.id;
        if(input.held && hit.option && slider(*hit.option) && hit.cell==2)drag_=hit.id;activate(hit,in.pointer?in.x:hit.x+hit.width*.5f);}
}
void OptionsView::update(OptionsInput in){
    if(!std::isfinite(in.selection.x) || !std::isfinite(in.selection.y) || std::abs(std::int64_t(in.selection.navigation))>1 || std::abs(std::int64_t(in.page_delta))>1)throw script::Error("Native settings input is out of range");
    if(phase_==OptionsPhase::idle)throw script::Error("Native settings view is not prepared");pressed_.reset();
    if(command_ || phase_==OptionsPhase::closed)return;
    if(confirmation_){
        confirmation_->update({in.selection,in.cancel});
        if(auto answer=confirmation_->take_answer();answer && *answer)emit(OptionsCommand::Kind::reset_confirmed);
        if(confirmation_->phase()==ConfirmationPhase::closed)confirmation_.reset();draw();return;
    }
    if(phase_==OptionsPhase::loading){if(!poll())return;assets_=std::move(*pending_);pending_.reset();regions();elapsed_=0;phase_=OptionsPhase::opening;}
    if(phase_==OptionsPhase::page_loading){if(!poll()){draw();return;}elapsed_=0;phase_=OptionsPhase::page_out;}
    switch(phase_){
    case OptionsPhase::opening:if(++elapsed_>=timing_.open){elapsed_=0;phase_=OptionsPhase::active;}break;
    case OptionsPhase::page_out:if(++elapsed_>=timing_.page){assets_=std::move(*pending_);pending_.reset();page_=assets_.page;regions();elapsed_=0;phase_=OptionsPhase::page_in;}break;
    case OptionsPhase::page_in:if(++elapsed_>=timing_.page){elapsed_=0;phase_=OptionsPhase::active;}break;
    case OptionsPhase::closing:if(++elapsed_>=timing_.close){phase_=OptionsPhase::closed;emit(OptionsCommand::Kind::close);}break;
    case OptionsPhase::active:input(in);break;
    default:break;
    }
    draw();
}
void OptionsView::draw(){
    sprites_.clear();if(!assets_.images[0] || phase_==OptionsPhase::idle || phase_==OptionsPhase::closed)return;
    const auto fraction=[&](unsigned duration){return duration?std::min(1.f,float(elapsed_)/duration):1.f;};
    const auto whole=phase_==OptionsPhase::opening?fraction(timing_.open):phase_==OptionsPhase::closing?1-fraction(timing_.close):1.f;
    const auto content=phase_==OptionsPhase::page_out?1-fraction(timing_.page):phase_==OptionsPhase::page_in?fraction(timing_.page):whole;
    const auto body_scale=phase_==OptionsPhase::page_in?1-.05f*(1-content):1+.1f*(1-content);
    const auto push=[&](Image image,float x,float y,float w,float h,float sx,float sy,int layer,int order,float alpha,float scale=1){
        if(w<=0 || h<=0)return;sprites_.push_back({std::move(image),640+(x-640)*scale,360+(y-360)*scale,w*scale,h*scale,sx,sy,w,h,255,255,255,opacity(alpha)});auto& sprite=sprites_.back();sprite.layer=layer;sprite.order=order;
    };
    const auto item=[&](unsigned image,float x,float y,float w,float h,float sx,float sy,int layer,int order,float alpha,float scale=1){push(assets_.images[image],x,y,w,h,sx,sy,layer,order,alpha,scale);};
    push(black_,0,0,1280,720,0,0,0x1061,0x4a8,128*whole);sprites_.back().source_width=sprites_.back().source_height=1;
    item(0,0,-32*(1-whole),1280,40,0,0,0x1062,0x4a4,256*whole);
    item(1,0,664+32*(1-whole),1280,56,0,0,0x1062,0x4a5,256*whole);
    const auto chrome_scale=1+.1f*(1-whole);
    const auto status=[&](unsigned id){return pressed_==id?2:focus_==id?1:0;};
    item(4,440,600,48,64,float(status(7)*48),0,0x1064,0x4a9,256*whole,chrome_scale);
    item(4,792,600,48,64,float(status(8)*48),64,0x1064,0x4aa,256*whole,chrome_scale);
    item(4,496,608,288,40,144,0,0x1064,0x4ab,256*whole,chrome_scale);
    item(6,1164,688,56,24,0,float(status(9)*24),0x1064,0x4ad,256*whole,chrome_scale);
    item(2,524,64,756,48,0,0,0x1063,0x4ae,256*whole,chrome_scale);
    item(2,524,64,756,48,0,48,0x1065,0x4af,256*whole,chrome_scale);
    for(unsigned n=0;n<3;++n)item(3,float(688+n*192),72,192,32,float(n*192),n==page_?0:focus_==n?32:64,n==page_?0x1066:0x1064,0x4b0+int(n),256*whole,chrome_scale);
    item(5,880,688,208,24,0,float(status(3)*24),0x1063,0x4b4,256*whole,chrome_scale);
    item(7,128,112,1024,464,0,0,0x1062,0x4b5,256*content,body_scale);
    for(const auto& hit:hits_)if(hit.option){const auto& row=rows[page_*10+hit.row];const auto value=state_.value(*hit.option);bool mark{};float width=hit.width;
        if(row.type>=4){if(hit.cell==2){mark=true;width=*hit.option==Option::auto_delay?float(fill(*hit.option,value)):336.f*value/256;}else if(hit.cell==3 || hit.cell==4)mark=state_.muted[unsigned(*hit.option)-unsigned(Option::master_volume)]==(hit.cell==3);else mark=pressed_==hit.id;}
        else mark=value==int(hit.cell);
        if(mark)item(9,hit.x,hit.y,width,hit.height,hit.x-128,hit.y-112,hit.cell==2 && row.type>=4?0x1065:row.type>=4?0x1064:0x1063,0x4b8+int(hit.row+hit.cell*10),256*content,body_scale);
        if(focus_==hit.id && !mark)item(8,hit.x,hit.y,hit.width,hit.height,hit.x-128,hit.y-112,0x1063,0x4b6,256*content,body_scale);
    }
    if(focus_ && *focus_>=10){const auto row=(*focus_-10)/10;const auto help=page_==0?row:page_==1?row<2?row:row<6?2:row-3:0;
        // The last first-page row intentionally lies beyond its hint atlas.
        if(help*24<assets_.images[10]->height)item(10,168,552,994,24,0,float(help*24),0x1064,0x4ac,256*whole,chrome_scale);}
    if(confirmation_)sprites_.insert(sprites_.end(),confirmation_->sprites().begin(),confirmation_->sprites().end());
    std::stable_sort(sprites_.begin(),sprites_.end(),[](const auto& a,const auto& b){return a.layer!=b.layer?a.layer<b.layer:a.order<b.order;});
}
}
