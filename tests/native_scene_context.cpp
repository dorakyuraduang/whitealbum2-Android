#include "native_scene_fixture.hpp"

int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{};
    const auto run=[&](bool call,bool child_message,bool pause,bool skip){
        auto banks=memory_banks();Script parent,child;
        parent.call(133,{s::Value(std::string()),s::Value(7),s::Value(1)});
        parent.call(call?1:0,{s::Value(std::string("1002")),s::Value(0)});
        if(call)parent.call(194,{s::Value(1000000),s::Value(0),s::Value(1)});parent.finish();
        if(child_message)child.call(133,{s::Value(std::string()),s::Value(19),s::Value(1)});
        child.call(194,{s::Value(call?2:1000000),s::Value(0),s::Value(0)});child.finish();
        banks["ck-gal.pak"]=bank({{"Global.vrb",text("3,FLG_00,0\r\n")},{"LFScriptFunc.fnc",functions(0,200)},{"LFScriptFuncEx.fnc",functions(200,239)},{"1001.bnr",parent.code},{"1001.txt",text(parent.strings)},{"1002.bnr",child.code},{"1002.txt",text(child.strings)}});
        auto storage=std::make_shared<Storage>();auto progress=std::make_shared<p::Store>(storage);if(skip)progress->state().mark_read(0,8);
        g::Scene scene([&](auto name)->std::shared_ptr<r::Source>{const auto found=banks.find(std::string(name));return found==banks.end()?nullptr:found->second;},1,progress);scene.start();std::int64_t now=16667;CHECK(scene.update(now)==s::RunState::yielded && scene.statistics().scene=="1002");
        const auto reading=[&]{s::SceneReadHistory current(progress);current.restore(g::SavedScene::decode(scene.checkpoint()).part(g::ScenePart::reading));return current;};
        CHECK(reading().script_index()==(child_message?1u:0u) && reading().last_line()==(child_message?19:7));++cases;
        if(pause){const auto frozen=scene.checkpoint();scene.pause(true,now);now+=10000000;CHECK(scene.update(now)==s::RunState::yielded && scene.checkpoint()==frozen);CHECK(reading().script_index()==0 && !progress->state().read(0,8));scene.pause(false,now);++cases;}
        g::Input input;input.skip=skip;now+=16667;CHECK(scene.update(now,input)==s::RunState::yielded);CHECK(reading().script_index()==1 && reading().last_line()==(child_message?19:0) && progress->state().read(0,8));CHECK(!progress->state().read(1,1));++cases;
        if(call){unsigned remaining=5;while(scene.statistics().scene!="1001"){CHECK(remaining--);now+=16667;CHECK(scene.update(now)==s::RunState::yielded);}CHECK(reading().script_index()==1);now+=16667;CHECK(scene.update(now)==s::RunState::yielded);CHECK(reading().script_index()==0 && reading().last_line()==0);CHECK(progress->state().read(1,20)==child_message);++cases;}
        else{CHECK(scene.statistics().command==194 && scene.statistics().scene=="1002");const auto state=reading().checkpoint();for(unsigned n=0;n<3;++n){now+=16667;CHECK(scene.update(now,input)==s::RunState::yielded && reading().checkpoint()==state);}++cases;}
        scene.flush_progress();CHECK(storage->bytes);auto persisted=p::State::decode(*storage->bytes);CHECK(persisted.read(0,8));if(child_message)CHECK(persisted.read(1,19));++cases;
    };
    run(false,false,false,false);run(false,false,true,false);run(false,false,false,true);run(true,false,false,false);run(true,true,false,false);
    auto progress=std::make_shared<p::Store>(std::make_shared<Storage>());s::SceneReadHistory reading(progress);reading.enter("1001");reading.seen("1001",7);const auto stable=reading.checkpoint();reading.enter("1001");CHECK(reading.checkpoint()==stable && !progress->state().read(0,8));++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-scene-context.v1\",\"cases\":"<<cases<<",\"replacement_before_first_message\":true,\"unread_prelude_not_skipped\":true,\"child_return_selects_parent\":true,\"child_last_marker_persisted\":true,\"paused_context_is_frozen\":true,\"same_script_keeps_position\":true,\"synthetic_resources_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native chapter/call reading context and unread prelude policy.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
