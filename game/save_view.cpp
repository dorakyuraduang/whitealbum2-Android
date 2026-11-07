#include "save_view.hpp"
#include "menu_digits.hpp"
#include "menu_header.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace wa2::game {
namespace {
constexpr unsigned widths[]{1280,1280,1264,632,160,840,640,192,432,56,120,248,304,480,320,112,432,320,192,264};
constexpr unsigned heights[]{40,56,192,96,20,96,96,32,128,72,96,384,168,112,56,16,312,288,8,1008};
std::uint8_t opacity(float value){return std::uint8_t(std::clamp(int(value),0,255));}
text::TextStyle label_style(){text::TextStyle style;style.size=28;style.columns=22;style.rows=1;style.line_gap=0;return style;}
text::TextStyle prompt_style(){auto style=label_style();style.columns=15;style.rows=2;return style;}
std::string label(const SavePreview& preview,unsigned size=20){return "<F"+std::to_string(size)+preview.description+">";}
std::string number(unsigned slot){std::array<char,8> buffer{};const auto value=slot%100+1;std::snprintf(buffer.data(),buffer.size(),value==100?"%03u":" %02u",value);return buffer.data();}
std::string date(unsigned slot,const SavePreview& preview){std::array<char,80> buffer{};const auto& d=preview.date;std::snprintf(buffer.data(),buffer.size(),"            %04d %02d/%02d %02d:%02d",d.year,d.month,d.day,d.hour,d.minute);return number(slot)+buffer.data();}
}
SaveView::SaveView(Loader loader,Preview preview,Text text,Sound sound,CurrentMusic music):loader_(std::move(loader)),preview_(std::move(preview)),text_(std::move(text)),sound_(std::move(sound)),current_music_(std::move(music)){
    if(!loader_ || !preview_ || !text_)throw script::Error("Missing native save view services");black_=std::make_shared<const image::Raster>(image::Raster{1,1,{0,0,0,255}});hits_.reserve(25);sprites_.reserve(1024);
}
void SaveView::clear(){inventory_={};newly_saved_={};images_={};image_request_.reset();incoming_.reset();current_={};rows_={};result_label_={};hits_.clear();sprites_.clear();confirmation_.reset();focus_.reset();pressed_.reset();command_.reset();selected_.reset();operation_error_.clear();phase_=SavePhase::idle;page_=bank_=image_next_=elapsed_=0;}
std::string SaveView::name(unsigned index)const{
    constexpr std::string_view common[]{"","sys_09000.tga","sys_01010.tga","","sys_01014.tga","sys_01020.tga","sys_01021.tga","sys_01013.tga","sys_01022.tga","sys_09100.tga","sys_01015.tga","sys_01016.tga"};
    if(index==0)return context_.mode==SaveMode::save?"sys_01000.tga":"sys_02000.tga";
    if(index==3)return context_.mode==SaveMode::save?"sys_01011.tga":"sys_02011.tga";
    if(index<12)return std::string(common[index]);
    if(index==12)return std::string(menu_header(context_.theme,context_.title).name);
    if(index==13)return "sys_"+std::to_string(context_.skin)+"0110.tga";
    if(index==14)return "sys_02030.tga";if(index==15)return "sys_02031.tga";if(index==16)return "sys_02050.tga";
    if(index==17)return "sys_"+std::to_string(context_.skin)+"0011.tga";
    if(index==18)return "sys_00301.tga";if(index==19)return "sys_00300.tga";
    throw script::Error("Unknown native save atlas");
}
SaveView::PageData SaveView::page_data(unsigned page,unsigned bank)const{
    PageData value;value.page=page;value.bank=bank;for(unsigned n=0;n<value.rows.size();++n){auto& row=value.rows[n];row.slot=n==10?quick_slot:(bank?alternate_save_base:0)+page*10+n;if(inventory_[row.slot]){row.occupied=true;row.diagnostic=inventory_[row.slot]->diagnostic;}if(newly_saved_[row.slot]){row.occupied=true;row.diagnostic.clear();}}return value;
}
void SaveView::prepare(SaveContext context,std::span<const SaveSlotInfo> inventory){
    if((context.mode!=SaveMode::save && context.mode!=SaveMode::load) || context.skin<0 || context.skin>9 || context.theme<0 || context.theme>6 || (context.title && context.mode==SaveMode::save) || context.timing.open>600 || context.timing.page>600 || context.timing.close>600 || context.result_ticks>600 || inventory.size()>native_save_slots)throw script::Error("Invalid native save view context");
    std::array<std::optional<SaveSlotInfo>,300> next;std::array<bool,native_save_slots> seen{};std::array<unsigned,2> latest{};std::array<std::uint64_t,2> newest{};
    for(const auto& entry:inventory){if(entry.slot>=native_save_slots || seen[entry.slot])throw script::Error("Invalid native save view inventory");seen[entry.slot]=true;if(entry.slot>=next.size())continue;next[entry.slot]=entry;
        if(entry.slot<100 || entry.slot>=alternate_save_base){const auto bank=entry.slot>=alternate_save_base?1u:0u;if(entry.diagnostic.empty() && entry.modified_ms>newest[bank]){newest[bank]=entry.modified_ms;latest[bank]=entry.slot%100;}}}
    auto result_style=label_style();result_style.columns=20;auto result=text_(save_result_prompt(),result_style);auto request=loader_(context.mode==SaveMode::save?"sys_01000.tga":"sys_02000.tga");if(!request)throw script::Error("Missing native save image request");
    clear();context_=context;inventory_=std::move(next);latest_=latest;pages_={latest[0]/10,latest[1]/10};page_=pages_[0];incoming_=page_data(page_,0);result_label_=std::move(result);image_request_=std::move(request);phase_=SavePhase::loading;
}
bool SaveView::images(){
    for(unsigned issued=0;image_next_<images_.size();){if(image_next_>=14 && image_next_<18 && context_.mode==SaveMode::save){++image_next_;continue;}if(!image_request_){if(issued==2)return false;image_request_=loader_(name(image_next_));++issued;if(!image_request_)throw script::Error("Missing native save image request");}
        auto value=image_request_->get();if(!value)return false;const auto height=image_next_==12?menu_header(context_.theme,context_.title).minimum_height:heights[image_next_];if(value->width<widths[image_next_] || value->height<height)throw script::Unsupported("Native save atlas dimensions are not supported");images_[image_next_++]=std::move(value);image_request_.reset();}
    return true;
}
bool SaveView::poll(PageData& data){
    for(unsigned issued=0;data.next<data.rows.size();){auto& row=data.rows[data.next];if(!row.occupied || !row.diagnostic.empty() || (data.next==10 && context_.mode==SaveMode::save)){++data.next;continue;}
        if(!data.request){if(issued==2)return false;data.request=preview_(row.slot);++issued;if(!data.request)throw script::Error("Missing native save preview request");}
        auto result=data.request->get();if(!result)return false;
        row.diagnostic=std::move(result->diagnostic);row.preview=std::move(result->value);if(!row.preview && row.diagnostic.empty()){row.occupied=false;newly_saved_[row.slot]=false;}
        if(row.preview && !row.preview->legacy)data.labels[data.next]=text_(label(*row.preview,data.next==10?10:20),label_style());data.request.reset();++data.next;
    }
    return true;
}
void SaveView::publish(){current_=std::move(*incoming_);incoming_.reset();page_=current_.page;bank_=current_.bank;pages_[bank_]=page_;for(unsigned n=0;n<rows_.size();++n)rows_[n]=current_.rows[n];regions();}
void SaveView::regions(){hits_.clear();for(unsigned n=0;n<10;++n){const auto& row=rows_[n];hits_.push_back({n,float(8+n/5*632),float(112+n%5*96),632,96,context_.mode==SaveMode::save || (row.occupied && row.diagnostic.empty())});}
    for(unsigned n=0;n<10;++n)hits_.push_back({10+n,float(624+n*64),72,64,32});hits_.push_back({20,1164,688,56,24});hits_.push_back({21,440,600,48,64});hits_.push_back({22,792,600,48,64});
    if(context_.mode==SaveMode::load){const auto& quick=current_.rows[10];hits_.push_back({23,16,608,320,56,quick.occupied && quick.diagnostic.empty()});if(context_.alternate)hits_.push_back({24,896,600,368,40});}}
