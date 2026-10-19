#include "process.hpp"
#include "movie_settings.hpp"
#include "work_samples.hpp"
#include "geometry_cache.hpp"
#include "native_sprites.hpp"
#include "native_glyphs.hpp"
#include "image_colours.hpp"
#include "scalar_math.hpp"
#include "../port/native_resources.hpp"
#include "../port/native_vorbis.hpp"
#include "../port/wa2_win32.hpp"
#include "../platform/compat/scheduler.hpp"
#include "../platform/compat/system.hpp"
#include "../platform/compat/text.hpp"
#include "../platform/compat/locale.hpp"
#include "../platform/render/surface_host.hpp"
#include "../platform/compat/media.hpp"
#include "../platform/compat/acm.hpp"
#include "../platform/compat/shell.hpp"
#include <winrecomp/win32_state.hpp>
#include <sstream>
namespace wr {void install_wa2_exceptions(Process&);}
namespace wa2::aot {
Runtime::Runtime(wr::ProcessOptions options,wr::ProcessBackends backends,std::shared_ptr<graphics::SurfaceHost> graphics):
    process_(wr::FunctionExecutor{[this,samples=std::make_shared<FunctionWorkSamples>(functions())](Context& context,wr::Memory& memory){
        if(!program_){program_=std::make_unique<Program>(memory);process_.state().reports["static_cpu_work"]=[samples]{return samples->report();};}
        FunctionWorkScope sampled_work{samples.get()};
        LeafServiceScope services{[](wr::Cpu& cpu,wr::Memory& ram,void* owner){
            auto& process=*static_cast<wr::Process*>(owner);
            return &cpu==&process.cpu && &ram==&process.memory && process.dispatch_leaf_api();
        },&process_};
        return program_->invoke_unmetered(context);
    }},std::move(options),std::move(backends)){
    wr::install_wa2_native_resources(process_,true);
    wr::install_wa2_native_vorbis(process_,true);
    wr::install_wa2_registry(process_);
    wa2::platform::compat::install_scheduler(process_);
    wa2::platform::compat::install_system(process_,prepare_movie_profile);
    wa2::platform::compat::install_text(process_);
    wa2::platform::compat::install_locale(process_);
    wr::install_wa2_exceptions(process_);
    wa2::platform::compat::install_acm(process_);
    wa2::platform::compat::install_shell(process_,wa2::platform::compat::native_shell_services(process_.options.data_root));
    auto sound=wr::install_wa2_audio_service(process_);
    if(graphics){auto renderer=wa2::graphics::install(process_,std::move(graphics));
        wa2::platform::compat::install_media(process_,std::move(sound),std::move(renderer));}
    // Exit/error diagnostics only: these are the input calculator's existing
    // cached data contracts, shared by every host. No frame pixels are read.
    process_.state().reports["source_pointer"]=[this]{try{
        auto& m=process_.memory;m.check(0xb601a0,0xf060,wr::Memory::Read);
        const auto signed16=[&](wr::U32 address){return std::int32_t(wr::sign_extend(m.load(address,16),16));};
        const auto bank=std::int32_t(wr::sign_extend(m.load(0xb601dc,8),8));
        std::ostringstream out;out<<"{\"schema\":\"wa2.source-pointer.v1\",\"available\":true,\"game_size\":["<<signed16(0xb601e0)<<','<<signed16(0xb601e2)<<"],\"client_size\":["<<signed16(0xb601e4)<<','<<signed16(0xb601e6)<<"],\"game_xy\":["<<std::int32_t(m.load(0xb601a0,32))<<','<<std::int32_t(m.load(0xb601a4,32))<<"],\"selected_rectangle\":"<<signed16(0xb601d4)<<",\"buttons\":["<<m.load(0xb601aa,8)<<','<<m.load(0xb601ac,8)<<','<<m.load(0xb601ab,8)<<"],\"rectangle_bank\":"<<bank<<",\"rectangle_columns\":[\"index\",\"x\",\"y\",\"width\",\"height\"],\"active_rectangles\":[";
        bool comma=false;if(bank>=0 && bank<48)for(unsigned n=0;n<128;++n){const auto at=0xb60200+wr::U32(bank)*1280+n*10;if(!m.load(at,8))continue;if(comma)out<<',';comma=true;out<<'['<<n<<','<<signed16(at+2)<<','<<signed16(at+4)<<','<<signed16(at+6)<<','<<signed16(at+8)<<']';}
        out<<"],\"screen_access\":false}";return out.str();
    }catch(const std::exception& error){return "{\"available\":false,\"error\":"+wr::quote(error.what())+",\"screen_access\":false}";}};
    process_.state().reports["native_trigonometry_cache"]=[]{const auto stats=math::trigonometry_cache_stats();
        return "{\"hits\":"+std::to_string(stats.hits)+",\"misses\":"+std::to_string(stats.misses)+",\"entries_per_function\":64}";};
    process_.state().reports["native_glyph_geometry_cache"]=[]{const auto s=geometry_cache_counts();
        return "{\"grid_hits\":"+std::to_string(s.grid_hits)+",\"grid_misses\":"+std::to_string(s.grid_misses)+
            ",\"corner_hits\":"+std::to_string(s.sprite_hits)+",\"corner_misses\":"+std::to_string(s.sprite_misses)+
            ",\"grid_captures\":"+std::to_string(s.grid_captures)+",\"corner_captures\":"+std::to_string(s.sprite_captures)+
            ",\"maximum_grid_entries\":256,\"maximum_corner_entries\":128,\"pixels_cached\":false}";};
    process_.state().reports["native_sprite_commands"]=[]{const auto s=native_sprite_counts();
        return "{\"accepted\":"+std::to_string(s.accepted)+",\"declined\":"+std::to_string(s.declined)+",\"tiles\":"+std::to_string(s.tiles)+
            ",\"offscreen\":"+std::to_string(s.offscreen)+",\"capability_filter\":"+std::to_string(s.capability_filter)+
            ",\"fp_declined\":"+std::to_string(s.fp_declined)+",\"adapter_declined\":"+std::to_string(s.adapter_declined)+",\"retained_command_cache\":false}";};
    process_.state().reports["native_glyph_commands"]=[]{const auto s=native_glyph_counts();
        return "{\"accepted\":"+std::to_string(s.accepted)+",\"declined\":"+std::to_string(s.declined)+",\"tiles\":"+std::to_string(s.tiles)+",\"retained_command_cache\":false}";};
    process_.state().reports["native_image_colours"]=[]{const auto s=image_colour_counts();
        return "{\"attempts\":"+std::to_string(s.attempts)+",\"completed\":"+std::to_string(s.completed)+",\"pixels\":"+std::to_string(s.pixels)+",\"retained_tables\":false}";};
    process_.state().reports["heap_pool_cache"]=[this]{const auto& state=process_.state();
        return "{\"idle_pools\":"+std::to_string(state.idle_heap_pools)+",\"peak_idle_pools\":"+std::to_string(state.peak_idle_heap_pools)+
            ",\"idle_bytes\":"+std::to_string(state.idle_heap_pools*65536ull)+",\"maximum_idle_bytes\":1048576,\"reuses\":"+
            std::to_string(state.heap_pool_reuses)+",\"retentions\":"+std::to_string(state.heap_pool_retained)+",\"evictions\":"+std::to_string(state.heap_pool_evictions)+"}";};
}
}
