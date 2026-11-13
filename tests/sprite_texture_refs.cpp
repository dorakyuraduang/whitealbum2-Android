// Own-memory resource ownership only. No renderer/device, game or frame loop.
#include "../platform/render/sprite_texture_refs.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <type_traits>
#include <vector>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("sprite texture check: ")+#x);}while(false)
struct Tracked {
    inline static unsigned live{},created{};
    unsigned value{};
    Tracked(){++live;++created;}
    Tracked(Tracked&& other)noexcept:value(other.value){++live;++created;}
    ~Tracked(){--live;}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);using wa2::graphics::SpriteTextureRefs;
    static_assert(!std::is_copy_constructible_v<SpriteTextureRefs<int>> && !std::is_move_constructible_v<SpriteTextureRefs<int>>);
    unsigned cases{},ownership{},failures{};
    for(unsigned count:{0u,1u,2u,4u,5u,8u,255u,256u}){
        Tracked::created=0;
        {SpriteTextureRefs<Tracked> refs(count);CHECK(refs.size()==count);
            CHECK(Tracked::created==(count<=4?4:260));
            for(unsigned n=0;n<count;++n)refs[n].value=n*17+3;
            for(unsigned n=0;n<count;++n)CHECK(refs[n].value==n*17+3);
        }CHECK(!Tracked::live);++cases;
        std::vector<std::weak_ptr<unsigned>> weak;
        {SpriteTextureRefs<std::shared_ptr<unsigned>> refs(count);
            for(unsigned n=0;n<count;++n){auto resource=std::make_shared<unsigned>(n);weak.push_back(resource);refs[n]=resource;}
            for(unsigned n=0;n<count;++n){CHECK(!weak[n].expired() && *refs[n]==n);++ownership;}
        }
        for(const auto& resource:weak)CHECK(resource.expired());
    }
    try{SpriteTextureRefs<Tracked> invalid(257);CHECK(false);}catch(const std::length_error&){CHECK(!Tracked::live);++failures;}
    // A failed preflight must release already-resolved references before any
    // presentation side effect, just as the previous fixed array did.
    for(unsigned count:{4u,8u,256u}){
        std::vector<std::weak_ptr<unsigned>> weak;
        bool rejected{};
        try{SpriteTextureRefs<std::shared_ptr<unsigned>> refs(count);
            for(unsigned n=0;n<count;++n){if(n==count/2)throw std::runtime_error("injected lookup rejection");refs[n]=std::make_shared<unsigned>(n);weak.push_back(refs[n]);}
        }catch(const std::runtime_error&){for(const auto& resource:weak)CHECK(resource.expired());rejected=true;}
        CHECK(rejected);++failures;
    }
    std::ofstream out(argv[1]);out<<"{\"cases\":"<<cases<<",\"ownership_checks\":"<<ownership<<",\"failure_cases\":"<<failures
        <<",\"inline_reference_slots\":4,\"maximum_tiles\":256,\"call_local_lifetime\":true,\"no_dynamic_reference_storage\":true,\"copy_move_forbidden\":true"
        <<",\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"passed\":true}\n";
    CHECK(out);std::cout<<"PASS small sprite texture reference lifetime and 256-tile overflow bounds.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