void SaveView::emit(SaveCommand command){if(command_)throw script::Error("Native save command has not been consumed");command_=command;}
std::optional<SaveCommand> SaveView::take_command(){auto result=command_;command_.reset();return result;}
void SaveView::change_page(unsigned page,unsigned bank){if(page>=10 || bank>1 || (bank && (!context_.alternate || context_.mode!=SaveMode::load)))throw script::Error("Native save page is out of range");if(page==page_ && bank==bank_)return;
    auto incoming=page_data(page,bank);incoming_=std::move(incoming);focus_.reset();elapsed_=0;phase_=SavePhase::page_loading;if(sound_)sound_(audio::SystemCue::accept);
}
void SaveView::activate(unsigned id){
    if(id>=10 && id<20){change_page(id-10,bank_);return;}if(id==21 || id==22){change_page((page_+(id==21?9:1))%10,bank_);return;}if(id==24){change_page(pages_[1-bank_],1-bank_);return;}
    if(id==20){phase_=SavePhase::closing;elapsed_=0;focus_.reset();if(sound_)sound_(audio::SystemCue::cancel);return;}
    if(id>=10 && id!=23)throw script::Error("Unknown native save control");const auto& row=current_.rows[id==23?10:id];
    if(context_.mode==SaveMode::load && (!row.occupied || !row.diagnostic.empty())){if(sound_)sound_(audio::SystemCue::reject);return;}
    const SaveCommand next{context_.mode==SaveMode::save?SaveCommand::Kind::save:SaveCommand::Kind::load,row.slot};
    if(context_.confirmation && (context_.mode==SaveMode::load || row.occupied)){
        auto confirmation=std::make_unique<ConfirmationView>(loader_,[this](auto raw){return text_(raw,prompt_style());},sound_);
        confirmation->prepare(context_.mode==SaveMode::save?save_overwrite_prompt():save_load_prompt(),context_.skin,context_.timing.open,context_.timing.close);selected_=next;confirmation_=std::move(confirmation);phase_=SavePhase::confirming;
    }else{selected_=next;emit(next);phase_=SavePhase::awaiting;}if(sound_)sound_(audio::SystemCue::accept);
}
void SaveView::input(SaveInput input){const auto& in=input.selection;if(input.cancel){activate(20);return;}if(input.page_delta){change_page((page_+(input.page_delta<0?9:1))%10,bank_);return;}
    const auto old=focus_;if(in.pointer){focus_.reset();for(const auto& hit:hits_)if(in.x>=hit.x && in.x<hit.x+hit.width && in.y>=hit.y && in.y<hit.y+hit.height){focus_=hit.id;break;}}
    if(in.navigation){const auto found=std::find_if(hits_.begin(),hits_.end(),[&](const auto& hit){return focus_==hit.id;});const auto n=found==hits_.end()?(in.navigation>0?0:int(hits_.size())-1):(int(found-hits_.begin())+in.navigation+int(hits_.size()))%int(hits_.size());focus_=hits_[unsigned(n)].id;}
    if(focus_ && focus_!=old && sound_)sound_(audio::SystemCue::focus);if(focus_ && (in.confirm || (in.pointer && in.activate))){const auto found=std::find_if(hits_.begin(),hits_.end(),[&](const auto& hit){return hit.id==*focus_;});if(!found->enabled){if(sound_)sound_(audio::SystemCue::reject);return;}pressed_=*focus_;activate(*focus_);}
}
void SaveView::succeeded(std::optional<SavePreview> saved){
    if(phase_!=SavePhase::awaiting || !selected_)throw script::Error("Native save view has no pending operation");
    if(selected_->kind!=SaveCommand::Kind::save)throw script::Error("A successful native load must replace its owner");
    if(!saved)throw script::Error("Native save operation did not provide its preview");saved->validate();
    const auto slot=selected_->slot;const auto offset=slot-page_*10;if(bank_ || offset>=10)throw script::Error("Native save result does not match its displayed page");auto text=text_(label(*saved),label_style());auto& row=current_.rows[offset];row.occupied=true;row.preview=std::move(saved);row.diagnostic.clear();current_.labels[offset]=std::move(text);rows_[offset]=row;newly_saved_[slot]=true;
    latest_[0]=slot;selected_.reset();confirmation_.reset();operation_error_.clear();elapsed_=0;phase_=SavePhase::result;draw();
}
void SaveView::failed(std::string diagnostic){if(phase_!=SavePhase::awaiting)throw script::Error("Native save view has no failed operation");operation_error_=std::move(diagnostic);selected_.reset();confirmation_.reset();phase_=SavePhase::active;regions();draw();}
void SaveView::update(SaveInput in){
    if(!std::isfinite(in.selection.x) || !std::isfinite(in.selection.y) || std::abs(std::int64_t(in.selection.navigation))>1 || std::abs(std::int64_t(in.page_delta))>1)throw script::Error("Native save input is out of range");
    if(phase_==SavePhase::idle)throw script::Error("Native save view is not prepared");if(command_ || phase_==SavePhase::closed)return;pressed_.reset();
    if(in.cancel && (phase_==SavePhase::loading || phase_==SavePhase::page_loading)){incoming_.reset();phase_=SavePhase::closing;elapsed_=0;}
    if(phase_==SavePhase::loading){if(!images() || !poll(*incoming_))return;publish();phase_=SavePhase::opening;elapsed_=0;}
    if(phase_==SavePhase::page_loading){if(!poll(*incoming_)){draw();return;}phase_=SavePhase::page_out;elapsed_=0;}
    switch(phase_){
    case SavePhase::opening:if(++elapsed_>=context_.timing.open){phase_=SavePhase::active;elapsed_=0;}break;
    case SavePhase::page_out:if(++elapsed_>=context_.timing.page){publish();phase_=SavePhase::page_in;elapsed_=0;}break;
    case SavePhase::page_in:if(++elapsed_>=context_.timing.page){phase_=SavePhase::active;elapsed_=0;}break;
    case SavePhase::active:input(in);break;
    case SavePhase::confirming:
        confirmation_->update({in.selection,in.cancel});if(auto result=confirmation_->take_answer();result && *result){emit(*selected_);phase_=SavePhase::awaiting;}
        else if(confirmation_->phase()==ConfirmationPhase::closed){confirmation_.reset();selected_.reset();phase_=SavePhase::active;}break;
    case SavePhase::result:if(++elapsed_>=context_.result_ticks){confirmation_.reset();phase_=SavePhase::active;elapsed_=0;regions();}break;
    case SavePhase::closing:if(++elapsed_>=context_.timing.close){phase_=SavePhase::closed;emit({SaveCommand::Kind::close,0});}break;
    default:break;
    }draw();
}
void SaveView::draw(){
    sprites_.clear();if(image_next_<images_.size() || !images_[0] || phase_==SavePhase::closed || phase_==SavePhase::idle || phase_==SavePhase::loading)return;
    const auto fraction=[&](unsigned ticks){return ticks?std::min(1.f,float(elapsed_)/ticks):1.f;};const auto whole=phase_==SavePhase::opening?fraction(context_.timing.open):phase_==SavePhase::closing?1-fraction(context_.timing.close):1.f;
    const auto body=phase_==SavePhase::page_out?1-fraction(context_.timing.page):phase_==SavePhase::page_in?fraction(context_.timing.page):whole;const auto scale=phase_==SavePhase::page_in?1-.05f*(1-body):1+.1f*(1-body);
    const auto push=[&](std::shared_ptr<const image::Raster> image,float x,float y,float w,float h,float sx,float sy,int layer,int order,float alpha,float zoom=1){sprites_.push_back({std::move(image),640+(x-640)*zoom,360+(y-360)*zoom,w*zoom,h*zoom,sx,sy,w,h,255,255,255,opacity(alpha)});sprites_.back().layer=layer;sprites_.back().order=order;};
    const auto item=[&](unsigned n,float x,float y,float w,float h,float sx,float sy,int layer,int order,float alpha,float zoom=1){push(images_[n],x,y,w,h,sx,sy,layer,order,alpha,zoom);};
    push(black_,0,0,1280,720,0,0,0x1061,0x4a8,128*whole);sprites_.back().source_width=sprites_.back().source_height=1;
    item(0,0,-32*(1-whole),1280,40,0,0,0x1062,0x4a4,256*whole);item(1,-64,664+32*(1-whole),1280,56,0,0,0x1062,0x4a5,256*whole);
    const auto header=menu_header(context_.theme,context_.title);item(12,40,float(header.y),304,168,0,float(header.y),0x1062,0x4a7,256*whole);
    item(5,440,64,840,48,0,0,0x1063,0x4fe,256*whole);item(5,440,64,840,48,0,48,0x1062,0x4ff,256*whole);
    for(unsigned n=0;n<10;++n){const auto selected=page_==n;item(6,float(624+n*64),72,64,32,float(n*64),selected?0:focus_==10+n?64:32,selected?0x1064:0x1062,0x4e0+int(n),256*whole);}
    const auto status=[&](unsigned id){return pressed_==id?2:focus_==id?1:0;};item(8,440,600,48,64,float(status(21)*48),0,0x1063,0x4ab,256*whole);item(8,792,600,48,64,float(status(22)*48),64,0x1063,0x4ac,256*whole);item(8,496,608,288,40,144,0,0x1063,0x4fc,256*whole);item(9,1164,688,56,24,0,float(status(20)*24),0x1063,0x4ad,256*whole);
    for(unsigned n=0;n<10;++n){const auto row_first=sprites_.size();const auto& row=current_.rows[n];const auto x=float(n/5*632),y=float(n%5*96);const auto alpha=opacity(256*body);
        item(2,x+8,y+112,632,96,row.occupied?632:0,float(bank_*96),0x1063,0x4ae + int(n),256*body,scale);
        if(focus_==n)item(3,x+8,y+112,632,96,0,0,0x1064,0x4a9,256*body,scale);
        if(row.preview && row.preview->thumbnail){push(row.preview->thumbnail,x+64,y+122,128,72,0,0,0x1064,0x4b8+int(n),alpha,scale);sprites_.back().source_width=float(row.preview->thumbnail->width);sprites_.back().source_height=float(row.preview->thumbnail->height);}
        const auto first=sprites_.size();menu_digits(sprites_,images_[7],row.preview && !row.preview->legacy?date(row.slot,*row.preview):number(row.slot),x+20,y+132,alpha,0x1065,0x4cc+int(n));
        current_.labels[n].append(sprites_,0,100000,x+196,y+146,alpha);for(auto i=first;i<sprites_.size();++i){auto& sprite=sprites_[i];if(i>=first && sprite.order==0){sprite.layer=0x1064;sprite.order=0x4d6+int(n);}sprite.x=640+(sprite.x-640)*scale;sprite.y=360+(sprite.y-360)*scale;sprite.width*=scale;sprite.height*=scale;}
        if(row.preview && row.preview->story_known){const auto code=row.preview->story_code,type=code/10%10,sub=code%10;
            // Cached 0x41ef5c: types 3/4 have no chapter badge. The two
            // ending groups index rows 6+sub and 11+sub in the same sheet.
            if(type<=2 || type==5 || type==6){const auto tile=type==5?6+sub:type==6?11+sub:type;if(tile*24<images_[11]->height)item(11,x+388,y+128,248,24,0,float(tile*24),0x1064,0x4c2+int(n),alpha,scale);}
            if(type==1 || type==2){if(sub){if((sub+2)*24<images_[11]->height)item(11,x+378,y+128,248,24,0,float((sub+2)*24),0x1064,0x508+int(n),alpha,scale);}
                else{const auto month=code/10000%100,day=code/100%100;if(month<1 || month>12 || day<1 || day>31)throw script::Unsupported("Native save calendar badge is out of range");
                    item(10,x+546,y+128,40,24,float((month-1)/4*40),float((month-1)%4*24),0x1064,0x508+int(n),alpha,scale);
                    std::array<char,4> digits{};std::snprintf(digits.data(),digits.size(),"%02u",day);const auto first=sprites_.size();menu_digits(sprites_,images_[7],digits.data(),x+594,y+128,alpha,0x1065,0x512+int(n));for(auto i=first;i<sprites_.size();++i){auto& sprite=sprites_[i];sprite.x=640+(sprite.x-640)*scale;sprite.y=360+(sprite.y-360)*scale;sprite.width*=scale;sprite.height*=scale;}
                }}
        }
        if(row.slot%100==latest_[bank_] && row.occupied)item(4,x+192,y+112,160,20,0,0,0x1064,0x4aa,256*body,scale);
        // SetGraphBrightness(128) is source-neutral; 64 maps to RGB 128
        // without reducing alpha (cached 0x43b290).
        if(!row.diagnostic.empty())for(auto i=row_first;i<sprites_.size();++i){auto& sprite=sprites_[i];sprite.red=std::uint8_t(unsigned(sprite.red)*128/255);sprite.green=std::uint8_t(unsigned(sprite.green)*128/255);sprite.blue=std::uint8_t(unsigned(sprite.blue)*128/255);}
    }
    if(context_.mode==SaveMode::load){const auto& quick=current_.rows[10];const bool enabled=quick.occupied && quick.diagnostic.empty();
        item(17,16,608,320,56,0,enabled?232:176,0x1063,0x4f4,256*whole);item(15,16,592,112,16,0,0,0x1063,0x4f5,256*whole);
        if(focus_==23)item(14,16,608,320,56,0,0,0x1064,0x4f6,256*whole);
        if(quick.preview){const auto& preview=*quick.preview;if(preview.thumbnail){push(preview.thumbnail,40,614,64,36,0,0,0x1064,0x4f9,(enabled?256:64)*whole);sprites_.back().source_width=float(preview.thumbnail->width);sprites_.back().source_height=float(preview.thumbnail->height);}if(!preview.legacy){menu_digits(sprites_,images_[7],date(quick_slot,preview).substr(15),112,616,opacity((enabled?256:64)*whole),0x1064,0x4f7);const auto first=sprites_.size();current_.labels[10].append(sprites_,0,100000,106,628,opacity((enabled?256:64)*whole));for(auto n=first;n<sprites_.size();++n){sprites_[n].layer=0x1064;sprites_[n].order=0x4f8;}}}
        if(context_.alternate){item(16,896,600,368,40,0,float((1-bank_)*120+status(24)*40),0x1063,0x4fa,256*whole);if(focus_==24)item(16,832,640,432,16,0,280,0x1063,0x4fb,256*whole);}
    }
    if(current_music_ && phase_!=SavePhase::opening && phase_!=SavePhase::closing){int order=0x43f;append_music_caption(sprites_,images_[18],images_[19],current_music_(),0x1063,order);}
    if(phase_==SavePhase::result){push(black_,0,0,1280,720,0,0,0x1066,0x4f1,76);sprites_.back().source_width=sprites_.back().source_height=1;item(13,400,304,480,112,0,0,0x1067,0x500,255);const auto first=sprites_.size();result_label_.append(sprites_,0,100000,450,345,255);for(auto n=first;n<sprites_.size();++n){sprites_[n].layer=0x1067;sprites_[n].order=0x501;}}
    if(confirmation_ && phase_!=SavePhase::result)sprites_.insert(sprites_.end(),confirmation_->sprites().begin(),confirmation_->sprites().end());
    std::stable_sort(sprites_.begin(),sprites_.end(),[](const auto& a,const auto& b){return a.layer==b.layer?a.order<b.order:a.layer<b.layer;});
}
std::string_view save_overwrite_prompt()noexcept{return "<F20\x81\x40\x81\x40\x81\x40\x8d\x5c\x91\x94\x8d\x9f\x97\xc8\x97\xdb\x94\x6d\x81\x42\x81\x40\x81\x40\x81\x40\n\x81\x40\x81\x40\x81\x40\x81\x40\x94\xbb\x8d\x73\x8b\x4c\x81\x48\x81\x40\x81\x40>";}
std::string_view save_load_prompt()noexcept{return "<F20\x81\x40\x81\x40\x81\x40\x81\x40\x98\x6d\x8a\xe0\x8d\x5c\x91\x94\x81\x42\x81\x40\x81\x40\x81\x40\n\x81\x40\x81\x40\x81\x40\x81\x40\x94\xbb\x8d\x73\x8b\x4c\x81\x48\x81\x40\x81\x40>";}
std::string_view save_result_prompt()noexcept{return "<F20 \x81\x40\x81\x40\x81\x40\x8d\x5c\x91\x94\x8d\xe5\x89\x97\x8d\x5c\x81\x40\x81\x40\x81\x40\x81\x40>";}
}
