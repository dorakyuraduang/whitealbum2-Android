#include "../platform/compat/media.hpp"
#include "../platform/compat/media_abi.hpp"
#include "../platform/compat/scheduler.hpp"
#include "../platform/compat/system.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../platform/render/movie_layout.hpp"
#include "../platform/render/frame_layout.hpp"
#include "../platform/window/pointer_coordinates.hpp"
#include "../port/guest_api.hpp"
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;namespace c=wa2::platform::compat;namespace movie=wa2::movie;
namespace graphics=wa2::graphics;
class MemorySurface final:public graphics::SurfaceHost {
public:
    static constexpr U32 owner=0x1234;
    std::array<int,2> size{1280,720};
    SDL_Window* acquire(U32,U32,U32,bool)override{throw std::runtime_error("movie layout fixture cannot create a graphics device");}
    bool embedded_gl()const noexcept override{return false;}
    void source_cursor(std::int32_t,std::int32_t)override{}int cursor_visible(bool)override{return 0;}
    std::array<int,2> client_size(U32 window)const override{CHECK(window==owner);return size;}
    void frame_submitted()override{throw std::runtime_error("movie layout fixture cannot submit a frame");}
};
unsigned layout_checks(){
    unsigned checked{};MemorySurface host;movie::VideoFrame frame;frame.width=1920;frame.height=1080;
    graphics::MovieView view;view.owner=host.owner;view.source={0,0,1920,1080};view.destination={0,0,1280,720};
    const auto check=[&](std::array<int,2> client,std::array<int,2> drawable,std::array<double,4> destination,
                         std::array<float,4> texture=std::array<float,4>{0,0,1,1}){
        host.size=client;const auto layout=graphics::layout_movie(frame,view,host.client_viewport(host.owner,drawable[0],drawable[1]));CHECK(layout);
        for(unsigned n=0;n<4;++n){CHECK(std::abs(layout->destination[n]-destination[n])<.000001);CHECK(std::abs(layout->texture[n]-texture[n])<.000001);}
        ++checked;
    };
    // The game renders at 1920x1080 but requests the movie in a 1280x720
    // source client. The old backbuffer transform made this only 853x480.
    check({1280,720},{1280,720},{0,0,1280,720});
    check({1280,720},{1920,1080},{0,0,1920,1080});
    check({1280,720},{2560,1440},{0,0,2560,1440});
    check({1280,720},{2560,1080},{320,0,1920,1080});
    check({1280,720},{720,1280},{0,437,720,405});
    check({1280,720},{1024,768},{0,96,1024,576});
    view.destination={0,0,1024,768};check({1024,768},{1024,768},{0,96,1024,576});
    view.destination={0,0,1920,1080};check({1920,1080},{1280,720},{0,0,1280,720});
    view.destination={160,90,960,540};check({1280,720},{1280,720},{160,90,960,540});
    view.destination={-160,-90,1280,720};check({1280,720},{1280,720},{-160,-90,1280,720});
    view.destination={0,0,1280,720};view.source={480,270,960,540};
    check({1280,720},{1280,720},{0,0,1280,720},{.25f,.25f,.75f,.75f});
    view.source={320,0,1280,960};check({1280,720},{1280,720},{160,0,960,720},{1.f/6,0,5.f/6,8.f/9});
    frame.width=640;frame.height=480;view.source={0,0,640,480};check({1280,720},{1280,720},{160,0,960,720});
    frame.width=720;frame.height=576;frame.aspect_numerator=64;frame.aspect_denominator=45;view.source={0,0,720,576};
    check({1280,720},{1280,720},{0,0,1280,720});
    frame.aspect_numerator=16;frame.aspect_denominator=15;check({1280,720},{1280,720},{160,0,960,720});
    const auto viewport=host.client_viewport(host.owner,1280,720);
    const auto reject=[&](const movie::VideoFrame& f,const graphics::MovieView& v,const graphics::ClientViewport& vp){CHECK(!graphics::layout_movie(f,v,vp));++checked;};
    reject(frame,view,{{0,720},{0,0,1280,720}});reject(frame,view,{{1280,720},{0,0,0,720}});
    for(unsigned n=0;n<9;++n){auto f=frame;auto v=view;
        switch(n){case 0:f.width=0;break;case 1:f.height=0;break;case 2:f.aspect_numerator=0;break;case 3:f.aspect_denominator=0;break;
            case 4:v.source[0]=-1;break;case 5:v.source[2]=0;break;case 6:v.source[0]=0x7fffffff;break;case 7:v.source[3]=577;break;case 8:v.destination[2]=0;break;}
        reject(f,v,viewport);
    }
    return checked;
}
unsigned frame_layout_checks(){
    unsigned checked{};MemorySurface host;
    const auto check=[&](std::array<int,2> drawable,std::array<unsigned,2> buffer,graphics::FrameRect source,
                         std::optional<graphics::FrameRect> destination,graphics::FrameRect expected){
        const auto viewport=host.client_viewport(host.owner,drawable[0],drawable[1]);
        const auto layout=graphics::layout_frame(buffer[0],buffer[1],source,destination,viewport);
        CHECK(layout && layout->source==source && layout->destination==expected);++checked;
    };
    // The game can copy 1280x720 into a phone-sized backbuffer, then Present
    // that subrectangle. The default destination is the 1280x720 CLIENT area.
    for(const auto buffer:{std::array<unsigned,2>{2400,1080},std::array<unsigned,2>{1920,1080},std::array<unsigned,2>{1280,720}})
        check({2400,1080},buffer,{0,0,1280,720},{},{240,0,2160,1080});
    check({2340,1080},{1920,1080},{0,0,1920,1080},{},{210,0,2130,1080});
    check({1920,1200},{1920,1080},{0,0,1920,1080},{},{0,60,1920,1140});
    check({2560,1440},{640,360},{0,0,640,360},{},{0,0,2560,1440});
    check({720,1280},{1280,720},{0,0,1280,720},{},{0,437,720,842});
    check({2400,1080},{1920,1080},{0,0,1920,1080},graphics::FrameRect{320,180,960,540},{720,270,1680,810});
    host.size={2400,1080};check({2400,1080},{1280,720},{0,0,1280,720},graphics::FrameRect{240,0,2160,1080},{240,0,2160,1080});
    host.size={1280,720};
    for(const auto size:{std::array<int,2>{2400,1080},std::array<int,2>{2340,1080},std::array<int,2>{1920,1200}}){
        const auto viewport=host.client_viewport(host.owner,size[0],size[1]);
        const auto map=wa2::window::PointerCoordinates::from_client(size[0],size[1],size[0],size[1],viewport);
        const auto at=map.window(640,250);CHECK(at && (map.source((*at)[0],(*at)[1])==std::array<std::int32_t,2>{640,250}));++checked;
        if(viewport.drawable[0]>0)CHECK(map.source(viewport.drawable[0]-1,size[1]/2)->at(0)<0);
        else CHECK(map.source(size[0]/2,viewport.drawable[1]-1)->at(1)<0);++checked;
    }
    CHECK((host.backbuffer_size(host.owner,0,0,true)==std::array<std::uint32_t,2>{1280,720}));++checked;
    CHECK((host.backbuffer_size(host.owner,1920,0,true)==std::array<std::uint32_t,2>{1920,720}));++checked;
    CHECK((host.backbuffer_size(host.owner,0,1080,true)==std::array<std::uint32_t,2>{1280,1080}));++checked;
    CHECK(!host.backbuffer_size(host.owner,0,0,false));++checked;
    CHECK((host.backbuffer_size(host.owner,1920,1080,false)==std::array<std::uint32_t,2>{1920,1080}));++checked;
    CHECK(!graphics::layout_frame(1280,720,{0,0,1281,720},{},host.client_viewport(host.owner,2400,1080)));++checked;
    CHECK(!graphics::layout_frame(1920,1080,{0,0,1920,1080},graphics::FrameRect{0,0,1920,1080},host.client_viewport(host.owner,2400,1080)));++checked;
    host.size={0,0};CHECK(!host.backbuffer_size(host.owner,0,0,true));++checked;
    return checked;
}
struct Counters {unsigned opened{},closed{},created{},stopped{},frames{},restored{},source_opens{};float gain{};std::array<double,4> movie_destination{};};
class MemoryOutput final:public wa2::platform::AudioOutput {
    std::shared_ptr<Counters> counts;bool active{};
public:explicit MemoryOutput(std::shared_ptr<Counters> c):counts(std::move(c)){}
    bool open(const std::string&)override{if(!active){++counts->opened;active=true;}return true;}void close()override{if(active){active=false;++counts->closed;}}
    void pause(bool)override{}std::vector<std::string> devices()override{return {};}
    wa2::platform::AudioStatus status()const override{return {48000,512,0,0,0,"memory",""};}
};
class MemorySource final:public wa2::resource::Source {
public:std::uint64_t size()const override{return 16;}void read_at(std::uint64_t,std::span<std::uint8_t>)override{throw std::runtime_error("movie bridge fixture must not invoke a real decoder");}
};
class Playback final:public movie::Playback {
    std::shared_ptr<Counters> counts;unsigned updates{};bool ended{},paused{};std::optional<movie::VideoFrame> current;
public:explicit Playback(std::shared_ptr<Counters> value):counts(std::move(value)){++counts->created;}
    ~Playback()override{stop();}bool finished()const override{return ended;}
    void stop()override{if(!ended){++counts->stopped;ended=true;}current.reset();}
    void update(std::int64_t)override{if(ended || paused)return;movie::VideoFrame frame;frame.owner=std::make_shared<unsigned>(++updates);frame.width=640;frame.height=480;frame.time_us=updates*3000;frame.duration_us=3000;current=std::move(frame);if(updates==4)ended=true;}
    void pause(bool value,std::int64_t)override{paused=value;}void gain(float value)override{counts->gain=value;}
    std::optional<movie::VideoFrame> frame()const override{return current;}
};
class Presenter final:public wa2::graphics::MoviePresenter {
    std::shared_ptr<Counters> counts;
public:explicit Presenter(std::shared_ptr<Counters> value):counts(std::move(value)){}
    void movie_frame(const movie::VideoFrame& frame,const wa2::graphics::MovieView& view)override{
        CHECK(frame.width==640 && view.source[2]==640 && view.owner==MemorySurface::owner);
        MemorySurface host;const auto layout=graphics::layout_movie(frame,view,host.client_viewport(view.owner,1280,720));CHECK(layout);
        counts->movie_destination=layout->destination;++counts->frames;
    }
    void restore_game()override{++counts->restored;}
};
Image fixture(){Image image;image.base=0x400000;image.size=65536;image.headers_size=4096;image.entry=0x401000;image.bytes.resize(8192);image.sections={{".text",4096,4096,4096,4096,0x60000020}};return image;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned checked{},mounted_paths{},missing_cases{},open_error_cases{};const auto layouts=layout_checks(),frame_layouts=frame_layout_checks();
    const auto source_file=std::filesystem::path(argv[1]).parent_path()/("movie-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
    {std::ofstream file(source_file,std::ios::binary);file.write("MOV0abcdefghijkl",16);CHECK(file.good());}
    for(bool linear:{false,true})for(bool mounted:{false,true}){auto counts=std::make_shared<Counters>();{
        ProcessOptions options;options.prefer_linear_memory=linear;Process p(FunctionExecutor{[](Cpu&,Memory&){return false;}},options);c::install_scheduler(p);c::install_system(p);
        if(mounted)c::install_resource_mount(p,std::make_shared<c::ResourceMount>(
            std::vector<ReadOnlyFileInfo>{{"MV000.PAK",16,0},{"IC/MV010.PAK",16,0}},
            [source_file,counts](std::string_view){++counts->source_opens;return std::fopen(source_file.string().c_str(),"rb");}));
        auto audio=install_wa2_audio_service(p,[counts](wa2::audio::Mixer&){return std::make_unique<MemoryOutput>(counts);});
        auto presenter=std::make_shared<Presenter>(counts);std::int64_t clock{};c::MovieServices services;
        services.monotonic_us=[&]{return clock;};if(!mounted)services.open=[](Process& p,std::u16string_view name){
            if(name==u"open-denied.pak")throw wa2::resource::Error("fixture provider permission denied");
            if(name==u"unsupported.pak")throw GuestFault(FaultKind::unsupported,p.cpu.eip,"fixture unsupported movie service");
            CHECK(name==u"movie0.pak");return std::make_shared<MemorySource>();};
        services.inspect=[mounted](const auto& source){if(mounted){std::array<std::uint8_t,4> header{};source->read_at(0,header);CHECK((header==std::array<std::uint8_t,4>{'M','O','V','0'}));}return movie::Information{640,480,48000,2,12000};};services.player=[counts](auto,auto){return std::make_unique<Playback>(counts);};
        c::install_media(p,std::move(audio),presenter,std::move(services));p.load(fixture());auto& m=p.memory;const auto work=p.allocate_bytes(4096);
        wr::port::write(p,work,c::media_abi::filter_class);wr::port::write(p,work+32,c::media_abi::graph);
        CHECK(p.callback(p.resolve("ole32.dll","CoCreateInstance"),std::array<U32,5>{work,0,1,work+32,work+64})==0);const auto graph=m.load(work+64,32);CHECK(graph);
        const auto method=[&](U32 object,unsigned slot,std::initializer_list<U32> args){std::vector<U32> values{object};values.insert(values.end(),args.begin(),args.end());return p.callback(m.load(m.load(object,32)+slot*4,32),values);};
        const auto query=[&](const auto& iid){wr::port::write(p,work+32,iid);CHECK(method(graph,0,{work+32,work+64})==0);return m.load(work+64,32);};
        const auto control=query(c::media_abi::control),events=query(c::media_abi::event_ex),volume=query(c::media_abi::audio),video=query(c::media_abi::basic_video),window=query(c::media_abi::video_window);
        const auto movie_report=[&]{return p.state().reports.at("directshow")();};
        if(mounted){
            for(const auto* name:{u"mv000.pak",u".\\MV000.PAK",u"C:\\WINRECOMP\\mv000.pak",u"IC\\mv010.pak",u"C:\\WinRecomp\\IC\\MV010.PAK"}){
                CHECK(method(graph,13,{p.put_wstring(name),0})==0);++mounted_paths;
            }
            const auto set_directory=p.resolve("kernel32.dll","SetCurrentDirectoryA");CHECK(set_directory);
            CHECK(p.callback(set_directory,std::array<U32,1>{p.put_string("IC")})==1);
            CHECK(method(graph,13,{p.put_wstring(u"mv010.pak"),0})==0);++mounted_paths;
            CHECK(method(graph,13,{p.put_wstring(u"missing.pak"),0})==0x80040216u);
            const auto text=movie_report();CHECK(text.find("IC/missing.pak")!=text.npos && text.find("MV000.PAK")!=text.npos);
            CHECK(method(control,7,{})==0x80040227u && counts->created==0 && counts->opened==0);++mounted_paths;++missing_cases;
            CHECK(p.callback(set_directory,std::array<U32,1>{p.put_string("C:\\WinRecomp")})==1);
            for(bool automation:{false,true}){
                CHECK(method(graph,13,{p.put_wstring(u"mv000.pak"),0})==0);const auto before=counts->source_opens;
                const auto missing=p.put_wstring(u"mv001.pak");
                CHECK((automation?method(control,11,{missing}):method(graph,13,{missing,0}))==0x80040216u);
                CHECK(counts->source_opens==before && counts->created==0 && counts->opened==0);
                CHECK(method(video,34,{work+128,work+132})==0 && m.load(work+128,32)==0 && m.load(work+132,32)==0);
                CHECK(method(control,7,{})==0x80040227u);CHECK(movie_report().find("mv001.pak")!=std::string::npos);++missing_cases;
            }
        }else{
            CHECK(method(graph,13,{p.put_wstring(u"open-denied.pak"),0})==0x80004005u);
            CHECK(movie_report().find("permission denied")!=std::string::npos);CHECK(method(control,7,{})==0x80040227u);++open_error_cases;
            bool unsupported=false;try{method(control,11,{p.put_wstring(u"unsupported.pak")});}catch(const GuestFault& error){unsupported=error.kind==FaultKind::unsupported;}
            CHECK(unsupported && counts->created==0 && counts->opened==0);
        }
        CHECK(method(graph,13,{0,0})==0x80004003u);CHECK(method(control,11,{p.put_wstring(u"")})==0x80070057u);
        const auto filename=p.put_wstring(mounted?u"C:\\WinRecomp\\MV000.PAK":u"movie0.pak");CHECK(method(graph,13,{filename,0})==0 && counts->opened==0);
        CHECK(movie_report().find("\"last_render_hresult\":0")!=std::string::npos);
        CHECK(method(video,34,{work+128,work+132})==0 && m.load(work+128,32)==640 && m.load(work+132,32)==480);
        CHECK(method(window,29,{MemorySurface::owner})==0);CHECK(method(window,39,{0,0,1280,720})==0);
        CHECK(method(window,40,{work+144,work+148,work+152,work+156})==0 && m.load(work+152,32)==1280 && m.load(work+156,32)==720);
        CHECK(method(video,31,{320,180,640,360})==0);CHECK(method(video,32,{work+144,work+148,work+152,work+156})==0);
        CHECK(m.load(work+144,32)==320 && m.load(work+148,32)==180 && m.load(work+152,32)==640 && m.load(work+156,32)==360);
        CHECK(method(window,39,{0,0,1280,720})==0);
        CHECK(method(volume,7,{U32(-2000)})==0);CHECK(method(events,7,{work+128})==0);const auto event=m.load(work+128,32);CHECK(event);
        CHECK(method(control,7,{})==0 && counts->opened==1 && std::abs(counts->gain-.1f)<.000001f);
        const auto poll=[&]{clock+=3000;for(const auto& fn:p.state().poll)fn();};
        poll();CHECK(counts->frames==1);CHECK((counts->movie_destination==std::array<double,4>{160,0,960,720}));
        CHECK(method(control,8,{})==0);poll();CHECK(counts->frames==1);CHECK(method(control,7,{})==0);
        poll();poll();poll();CHECK(counts->frames==4);
        CHECK(p.callback(p.resolve("kernel32.dll","WaitForSingleObject"),std::array<U32,2>{event,0})==0);
        CHECK(method(events,8,{work+128,work+132,work+136,0})==0 && m.load(work+128,32)==1);
        CHECK(method(events,12,{1,0,0})==0);m.store(work+128,1,32);CHECK(method(events,8,{work+128,work+132,work+136,0})==0x80004004 && m.load(work+128,32)==0);
        CHECK(method(control,9,{})==0 && counts->restored==1);
        if(mounted){
            // Clear a completed source and event on failure, then recover.
            CHECK(method(graph,13,{p.put_wstring(u"mv001.pak"),0})==0x80040216u);
            CHECK(method(control,7,{})==0x80040227u && counts->created==1);
            CHECK(p.callback(p.resolve("kernel32.dll","WaitForSingleObject"),std::array<U32,2>{event,0})==258u);
            CHECK(method(control,11,{filename})==0);CHECK(movie_report().find("mv001.pak")!=std::string::npos);++missing_cases;
        }
        for(auto object:{window,video,volume,events,control,graph})CHECK(method(object,2,{})==0);
        CHECK(!p.state().events.contains(event) && p.executed_instructions==0);++checked;
    }CHECK(counts->closed==1);}
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.shared-movie-bridge.v2\",\"memory_backend_source_combinations\":"<<checked<<",\"memory_backends\":2,\"default_mounted_movie_path_cases\":"<<mounted_paths<<",\"missing_movie_hresult_cases\":"<<missing_cases<<",\"provider_io_hresult_cases\":"<<open_error_cases<<",\"failed_render_does_not_replay_stale_source\":true,\"unsupported_service_still_rejected\":true,\"recovery_after_missing_mv001\":true,\"frame_layout_cases\":"<<frame_layouts<<",\"present_destination_uses_client\":true,\"movie_layout_cases\":"<<layouts<<",\"client_backbuffer_resolution_mismatch\":true,\"dpi_aspect_crop_and_sample_aspect_ratio\":true,\"source_com_layout_and_identity\":true,\"pause_resume_and_volume\":true,\"completion_event_and_empty_queue\":true,\"decoder_factory_released\":true,\"audio_hardware_opened\":false,\"graphics_device_created\":false,\"real_codec_playback_checked\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS shared movie transport and "<<layouts<<" production layout cases: client resolution, DPI, aspect, crop and COM playback in synthetic memory.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
