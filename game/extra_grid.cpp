#include "extra_grid.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wa2::game {
namespace {
std::string image_name(std::string_view prefix,unsigned id){if(id>=1000000)throw script::Error("Native extra image identifier is out of range");std::array<char,48> name{};std::snprintf(name.data(),name.size(),"%.*s%06u.tga",int(prefix.size()),prefix.data(),id);return name.data();}
constexpr unsigned chrome=10;
}
ExtraGrid::ExtraGrid(Loader loader,Sound sound):loader_(std::move(loader)),sound_(std::move(sound)){
    if(!loader_)throw script::Error("Missing native extra-grid image loader");auto black=std::make_shared<image::Raster>();black->width=black->height=1;black->rgba={0,0,0,255};black_=std::move(black);sprites_.reserve(64);
}
void ExtraGrid::clear(){pages_.clear();data_={};pending_.reset();full_request_.reset();full_.reset();variants_.clear();sprites_.clear();focus_.reset();command_.reset();page_=variant_=full_id_=0;phase_=ExtraGridPhase::idle;}
void ExtraGrid::prepare(ExtraGridKind kind,TitleContent content,const progress::State& progress,unsigned page){
    if(kind!=ExtraGridKind::gallery && kind!=ExtraGridKind::replay)throw script::Error("Unknown native extra-grid kind");
    std::vector<unsigned> pages;const bool introductory=(content.local_introductory || content.external_introductory) && progress.flag(0)!=0;
    if(kind==ExtraGridKind::gallery){for(unsigned n=introductory?0:3;n<(progress.flag(5)?14:9);++n)pages.push_back(n);}else pages={0,1};
    if(std::find(pages.begin(),pages.end(),page)==pages.end())page=pages.front();
    // Preparing is a transaction even if the first resource adapter rejects it.
    ExtraGrid next(loader_,sound_);next.kind_=kind;next.content_=content;next.pages_=std::move(pages);
    for(unsigned group=0;group<extras::gallery.size();++group)for(unsigned n=0;n<10;++n){const auto id=extras::gallery[group][n];next.gallery_[group][n]=id && progress.gallery(0,id)!=0;}
    for(unsigned n=0;n<extras::replays.size();++n){const auto& row=extras::replays[n];next.replay_[n]=progress.flag(row.flag)!=0 && (row.theme!=0 || content.local_introductory || content.external_introductory);}
    next.begin_page(page);*this=std::move(next);
}
ExtraGrid::PageData ExtraGrid::plan(unsigned page)const{
    if(std::find(pages_.begin(),pages_.end(),page)==pages_.end())throw script::Error("Native extra-grid page is unavailable");PageData next;next.page=page;
    const bool gallery=kind_==ExtraGridKind::gallery;const auto theme=gallery?(page<3?0:page<9?1:2):0;
    for(const auto& name:std::array<std::string,chrome>{gallery?"sys_05000.tga":"sys_06000.tga","sys_09000.tga","sys_05100.tga","sys_05110.tga","sys_05020.tga","sys_09100.tga","sys_05201.tga",gallery?"sys_05021.tga":"sys_06021.tga","sys_05900.tga","sys_05901.tga"})next.assets.push_back({name,{}});
    for(unsigned ordinal=0;ordinal<12;++ordinal){ExtraGridRow row;row.ordinal=ordinal;row.index=page*12+ordinal;row.theme=unsigned(theme);
        if(gallery){const auto& variants=extras::gallery.at(row.index);for(unsigned n=0;n<10;++n)if(variants[n] && gallery_[row.index][n])row.variants.push_back(variants[n]);row.enabled=!row.variants.empty();row.thumbnail=variants[0];
            // The source chooses the first seen thumbnail for these two groups.
            if((row.thumbnail==210300 || row.thumbnail==210400) && row.enabled)row.thumbnail=row.variants.front();
        }else{const auto& replay=extras::replays.at(row.index);row.thumbnail=replay.thumbnail;row.theme=replay.theme;row.enabled=replay_[row.index];}
        if(row.enabled)next.assets.push_back({image_name("tv",row.thumbnail),{}});next.rows.push_back(std::move(row));
    }return next;
}
void ExtraGrid::begin_page(unsigned page){
    auto next=plan(page);auto request=loader_(next.assets.front().name);if(!request)throw script::Error("Missing native extra-grid image request");pending_=Pending{std::move(next),0,std::move(request)};phase_=ExtraGridPhase::loading;focus_.reset();
}
bool ExtraGrid::poll(){
    if(!pending_)throw script::Error("Native extra-grid lost its page request");auto& pending=*pending_;
    constexpr unsigned widths[]{1280,1280,272,272,144,56,840,288,272,272},heights[]{36,56,160,160,128,72,96,40,160,160};
    try{for(unsigned issued=0;pending.next<pending.data.assets.size();){auto& asset=pending.data.assets[pending.next];if(!pending.request){if(issued==2)return false;pending.request=loader_(asset.name);++issued;if(!pending.request)throw script::Error("Missing native extra-grid image request");}
            auto image=pending.request->get();if(!image)return false;
            if(!image->width || !image->height || (pending.next<chrome && (image->width<widths[pending.next] || image->height<heights[pending.next])))throw script::Unsupported("Native extra-grid atlas dimensions are not supported: "+asset.name);
            asset.pixels=std::move(image);pending.request.reset();++pending.next;}
    }catch(...){pending_.reset();phase_=data_.assets.empty()?ExtraGridPhase::idle:ExtraGridPhase::cards;throw;}
    page_=pending.data.page;data_=std::move(pending.data);pending_.reset();phase_=ExtraGridPhase::cards;draw();return true;
}
void ExtraGrid::turn(int delta){
    if(delta!=-1 && delta!=1)throw script::Error("Native extra-grid page step is out of range");const auto current=std::find(pages_.begin(),pages_.end(),page_);if(current==pages_.end())throw script::Error("Native extra-grid current page is unavailable");
    const auto index=(int(current-pages_.begin())+delta+int(pages_.size()))%int(pages_.size());begin_page(pages_[unsigned(index)]);if(sound_)sound_(audio::SystemCue::focus);
}
void ExtraGrid::full(unsigned index){
    if(index>=variants_.size())throw script::Error("Native gallery variant is unavailable");auto request=loader_(image_name("v",variants_[index]));if(!request)throw script::Error("Missing native gallery image request");
    full_request_=std::move(request);pending_variant_=index;phase_=ExtraGridPhase::full_loading;
}
void ExtraGrid::cards(){full_request_.reset();full_.reset();variants_.clear();focus_.reset();phase_=ExtraGridPhase::cards;draw();}
void ExtraGrid::activate(unsigned id){
    if(id==50){command_=ExtraGridCommand{ExtraGridCommand::Kind::close};pending_.reset();full_request_.reset();phase_=ExtraGridPhase::closed;sprites_.clear();if(sound_)sound_(audio::SystemCue::cancel);return;}
    if(id==51 || id==52){turn(id==51?-1:1);return;}if(id>=100 && id<114){const auto page=id-100;if(page!=page_)begin_page(page);return;}
    if(id>=data_.rows.size())throw script::Error("Unknown native extra-grid control");const auto& row=data_.rows[id];if(!row.enabled){if(sound_)sound_(audio::SystemCue::reject);return;}
    if(kind_==ExtraGridKind::replay)command_=ExtraGridCommand{ExtraGridCommand::Kind::replay,row.index+1};else{auto old=variants_;variants_=row.variants;try{full(0);}catch(...){variants_=std::move(old);throw;}}
    if(sound_)sound_(audio::SystemCue::accept);
}
std::optional<ExtraGridCommand> ExtraGrid::take_command(){auto command=command_;command_.reset();return command;}
void ExtraGrid::update(ExtraGridInput input){
    const auto& selected=input.selection;if(!std::isfinite(selected.x) || !std::isfinite(selected.y) || std::abs(std::int64_t(selected.navigation))>1 || std::abs(std::int64_t(input.page_delta))>1)throw script::Error("Native extra-grid input is out of range");
    if(phase_==ExtraGridPhase::idle)throw script::Error("Native extra-grid is not prepared");if(phase_==ExtraGridPhase::closed || command_)return;
    if(input.cancel){if(phase_==ExtraGridPhase::full || phase_==ExtraGridPhase::full_loading){cards();if(sound_)sound_(audio::SystemCue::cancel);}else activate(50);return;}
    if(phase_==ExtraGridPhase::loading){poll();return;}
    if(phase_==ExtraGridPhase::full_loading){try{auto image=full_request_->get();if(!image)return;if(!image->width || !image->height)throw script::Error("Native gallery image is empty");full_=std::move(image);variant_=pending_variant_;full_id_=variants_.at(variant_);full_request_.reset();phase_=ExtraGridPhase::full;draw();}catch(...){full_request_.reset();if(full_)phase_=ExtraGridPhase::full;else cards();throw;}return;}
    if(phase_==ExtraGridPhase::full){if(input.page_delta<0){if(variant_)full(variant_-1);}else if(input.page_delta>0 || selected.confirm || (selected.pointer && selected.activate)){if(variant_+1<variants_.size())full(variant_+1);else cards();}return;}
    if(input.page_delta){turn(input.page_delta);return;}const auto previous=focus_;
    if(selected.pointer){focus_.reset();for(const auto& row:data_.rows){const float x=104.f+272*(row.ordinal%4),y=116.f+160*(row.ordinal/4);if(selected.x>=x && selected.x<x+256 && selected.y>=y && selected.y<y+144){focus_=row.ordinal;break;}}
        if(selected.x>=1164 && selected.x<1220 && selected.y>=688 && selected.y<712)focus_=50;
        if(selected.y>=600 && selected.y<664){if(selected.x>=440 && selected.x<488)focus_=51;if(selected.x>=792 && selected.x<840)focus_=52;}
        if(selected.y>=72 && selected.y<104)for(auto page:pages_)if(selected.x>=220+60*page && selected.x<280+60*page)focus_=100+page;}
    if(selected.navigation){std::vector<unsigned> controls;for(const auto& row:data_.rows)if(row.enabled)controls.push_back(row.ordinal);controls.insert(controls.end(),{51,52,50});const auto at=std::find(controls.begin(),controls.end(),focus_.value_or(999));const int size=int(controls.size());const auto index=at==controls.end()?(selected.navigation>0?0:size-1):(int(at-controls.begin())+selected.navigation+size)%size;focus_=controls[unsigned(index)];}
    if(focus_ && focus_!=previous && sound_)sound_(audio::SystemCue::focus);if(focus_ && (selected.confirm || (selected.pointer && selected.activate)))activate(*focus_);if(phase_==ExtraGridPhase::cards)draw();
}
void ExtraGrid::draw(){
    sprites_.clear();if(data_.assets.empty() || phase_==ExtraGridPhase::closed)return;int order{};
    const auto add=[&](std::shared_ptr<const image::Raster> image,float x,float y,float w,float h,float sx,float sy,float sw,float sh,unsigned alpha=255){sprites_.push_back({std::move(image),x,y,w,h,sx,sy,sw,sh,255,255,255,std::uint8_t(alpha)});sprites_.back().layer=0x1060;sprites_.back().order=order++;};
    add(black_,0,0,1280,720,0,0,1,1,full_?255:128);
    if(full_){add(full_,0,0,1280,720,0,0,float(full_->width),float(full_->height));return;}
    const auto& assets=data_.assets;add(assets[0].pixels,0,0,1280,36,0,0,1280,36);add(assets[1].pixels,0,664,1280,56,0,0,1280,56);
    for(auto page:pages_)add(assets[6].pixels,220.f+60*page,72,60,32,float(page*60),page==page_?32:focus_==100+page?64:0,60,32);
    unsigned thumb=chrome;for(const auto& row:data_.rows){const float x=96.f+272*(row.ordinal%4),y=112.f+160*(row.ordinal/4);const auto& card=assets[row.enabled?2:9].pixels;add(card,x,y,272,160,0,0,272,160);
        if(row.enabled){const auto& image=assets.at(thumb++).pixels;add(image,x+8,y+4,256,144,0,0,float(image->width),float(image->height));}
        if(focus_==row.ordinal && row.enabled)add(assets[3].pixels,x,y,272,160,0,0,272,160);}
    add(assets[4].pixels,440,600,48,64,focus_==51?96:0,0,48,64);add(assets[4].pixels,792,600,48,64,focus_==52?96:0,64,48,64);
    add(assets[7].pixels,496,608,288,40,0,0,288,40);add(assets[5].pixels,1164,688,56,24,0,focus_==50?24:0,56,24);
}
}
