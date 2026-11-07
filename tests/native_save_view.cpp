#include "save_view.hpp"
#include "menu_digits.hpp"
#include "scene_geometry.hpp"
#include "native_scene_fixture.hpp"
#include "native_save_atlases.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <limits>

namespace a=wa2::audio;namespace t=wa2::text;
g::SaveInput pointer(float x,float y,bool activate=false){return {{true,activate,false,x,y,0},false};}
template<class Predicate,class Step>void until(Predicate predicate,Step step){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);unsigned n{};while(!predicate()){CHECK(++n<2000 && std::chrono::steady_clock::now()<deadline);step();std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},loads{},reads{},max_pending{},max_new_images{};bool missing{},text_failure{};
    std::map<std::string,Bytes> resources;for(const auto& atlas:wa2::test::save_atlases)resources[atlas.name]=raster(atlas.w,atlas.h);i::ImageStore images(r::Archive::open(bank(std::move(resources))));
    std::vector<std::weak_ptr<i::ImageRequest>> image_jobs;const g::SaveView::Loader loader=[&](auto name){++loads;if(missing)return std::shared_ptr<i::ImageRequest>{};unsigned pending{};for(auto& job:image_jobs)if(auto item=job.lock())pending+=!item->get();CHECK(!pending);auto request=images.request(name);image_jobs.push_back(request);max_pending=std::max(max_pending,pending+unsigned(!request->get()));return request;};
    auto thumbnail=std::make_shared<const i::Raster>(i::Raster{128,72,Bytes(128u*72*4,255)});g::SavePreview primary;primary.scene="1001";primary.description="ABC";primary.thumbnail=thumbnail;primary.story_known=true;primary.story_code=122120;
    std::map<unsigned,g::SavePreview> entries{{17,primary},{99,primary},{100,primary},{205,primary}};
    std::vector<g::SaveSlotInfo> inventory{{17,40000,5000},{99,40000,2000},{100,40000,6000},{18,0,1000,"owned invalid file"},{205,40000,9000}};
    std::mutex store_mutex,gate;std::condition_variable wake;bool released{},gate_reads{true};std::atomic<unsigned> active{},maximum_active{};
    const g::SaveView::Preview preview=[&](unsigned slot){++reads;return std::make_shared<g::SavePreviewRequest>([&,slot]()->std::optional<g::SavePreview>{const auto count=++active;maximum_active.store(std::max(maximum_active.load(),count));
        struct Release {std::atomic<unsigned>& value;~Release(){--value;}} release{active};
        {std::unique_lock lock(gate);if(gate_reads)CHECK(wake.wait_for(lock,std::chrono::seconds(5),[&]{return released;}));}
        std::lock_guard lock(store_mutex);const auto found=entries.find(slot);if(found==entries.end())return {};return found->second;});};
    const g::SaveView::Text text=[&](auto raw,const auto& style){if(text_failure)throw std::runtime_error("owned text preparation failure");return t::FontBook::defer(t::layout(raw,style));};
    std::vector<a::SystemCue> cues;g::SaveView view(loader,preview,text,[&](auto cue){cues.push_back(cue);});
    const auto step=[&](g::SaveInput input={}){const auto before=loads;view.update(input);max_new_images=std::max(max_new_images,loads-before);};
    const auto ready=[&]{until([&]{return view.phase()==g::SavePhase::active;},[&]{step();});};
    const auto click=[&](unsigned id){const auto found=std::find_if(view.hits().begin(),view.hits().end(),[&](const auto& hit){return hit.id==id;});CHECK(found!=view.hits().end());step(pointer(found->x+1,found->y+1,true));};
    const auto sprite=[&](int order)->const i::Sprite&{const auto found=std::find_if(view.sprites().begin(),view.sprites().end(),[&](const auto& item){return item.order==order;});CHECK(found!=view.sprites().end());return *found;};
    const auto geometry=[&]{for(unsigned n=0;n<view.sprites().size();++n){const auto& item=view.sprites()[n];CHECK(item.image && i::scene_quad({item.x,item.y,item.width,item.height,item.source_x,item.source_y,item.source_width,item.source_height},item.image->width,item.image->height));if(n)CHECK(view.sprites()[n-1].layer<=item.layer);}};
    rejects([&]{g::SaveView bad({},preview,text);});++guards;rejects([&]{g::SaveView bad(loader,{},text);});++guards;rejects([&]{g::SaveView bad(loader,preview,{});});++guards;rejects([&]{step();});++guards;++cases;
    g::SaveContext context;context.timing={2,2,2};context.result_ticks=3;view.prepare(context,inventory);CHECK(view.phase()==g::SavePhase::loading && view.page()==1 && loads==1 && !reads);++cases;
    until([&]{return reads!=0;},[&]{step();});const auto read_count=reads;for(unsigned n=0;n<10;++n)step();CHECK(view.phase()==g::SavePhase::loading && reads==read_count && view.sprites().empty());{std::lock_guard lock(gate);released=true;}wake.notify_all();ready();CHECK(max_pending<=1 && max_new_images<=2 && maximum_active==1 && view.rows().size()==10 && view.hits().size()==23);geometry();++cases;
    CHECK(view.rows()[7].slot==17 && view.rows()[7].preview->description=="ABC" && !view.rows()[0].occupied && !view.rows()[8].diagnostic.empty());CHECK(sprite(0x4a7).width==304 && sprite(0x4a7).height==168 && sprite(0x4a7).source_y==16);++cases;
    CHECK(sprite(0x4ae).x==8 && sprite(0x4ae).y==112 && sprite(0x4ae).source_x==0 && sprite(0x4ae).alpha==255);CHECK(sprite(0x4b5).x==640 && sprite(0x4b5).y==304 && sprite(0x4b5).source_x==632);CHECK(sprite(0x50f).source_x==80 && sprite(0x50f).source_y==72);++cases;
    std::vector<i::Sprite> digits;auto digit_atlas=images.request("sys_01013.tga");until([&]{return bool(digit_atlas->get());},[]{});g::menu_digits(digits,digit_atlas->get(),"0/ :9",20,30,123,77,88);CHECK(digits.size()==4 && digits[0].source_x==0 && digits[0].source_y==16 && digits[1].source_x==180 && digits[1].source_y==0 && digits[2].source_x==120 && digits[2].source_y==16 && digits[3].x==68 && digits[3].alpha==123);++cases;
    const auto size=digits.size();rejects([&]{g::menu_digits(digits,digit_atlas->get(),"01A",0,0,255,0,0);});++guards;CHECK(digits.size()==size);rejects([&]{g::menu_digits(digits,{},"0",0,0,255,0,0);});++guards;++cases;
    click(0);auto command=view.take_command();CHECK(command && command->kind==g::SaveCommand::Kind::save && command->slot==10 && view.phase()==g::SavePhase::awaiting);step(pointer(700,500,true));CHECK(!view.take_command());++cases;
    view.failed("owned disk failure");CHECK(view.phase()==g::SavePhase::active && view.operation_error()=="owned disk failure" && !view.rows()[0].occupied);click(0);CHECK(view.take_command()->slot==10);{std::lock_guard lock(store_mutex);entries[10]=primary;}view.succeeded(primary);CHECK(view.phase()==g::SavePhase::result && view.rows()[0].occupied && view.operation_error().empty());geometry();ready();++cases;
    click(22);ready();CHECK(view.page()==2);click(21);ready();CHECK(view.page()==1 && view.rows()[0].occupied && view.rows()[0].preview->description=="ABC");++cases;
    click(7);CHECK(view.phase()==g::SavePhase::confirming && !view.take_command());until([&]{return view.confirmation()->phase()==g::ConfirmationPhase::active;},[&]{step();});step(pointer(649,377,true));ready();CHECK(!view.take_command() && view.rows()[7].occupied);++cases;
    click(7);until([&]{return view.confirmation()->phase()==g::ConfirmationPhase::active;},[&]{step();});step(pointer(473,377,true));command=view.take_command();CHECK(command && command->slot==17 && view.phase()==g::SavePhase::awaiting);view.succeeded(primary);CHECK(!view.confirmation());ready();++cases;
    click(19);ready();CHECK(view.page()==9);click(22);ready();CHECK(view.page()==0);g::SaveInput previous;previous.page_delta=-1;step(previous);ready();CHECK(view.page()==9);++cases;
    const auto old_phase=view.phase();const auto old_page=view.page();missing=true;rejects([&]{view.prepare(context,inventory);});++guards;missing=false;text_failure=true;rejects([&]{view.prepare(context,inventory);});++guards;text_failure=false;CHECK(view.phase()==old_phase && view.page()==old_page);++cases;
    for(unsigned n=0;n<8;++n){auto invalid=context;if(n==0)invalid.skin=10;if(n==1)invalid.theme=7;if(n==2)invalid.timing.open=601;if(n==3)invalid.timing.page=601;if(n==4)invalid.timing.close=601;if(n==5)invalid.result_ticks=601;if(n==6)invalid.title=true;if(n==7)invalid.mode=static_cast<g::SaveMode>(3);rejects([&]{view.prepare(invalid,inventory);});++guards;}auto duplicate=inventory;duplicate.push_back(duplicate.front());rejects([&]{view.prepare(context,duplicate);});++guards;++cases;
    context.mode=g::SaveMode::load;context.alternate=true;context.title=true;view.prepare(context,inventory);ready();CHECK(view.hits().size()==25 && sprite(0x4a7).source_y==0 && sprite(0x4a7).height==168);geometry();++cases;
    click(0);CHECK(!view.take_command() && cues.back()==a::SystemCue::reject);click(8);CHECK(!view.take_command() && cues.back()==a::SystemCue::reject);++cases;
    click(7);until([&]{return view.confirmation()->phase()==g::ConfirmationPhase::active;},[&]{step();});step(pointer(473,377,true));command=view.take_command();CHECK(command && command->kind==g::SaveCommand::Kind::load && command->slot==17);view.failed("owned missing image");CHECK(view.phase()==g::SavePhase::active && view.rows()[7].occupied);++cases;
    click(24);ready();CHECK(view.bank()==1 && view.page()==0 && view.rows()[5].slot==205 && view.rows()[5].occupied && sprite(0x4ae).source_y==96);geometry();click(24);ready();CHECK(view.bank()==0 && view.page()==1);++cases;
    click(23);until([&]{return view.confirmation()->phase()==g::ConfirmationPhase::active;},[&]{step();});step(pointer(473,377,true));CHECK(view.take_command()->slot==g::quick_slot);view.failed("owned read failure");++cases;
    context.confirmation=false;view.prepare(context,inventory);ready();click(7);CHECK(!view.confirmation() && view.take_command()->slot==17);rejects([&]{view.succeeded(primary);});++guards;view.failed("retry");++cases;
    const auto stable=sprite(0x4b5).image;auto retained=std::vector<i::Sprite>(view.sprites().begin(),view.sprites().end());click(20);until([&]{return view.phase()==g::SavePhase::closed;},[&]{step();});CHECK(view.take_command()->kind==g::SaveCommand::Kind::close && view.sprites().empty() && retained[0].image && stable);++cases;
    view.prepare(context,inventory);step({{},true});until([&]{return view.phase()==g::SavePhase::closed;},[&]{step();});CHECK(view.take_command()->kind==g::SaveCommand::Kind::close && view.sprites().empty());++cases;
    context.confirmation=true;view.prepare(context,inventory);ready();g::SaveInput invalid;invalid.selection.x=std::numeric_limits<float>::infinity();rejects([&]{step(invalid);});++guards;invalid={};invalid.page_delta=2;rejects([&]{step(invalid);});++guards;invalid={};invalid.selection.navigation=2;rejects([&]{step(invalid);});++guards;++cases;
    {std::lock_guard lock(store_mutex);entries.erase(17);}view.prepare(context,inventory);ready();CHECK(!view.rows()[7].occupied);++cases;
    auto bad_info=inventory;bad_info[0].slot=g::native_save_slots;rejects([&]{view.prepare(context,bad_info);});++guards;rejects([&]{view.succeeded();});++guards;rejects([&]{view.failed("no action");});++guards;++cases;
    view.clear();CHECK(view.phase()==g::SavePhase::idle && view.sprites().empty() && view.hits().empty());++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-save-view.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"manual_slots\":100,\"two_column_source_geometry\":true,\"source_bitmap_digit_grid\":true,\"overwrite_and_load_confirmations\":true,\"maximum_preview_reads\":"<<maximum_active.load()<<",\"maximum_pending_images\":"<<max_pending<<",\"maximum_new_images_per_tick\":"<<max_new_images<<",\"failed_operations_keep_rows\":true,\"deferred_font_geometry_only\":true,\"synthetic_resources_only\":true,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS shared native save/load view, source geometry, bounded previews and confirmations.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
