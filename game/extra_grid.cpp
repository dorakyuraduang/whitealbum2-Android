#include "extra_grid.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wa2::game {
namespace {
std::string image_name(std::string_view prefix,unsigned id){if(id>=1000000)throw script::Error("Native extra image identifier is out of range");std::array<char,48> name{};std::snprintf(name.data(),name.size(),"%.*s%06u.tga",int(prefix.size()),prefix.data(),id);return name.data();}
constexpr unsigned chrome(ExtraGridKind kind){return kind==ExtraGridKind::replay?15:12;}
}
ExtraGrid::ExtraGrid(Loader loader,Sound sound):loader_(std::move(loader)),sound_(std::move(sound)){
    if(!loader_)throw script::Error("Missing native extra-grid image loader");auto black=std::make_shared<image::Raster>();black->width=black->height=1;black->rgba={0,0,0,255};black_=std::move(black);sprites_.reserve(64);
}
void ExtraGrid::clear(){pages_.clear();data_={};pending_.reset();full_request_.reset();full_.reset();previous_full_.reset();variants_.clear();sprites_.clear();focus_.reset();command_.reset();leaving_.reset();page_=variant_=full_id_=elapsed_=0;timing_={};close_from_=1;coda_complete_=false;phase_=ExtraGridPhase::idle;}
void ExtraGrid::prepare(ExtraGridKind kind,TitleContent content,const progress::State& progress,unsigned page,MenuTiming timing){
    timing.validate();
    if(kind!=ExtraGridKind::gallery && kind!=ExtraGridKind::replay)throw script::Error("Unknown native extra-grid kind");
    std::vector<unsigned> pages;const bool introductory=(content.local_introductory || content.external_introductory) && progress.flag(0)!=0;
    if(kind==ExtraGridKind::gallery){for(unsigned n=introductory?0:3;n<(progress.flag(5)?14:9);++n)pages.push_back(n);}else pages={0,1};
    if(std::find(pages.begin(),pages.end(),page)==pages.end())page=pages.front();
    // Preparing is a transaction even if the first resource adapter rejects it.
    ExtraGrid next(loader_,sound_);next.kind_=kind;next.content_=content;next.pages_=std::move(pages);next.timing_=timing;next.coda_complete_=progress.flag(5)!=0;
    for(unsigned group=0;group<extras::gallery.size();++group)for(unsigned n=0;n<10;++n){const auto id=extras::gallery[group][n];next.gallery_[group][n]=id && progress.gallery(0,id)!=0;}
    for(unsigned n=0;n<extras::replays.size();++n){const auto& row=extras::replays[n];next.replay_[n]=progress.flag(row.flag)!=0 && (row.theme!=0 || content.local_introductory || content.external_introductory);}
    next.begin_page(page);*this=std::move(next);
}
ExtraGrid::PageData ExtraGrid::plan(unsigned page)const{
    if(std::find(pages_.begin(),pages_.end(),page)==pages_.end())throw script::Error("Native extra-grid page is unavailable");PageData next;next.page=page;
    const bool gallery=kind_==ExtraGridKind::gallery;const auto theme=gallery?(page<3?0:page<9?1:2):0;
    const auto asset=[&](std::string name){const auto old=std::find_if(data_.assets.begin(),data_.assets.end(),[&](const auto& value){return value.name==name;});next.assets.push_back({std::move(name),old==data_.assets.end()?nullptr:old->pixels});};
    for(const auto& name:std::array<std::string,12>{gallery?"sys_05000.tga":"sys_06000.tga","sys_09000.tga","sys_05100.tga","sys_05110.tga","sys_05020.tga","sys_09100.tga","sys_05201.tga",gallery?"sys_05021.tga":"sys_06021.tga","sys_05900.tga","sys_05901.tga","sys_05902.tga","sys_05200.tga"})asset(name);
    if(!gallery)for(const auto name:{"sys_05903.tga","sys_05300.tga","sys_05301.tga"})asset(name);
    for(unsigned ordinal=0;ordinal<12;++ordinal){ExtraGridRow row;row.ordinal=ordinal;row.index=page*12+ordinal;row.theme=unsigned(theme);
        if(gallery){const auto& variants=extras::gallery.at(row.index);for(unsigned n=0;n<10;++n)if(variants[n] && gallery_[row.index][n])row.variants.push_back(variants[n]);row.enabled=!row.variants.empty();row.thumbnail=variants[0];
            // The source chooses the first seen thumbnail for these two groups.
            if((row.thumbnail==210300 || row.thumbnail==210400) && row.enabled)row.thumbnail=row.variants.front();
        }else{const auto& replay=extras::replays.at(row.index);row.thumbnail=replay.thumbnail;row.theme=replay.theme;row.enabled=replay_[row.index];}
        if(row.theme>2)throw script::Unsupported("Native extra placeholder theme is out of range");if(row.enabled)asset(image_name("tv",row.thumbnail));next.rows.push_back(std::move(row));
    }return next;
}
void ExtraGrid::begin_page(unsigned page){
    auto next=plan(page);unsigned first{};while(first<next.assets.size() && next.assets[first].pixels)++first;std::shared_ptr<image::ImageRequest> request;
    if(first<next.assets.size()){request=loader_(next.assets[first].name);if(!request)throw script::Error("Missing native extra-grid image request");}pending_=Pending{std::move(next),first,std::move(request)};phase_=ExtraGridPhase::loading;focus_.reset();draw();
}
bool ExtraGrid::poll(){
    if(!pending_)throw script::Error("Native extra-grid lost its page request");auto& pending=*pending_;
    constexpr unsigned widths[]{1280,1280,272,272,144,56,840,288,272,272,272,1000,272,288,288},heights[]{36,56,160,160,128,72,96,40,160,160,160,24,160,24,384};
    try{for(unsigned issued=0;pending.next<pending.data.assets.size();){auto& asset=pending.data.assets[pending.next];if(asset.pixels){++pending.next;continue;}if(!pending.request){if(issued==2)return false;pending.request=loader_(asset.name);++issued;if(!pending.request)throw script::Error("Missing native extra-grid image request");}
            auto image=pending.request->get();if(!image)return false;
            if(!image->width || !image->height || (pending.next<chrome(kind_) && (image->width<widths[pending.next] || image->height<heights[pending.next])))throw script::Unsupported("Native extra-grid atlas dimensions are not supported: "+asset.name);
            asset.pixels=std::move(image);pending.request.reset();++pending.next;}
    }catch(...){pending_.reset();phase_=data_.assets.empty()?ExtraGridPhase::idle:ExtraGridPhase::cards;throw;}
    elapsed_=0;if(data_.assets.empty()){commit_page();phase_=timing_.open?ExtraGridPhase::opening:ExtraGridPhase::cards;}
    else if(timing_.page)phase_=ExtraGridPhase::page_out;else{commit_page();phase_=ExtraGridPhase::cards;}draw();return true;
}
void ExtraGrid::commit_page(){if(!pending_ || pending_->next!=pending_->data.assets.size())throw script::Error("Native extra page is not ready to commit");page_=pending_->data.page;data_=std::move(pending_->data);pending_.reset();}
float ExtraGrid::visibility()const noexcept{
    const auto fraction=[&](unsigned duration){return duration?std::min(1.f,float(elapsed_)/duration):1.f;};
    if(phase_==ExtraGridPhase::idle || phase_==ExtraGridPhase::closed || data_.assets.empty())return 0;
    return phase_==ExtraGridPhase::opening?fraction(timing_.open):phase_==ExtraGridPhase::closing?close_from_*(1-fraction(timing_.close)):1.f;
}
void ExtraGrid::turn(int delta){
    if(delta!=-1 && delta!=1)throw script::Error("Native extra-grid page step is out of range");const auto current=std::find(pages_.begin(),pages_.end(),page_);if(current==pages_.end())throw script::Error("Native extra-grid current page is unavailable");
    const auto index=(int(current-pages_.begin())+delta+int(pages_.size()))%int(pages_.size());begin_page(pages_[unsigned(index)]);if(sound_)sound_(audio::SystemCue::focus);
}
void ExtraGrid::full(unsigned index){
    if(index>=variants_.size())throw script::Error("Native gallery variant is unavailable");auto request=loader_(image_name("v",variants_[index]));if(!request)throw script::Error("Missing native gallery image request");
    full_request_=std::move(request);pending_variant_=index;phase_=ExtraGridPhase::full_loading;
}
void ExtraGrid::cards(){full_request_.reset();full_.reset();previous_full_.reset();variants_.clear();focus_.reset();elapsed_=0;phase_=ExtraGridPhase::cards;draw();}
void ExtraGrid::finish_close(){
    if(!leaving_)throw script::Error("Native extra exit has no destination");command_=*leaving_;leaving_.reset();elapsed_=0;
    // A replay keeps this exact page as its return owner. Its exit command is
    // consumed before another frame; failed story preparation can retry here.
    phase_=command_->kind==ExtraGridCommand::Kind::replay?ExtraGridPhase::cards:ExtraGridPhase::closed;
}
void ExtraGrid::leave(ExtraGridCommand destination){
    close_from_=visibility();elapsed_=0;focus_.reset();pending_.reset();full_request_.reset();leaving_=destination;
    if(data_.assets.empty() || !timing_.close){finish_close();draw();return;}
    phase_=ExtraGridPhase::closing;command_=ExtraGridCommand{ExtraGridCommand::Kind::leaving};
}
void ExtraGrid::activate(unsigned id){
    if(id==50){leave({ExtraGridCommand::Kind::close});if(sound_)sound_(audio::SystemCue::cancel);return;}
    if(id==51 || id==52){turn(id==51?-1:1);return;}if(id>=100 && id<114){const auto page=id-100;if(page!=page_)begin_page(page);return;}
    if(id>=data_.rows.size())throw script::Error("Unknown native extra-grid control");const auto& row=data_.rows[id];if(!row.enabled){if(sound_)sound_(audio::SystemCue::reject);return;}
    if(kind_==ExtraGridKind::replay)leave({ExtraGridCommand::Kind::replay,row.index+1});else{auto old=variants_;variants_=row.variants;try{full(0);}catch(...){variants_=std::move(old);throw;}}
    if(sound_)sound_(audio::SystemCue::accept);
}
std::optional<ExtraGridCommand> ExtraGrid::take_command(){auto command=command_;command_.reset();return command;}
void ExtraGrid::update(ExtraGridInput input){
    const auto& selected=input.selection;if(!std::isfinite(selected.x) || !std::isfinite(selected.y) || std::abs(std::int64_t(selected.navigation))>1 || std::abs(std::int64_t(input.page_delta))>1)throw script::Error("Native extra-grid input is out of range");
    if(phase_==ExtraGridPhase::idle)throw script::Error("Native extra-grid is not prepared");if(phase_==ExtraGridPhase::closed || command_)return;
    if(input.cancel && phase_!=ExtraGridPhase::closing){if(phase_==ExtraGridPhase::full || phase_==ExtraGridPhase::full_loading || phase_==ExtraGridPhase::full_in){cards();if(sound_)sound_(audio::SystemCue::cancel);}else activate(50);draw();return;}
    if(phase_==ExtraGridPhase::loading){poll();return;}
    if(phase_==ExtraGridPhase::opening || phase_==ExtraGridPhase::page_out || phase_==ExtraGridPhase::page_in || phase_==ExtraGridPhase::closing || phase_==ExtraGridPhase::full_in){
        if(phase_==ExtraGridPhase::opening && ++elapsed_>=timing_.open){phase_=ExtraGridPhase::cards;elapsed_=0;}
        else if(phase_==ExtraGridPhase::page_out && ++elapsed_>=timing_.page){commit_page();phase_=ExtraGridPhase::page_in;elapsed_=0;}
        else if(phase_==ExtraGridPhase::page_in && ++elapsed_>=timing_.page){phase_=ExtraGridPhase::cards;elapsed_=0;}
        else if(phase_==ExtraGridPhase::closing && ++elapsed_>=timing_.close)finish_close();
        else if(phase_==ExtraGridPhase::full_in && ++elapsed_>=16){previous_full_.reset();phase_=ExtraGridPhase::full;elapsed_=0;}
        draw();return;
    }
    if(phase_==ExtraGridPhase::full_loading){try{auto image=full_request_->get();if(!image)return;if(!image->width || !image->height)throw script::Error("Native gallery image is empty");previous_full_=std::move(full_);full_=std::move(image);variant_=pending_variant_;full_id_=variants_.at(variant_);full_request_.reset();elapsed_=0;phase_=ExtraGridPhase::full_in;draw();}catch(...){full_request_.reset();if(full_)phase_=ExtraGridPhase::full;else cards();throw;}return;}
    if(phase_==ExtraGridPhase::full){if(input.page_delta<0){if(variant_)full(variant_-1);}else if(input.page_delta>0 || selected.confirm || (selected.pointer && selected.activate)){if(variant_+1<variants_.size())full(variant_+1);else cards();}return;}
    if(input.page_delta){turn(input.page_delta);return;}const auto previous=focus_;
    if(selected.pointer){focus_.reset();for(const auto& row:data_.rows){const float x=104.f+272*(row.ordinal%4),y=116.f+160*(row.ordinal/4);if(row.enabled && selected.x>=x && selected.x<x+256 && selected.y>=y && selected.y<y+144){focus_=row.ordinal;break;}}
        if(selected.x>=1164 && selected.x<1220 && selected.y>=688 && selected.y<712)focus_=50;
        if(selected.y>=600 && selected.y<664){if(selected.x>=440 && selected.x<488)focus_=51;if(selected.x>=792 && selected.x<840)focus_=52;}
        if(selected.y>=72 && selected.y<104)for(auto page:pages_){const auto x=428+60*(page-pages_.front());if(selected.x>=x && selected.x<x+60)focus_=100+page;}}
    if(selected.navigation){std::vector<unsigned> controls;for(const auto& row:data_.rows)if(row.enabled)controls.push_back(row.ordinal);controls.insert(controls.end(),{51,52,50});const auto at=std::find(controls.begin(),controls.end(),focus_.value_or(999));const int size=int(controls.size());const auto index=at==controls.end()?(selected.navigation>0?0:size-1):(int(at-controls.begin())+selected.navigation+size)%size;focus_=controls[unsigned(index)];}
    if(focus_ && focus_!=previous && sound_)sound_(audio::SystemCue::focus);if(focus_ && (selected.confirm || (selected.pointer && selected.activate)))activate(*focus_);if(phase_==ExtraGridPhase::cards)draw();
}
void ExtraGrid::draw(){
    sprites_.clear();if(data_.assets.empty() || phase_==ExtraGridPhase::closed)return;int order{};
    const auto whole=visibility(),fraction=timing_.page?std::min(1.f,float(elapsed_)/timing_.page):1.f;
    const auto content=phase_==ExtraGridPhase::page_out?1-fraction:phase_==ExtraGridPhase::page_in?fraction:1.f;
    const auto scale=1+.1f*(1-whole),card_scale=1+.1f*(1-whole*content);
    const auto add=[&](std::shared_ptr<const image::Raster> image,float x,float y,float w,float h,float sx,float sy,float sw,float sh,unsigned alpha=255,float factor=1){sprites_.push_back({std::move(image),640+(x-640)*factor,360+(y-360)*factor,w*factor,h*factor,sx,sy,sw,sh,255,255,255,std::uint8_t(std::min(255u,unsigned(float(alpha)*whole)))});sprites_.back().layer=0x1060;sprites_.back().order=order++;};
    const bool cards_visible=!full_ || (phase_==ExtraGridPhase::full_in && !previous_full_);
    add(black_,0,0,1280,720,0,0,1,1,cards_visible?128:255);
    if(cards_visible){
    const auto& assets=data_.assets;add(assets[0].pixels,0,-32*(1-whole),1280,36,0,0,1280,36);add(assets[1].pixels,0,664+32*(1-whole),1280,56,0,0,1280,56);
    for(auto page:pages_)add(assets[6].pixels,428.f+60*(page-pages_.front()),72,60,32,float(page*60),page==page_?32:focus_==100+page?64:0,60,32,255,scale);
    add(assets[11].pixels,280,86,1000,24,0,0,1000,24,255,scale);
    const auto card_alpha=unsigned(256*content);
    unsigned thumb=chrome(kind_);for(const auto& row:data_.rows){const float x=96.f+272*(row.ordinal%4),y=112.f+160*(row.ordinal/4);
        // Cached replay branches 42e5bc/42f4ea distinguish an uncleared CODA
        // placeholder from a cleared chapter with an unseen individual scene.
        auto card_index=row.enabled?2u:8+row.theme;
        if(!row.enabled && kind_==ExtraGridKind::replay && row.theme==2 && !coda_complete_)card_index=12;
        const auto& card=assets[card_index].pixels;add(card,x,y,272,160,0,0,272,160,card_alpha,card_scale);
        if(row.enabled){const auto& image=assets.at(thumb++).pixels;add(image,x+8,y+4,256,144,0,0,float(image->width),float(image->height),card_alpha,card_scale);}
        if(focus_==row.ordinal && row.enabled)add(assets[3].pixels,x,y,272,160,0,0,272,160,card_alpha,card_scale);}
    if(kind_==ExtraGridKind::replay){
        add(assets[13].pixels,104,624,288,24,0,0,288,24,255,scale);
        // Cached 42ef39 uses the enabled hit selected by 44b990; locked
        // rectangles cannot reveal a scene's description through hover.
        if(focus_ && *focus_<data_.rows.size() && data_.rows[*focus_].enabled)add(assets[14].pixels,104,608,288,16,0,float(data_.rows[*focus_].index*16),288,16,255,scale);
    }
    add(assets[4].pixels,440,600,48,64,focus_==51?96:0,0,48,64,255,scale);add(assets[4].pixels,792,600,48,64,focus_==52?96:0,64,48,64,255,scale);
    add(assets[7].pixels,496,608,288,40,0,0,288,40,255,scale);add(assets[5].pixels,1164,688+32*(1-whole),56,24,0,focus_==50?24:0,56,24);
    }
    if(previous_full_)add(previous_full_,0,0,1280,720,0,0,float(previous_full_->width),float(previous_full_->height));
    if(full_)add(full_,0,0,1280,720,0,0,float(full_->width),float(full_->height),phase_==ExtraGridPhase::full_in?std::min(255u,elapsed_*16):255);
}
}
