#include "frame_driver.hpp"
#include "native_scene_fixture.hpp"
#include <limits>

struct Settings:g::PreferenceStorage {std::optional<Bytes> value;std::optional<Bytes> read()override{return value;}void replace(std::span<const std::uint8_t> bytes)override{value=Bytes(bytes.begin(),bytes.end());}};
struct Saves:g::SaveStorage {std::optional<Bytes> read(unsigned)override{return {};}void replace(unsigned,std::span<const std::uint8_t>)override{}std::vector<g::SaveSlotInfo> list()override{return {};}};
struct Fixture {
    std::map<std::string,std::shared_ptr<Source>> banks=memory_banks();std::shared_ptr<p::Store> progress=std::make_shared<p::Store>(std::make_shared<Storage>());
    Fixture(){Script script;script.call(194,{s::Value(1000000000),s::Value(0),s::Value(1)});script.finish();banks["ck-gal.pak"]=bank({{"Global.vrb",text("3,FLG_00,0\r\n")},{"LFScriptFunc.fnc",functions(0,200)},{"LFScriptFuncEx.fnc",functions(200,239)},{"1001.bnr",script.code},{"1001.txt",text(script.strings)}});}
    std::unique_ptr<g::Application> application(){auto app=std::make_unique<g::Application>([this](auto name)->std::shared_ptr<r::Source>{const auto found=banks.find(std::string(name));return found==banks.end()?nullptr:found->second;},progress,std::make_shared<Settings>(),std::make_shared<Saves>(),memory_resolver());auto settings=app->preferences();settings.skip_read_only=false;app->preferences(settings);app->start({},0);return app;}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};g::FrameInput queue;g::Input click;click.advance=true;click.selection={true,true,false,210,320,0};queue.add(click);
    g::Input hover;hover.selection={true,false,false,900,600,0};queue.add(hover);auto received=queue.take();CHECK(received.advance && received.selection.activate && received.selection.x==210 && received.selection.y==320);++cases;
    received=queue.take();CHECK(!received.advance && !received.selection.activate && received.selection.pointer && received.selection.x==900 && received.selection.y==600);++cases;
    g::Input hold;hold.skip=true;queue.add(hold);CHECK(queue.take().skip && queue.take().skip);queue.add({});CHECK(!queue.take().skip);++cases;
    hold.pointer_held=true;queue.add(hold);CHECK(queue.take().pointer_held && queue.take().pointer_held);queue.add({});CHECK(!queue.take().pointer_held);++cases;
    g::Input toggle;toggle.toggle_window=true;queue.add(toggle);queue.add(toggle);CHECK(!queue.take().toggle_window);queue.add(toggle);CHECK(queue.take().toggle_window && !queue.take().toggle_window);++cases;
    g::Input choice;choice.choice=3;queue.add(choice);choice.choice=6;queue.add(choice);CHECK(queue.take().choice==3 && queue.take().choice==-1);++cases;
    g::Input page;page.page_delta=1;queue.add(page);queue.add(page);CHECK(queue.take().page_delta==1 && !queue.take().page_delta && !page.continued().page_delta);page.page_delta=-2;rejects([&]{queue.add(page);});++guards;++cases;
    g::Input wheel;wheel.scroll=1;queue.add(wheel);queue.add(wheel);CHECK(queue.take().scroll==1 && !queue.take().scroll && !wheel.continued().scroll);wheel.scroll=-2;rejects([&]{queue.add(wheel);});++guards;++cases;
    g::Input navigate;navigate.selection.navigation=1;navigate.selection.confirm=true;queue.add(navigate);queue.add(navigate);CHECK(queue.take().selection.navigation==1 && !queue.take().selection.confirm);++cases;
    queue.add(click);queue.clear();received=queue.take();CHECK(!received.advance && !received.selection.activate && !received.selection.pointer);++cases;
    for(auto invalid:{g::Input{.choice=8},g::Input{.choice=-2}}){rejects([&]{queue.add(invalid);});++guards;}
    hover.selection.x=std::numeric_limits<float>::quiet_NaN();rejects([&]{queue.add(hover);});hover.selection.x=0;hover.selection.navigation=2;rejects([&]{queue.add(hover);});guards+=2;++cases;
    for(unsigned rate:{0u,241u}){g::DriverSettings value;value.presentation_hz=rate;rejects([&]{value.validate();});++guards;}
    for(unsigned count:{0u,9u}){g::DriverSettings value;value.maximum_catch_up=count;rejects([&]{value.validate();});++guards;}
    for(auto value:{g::FrameBudget{-1,1},g::FrameBudget{50001,1},g::FrameBudget{0,0},g::FrameBudget{0,4097}}){g::DriverSettings settings;settings.skip=value;rejects([&]{settings.validate();});++guards;}++cases;
    Fixture fixture;
    // A finite simulated second checks cadence, not real elapsed time or FPS.
    // Different presentation rates must all produce the same sixty advances.
    for(unsigned rate:{30u,60u,120u,240u}){
        auto app=fixture.application();g::DriverSettings settings;settings.presentation_hz=rate;g::FrameDriver driver(*app,settings);auto frame=driver.pump(0,{},[]{return 0;});CHECK(frame.updates==1 && frame.draw && frame.sleep_us>0);unsigned draws=1,updates=1;
        for(unsigned n=1;n<=rate;++n){const auto now=(std::int64_t(n)*1000000+rate-1)/rate;frame=driver.pump(now,{},[]{return 0;});updates+=frame.updates;draws+=unsigned(frame.draw);CHECK(!frame.dropped_updates && frame.sleep_us>0);}
        CHECK(updates==61 && app->scene()->statistics().ticks==61 && draws==rate+1);++cases;
    }
    {
        auto app=fixture.application();g::FrameDriver driver(*app);driver.pump(0,{},[]{return 0;});const auto stall=driver.pump(1000000,{},[]{return 0;});CHECK(stall.updates==8 && stall.batch.ticks==8 && stall.dropped_updates==52);++cases;
        const auto ticks=app->scene()->statistics().ticks;const auto same=driver.pump(1000000,{},[]{return 0;});CHECK(!same.updates && !same.draw && app->scene()->statistics().ticks==ticks);++cases;
        rejects([&]{driver.pump(999999,{},[]{return 0;});});rejects([&]{driver.pump(-1,{},[]{return 0;});});rejects([&]{driver.pump(1000000,{},{});});guards+=3;++cases;
        const auto end=driver.pump(std::numeric_limits<std::int64_t>::max(),{},[]{return 0;});CHECK(end.updates==8 && end.dropped_updates>1000000 && end.sleep_us>0);++cases;
    }
    {
        auto app=fixture.application();g::FrameDriver driver(*app);driver.pump(0,{},[]{return 0;});driver.pump(16667,{},[]{return 0;});const auto ticks=app->scene()->statistics().ticks;
        auto frame=driver.pump(20000,{{},true,false},[]{return 0;});CHECK(frame.suspended && !frame.updates && !frame.draw && app->mixer()->paused());++cases;
        frame=driver.pump(600020000,{{},true,false},[]{return 0;});CHECK(frame.suspended && app->scene()->statistics().ticks==ticks);++cases;
        g::Input stale;stale.advance=stale.skip=true;frame=driver.pump(600020001,{stale,false,false},[]{return 0;});CHECK(frame.updates==1 && frame.batch.ticks==1 && !frame.dropped_updates && frame.draw && !app->mixer()->paused());++cases;
        app->open(g::Page::options,600020001);frame=driver.pump(600036668,{},[]{return 0;});CHECK(frame.updates==1 && frame.batch.ticks==0 && app->scene()->statistics().ticks==ticks+1);++cases;
        g::Input back;back.cancel=true;frame=driver.pump(600053335,{back,false,false},[]{return 0;});CHECK(frame.updates==1 && frame.batch.ticks==1 && app->page()==g::Page::story);++cases;
        frame=driver.pump(600053336,{{},false,true},[]{return 0;});CHECK(frame.closed && !frame.draw && !frame.updates && app->page()==g::Page::closed && !app->mixer());++cases;
        frame=driver.pump(600053337,{},[]{throw std::runtime_error("closed driver must not sample its budget clock");return 0;});CHECK(frame.closed && !frame.updates);++cases;
    }
    {
        auto app=fixture.application();g::FrameDriver driver(*app);driver.pump(0,{},[]{return 0;});g::Input skip;skip.skip=true;
        auto frame=driver.pump(150000,{skip,false,false},[]{return 0;});CHECK(frame.updates==8 && frame.batch.ticks==256 && frame.batch.limit==g::FrameLimit::ticks && frame.dropped_updates==1);++cases;
    }
    {
        auto app=fixture.application();g::FrameDriver driver(*app);driver.pump(0,{},[]{return 0;});g::Input skip;skip.skip=true;std::int64_t elapsed{};
        auto frame=driver.pump(150000,{skip,false,false},[&]{elapsed+=400;return elapsed;});CHECK(frame.updates==8 && frame.batch.ticks==8 && frame.batch.limit==g::FrameLimit::time);++cases;
        rejects([&]{driver.pump(166667,{},[]{return -1;});});++guards;++cases;
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-frame-driver.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"logic_hz\":60,\"presentation_rates\":[30,60,120,240],\"bounded_catch_up\":true,\"shared_skip_budget\":true,\"queued_click_keeps_position\":true,\"synthetic_clock_and_scripts_only\":true,\"performance_benchmark\":false,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS shared native cadence, queued input, stall recovery and per-pump skip budget.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
