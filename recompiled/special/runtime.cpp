#include "runtime.hpp"
#include "lzss.hpp"
#include "render.hpp"
#include "weather.hpp"
#include "images.hpp"
#include "optimizations.hpp"
#include "optimization_profile.hpp"
#include "../text_jump_budget.hpp"
#include "../native_weather.hpp"
#include "native_entries.hpp"
#include "performance_profile.hpp"
#include "save_profile.hpp"
#include "../movie_settings.hpp"
#include "../save_page_timing.hpp"
#include "../../port/native_vorbis.hpp"
#include "../../platform/compat/system.hpp"
#include "../../platform/compat/extended_system.hpp"
#include "../../platform/compat/scheduler.hpp"
#include "../../platform/compat/text.hpp"
#include "../../platform/compat/locale.hpp"
#include "../../platform/compat/acm.hpp"
#include "../../platform/compat/shell.hpp"
#include "../../platform/compat/media.hpp"
#include "../../platform/compat/resource_mount.hpp"
#include "../../platform/compat/language_resources.hpp"
#include "../../platform/compat/save_files.hpp"
#include "../../platform/window/gui.hpp"
#include "../../platform/compat/legacy_display.hpp"
#include "../../port/wa2_win32.hpp"
#include "../work_samples.hpp"
#include "../../platform/clock.hpp"
#include <winrecomp/win32_state.hpp>
#include <fstream>
#include <sstream>
namespace wr {void install_wa2_exceptions(Process&);}
namespace wa2::special {
namespace {
std::string_view identity(resource::GameLanguage language){
    (void)resource::language_profile(language,resource::ResourceTitle::special_contents);
    return language==resource::GameLanguage::english?"849fd5e414a0ee2b34cec65aba4981c6101f9a59d57b6299c832ae77080b9736":"1a93d3126a90c68d07c0f79ec19e3b282b3084538e1f92c3456525ba93103ae1";
}
}
wr::Image localized_image(resource::GameLanguage language){(void)identity(language);return language==resource::GameLanguage::english?english_image():initial_image();}
aot::StaticProgram& Runtime::program(){
    if(!program_){
        if(shared_codecs_)host_boundaries_.insert(host_boundaries_.end(),std::begin(codec_boundary_entries),std::end(codec_boundary_entries));
        if(shared_resources_)host_boundaries_.insert(host_boundaries_.end(),std::begin(resource_boundary_entries),std::end(resource_boundary_entries));
        program_=std::make_unique<aot::StaticProgram>(process_.memory,functions(),locate,host_boundaries_);
    }
    return *program_;
}
Runtime::Runtime(wr::ProcessOptions options,wr::ProcessBackends backends,std::shared_ptr<graphics::SurfaceHost> surface,bool shared_codecs,bool captions_enabled,std::shared_ptr<const subtitles::Catalogue> captions,bool shared_resources,resource::GameLanguage language):
    source_identity_(identity(language)),
    shared_codecs_(shared_codecs),
    shared_resources_(shared_resources),
    subtitle_player_(std::make_shared<subtitles::Playback>()),
    subtitle_state_(std::make_shared<SubtitleState>(subtitle_player_)),
    execute_([this,samples=std::make_shared<aot::FunctionWorkSamples>(functions()),music_flags=std::make_shared<aot::MusicFlagSaves>(music_flag_source),reported=false](wr::Cpu& cpu,wr::Memory& memory,bool source)mutable{
        if(!reported){process_.state().reports["static_cpu_work"]=[samples]{return samples->report();};process_.state().reports["music_flag_saves"]=[music_flags]{return music_flags->report();};reported=true;}
        aot::MusicFlagScope music_saves{*music_flags,memory,[](void* owner)->std::optional<aot::MusicFlagStamp>{
            const auto& state=static_cast<wr::Process*>(owner)->state();
            if(state.current_thread_id!=1)return {};
            for(const auto& [id,file]:state.files)if(file.owned && file.stream && file.write)return {};
            const auto revision=platform::compat::save_files_revision(state.mutable_files.get());
            if(!revision)return {};
            return aot::MusicFlagStamp{state.mutable_files.get(),*revision};
        },&process_};
        aot::FunctionWorkScope work{samples.get()};
        aot::SubtitleEventScope subtitle_events{memory,[](std::uint32_t entry,const wr::Cpu& state,wr::Memory& ram,std::uint32_t args,void* owner){static_cast<SubtitleState*>(owner)->returned(entry,state,ram,args);},subtitle_state_.get()};
        aot::LeafServiceScope services{[](wr::Cpu& cpu,wr::Memory& memory,void* owner){
            auto& p=*static_cast<wr::Process*>(owner);return &cpu==&p.cpu && &memory==&p.memory && p.dispatch_leaf_api();
        },&process_};
        return source?program().invoke_source(cpu):program().invoke(cpu);
    }),
    process_(wr::FunctionExecutor{[this](wr::Cpu& cpu,wr::Memory& memory){return execute_(cpu,memory,false);},
        [this](wr::Cpu& cpu,wr::Memory& memory){
            // Resource admission can decline a legacy archive/layout. Codec
            // identity, overlap and capacity guards retain their rejection.
            if(std::find(std::begin(resource_boundary_entries),std::end(resource_boundary_entries),cpu.eip)==std::end(resource_boundary_entries))return false;
            return execute_(cpu,memory,true);
        }},std::move(options),std::move(backends)){
    subtitle_player_->enabled(captions_enabled);
    if(captions_enabled && !captions)throw std::invalid_argument("SC captions need the subtitle resources matching the selected language");
    subtitle_player_->catalogue(std::move(captions));
    process_.state().reports["special_subtitles"]=[state=subtitle_state_]{return state->report();};
    platform::compat::install_scheduler(process_,[this](auto ms,auto caller){return aot::source_fast_forward_idle(process_.memory,ms,caller,fast_forward_source);});
    platform::compat::install_system(process_,aot::prepare_movie_profile,[](wr::Process& p)->std::optional<std::uint64_t>{
        if(p.state().current_thread_id!=1)return {};
        for(const auto& [id,file]:p.state().files)if(file.owned && file.write && !file.path.empty())return {};
        try{auto m=p.memory.execution_scope();if(!locate(m.load<32>(p.cpu.r[wr::ESP])).function)return {};
            return m.load<32>(save_source.update);
        }catch(const wr::GuestFault&){return {};}
    },[](wr::Process& p){return aot::source_profile_write_scope(p,profile_write_source,locate);});
    platform::compat::install_text(process_);
    platform::compat::install_locale(process_);
    platform::compat::install_extended_system(process_);
    process_.state().reports["native_sprite_commands"]=sprite_report;
    process_.state().reports["native_glyph_commands"]=glyph_report;
    process_.state().reports["native_sort"]=sort_report;
    process_.state().reports["native_weather"]=weather_report;
    process_.state().reports["native_image_pixels"]=image_pixel_report;
    process_.state().reports["native_scalar_optimizations"]=scalar_optimization_report;
    process_.state().reports["source_text_replay"]=[]{const auto c=aot::source_text_counts;const auto j=aot::text_jump_counts;
        return "{\"layout_hits\":"+std::to_string(c.layout_hits)+",\"layout_captures\":"+std::to_string(c.layout_captures)+
            ",\"page_spans\":"+std::to_string(c.page_spans)+",\"page_glyphs\":"+std::to_string(c.page_glyphs)+
            ",\"page_captures\":"+std::to_string(c.page_captures)+",\"cancelled\":"+std::to_string(c.cancelled)+
            ",\"local_jumps\":"+std::to_string(j.local_jumps)+",\"budget_yields\":"+std::to_string(j.budget_yields)+
            ",\"external_targets\":"+std::to_string(j.external_targets)+",\"maximum_local_jumps\":128}";};
    process_.state().reports["special_flow"]=[this]{try{
        auto m=process_.memory.execution_scope();return "{\"source_state\":"+std::to_string(m.load<32>(save_source.screen))+
            ",\"source_updates\":"+std::to_string(m.load<32>(save_source.update))+
            ",\"logic_updates\":"+std::to_string(m.load<32>(0xa37070))+
            ",\"current_state\":"+std::to_string(m.load<32>(fast_forward_source.current_state))+
            ",\"completed_state\":"+std::to_string(m.load<32>(fast_forward_source.previous_state))+
            ",\"window_state\":"+std::to_string(m.load<32>(fast_forward_source.window_state))+
            ",\"skip_mode\":"+std::to_string(m.load<32>(fast_forward_source.skip_mode))+
            ",\"skip_latch\":"+std::to_string(m.load<32>(fast_forward_source.skip_latch))+
            ",\"skip_input_held\":"+std::to_string(m.load<8>(fast_forward_source.input_held)&1u)+
            ",\"skip_inhibit\":"+std::to_string(m.load<32>(fast_forward_source.inhibit))+
            ",\"skip_ui_blocked\":"+std::to_string(m.load<32>(fast_forward_source.ui_blocked))+
            ",\"skip_requested\":"+(aot::source_skip_requested(m,fast_forward_source)?"true":"false")+",\"screen_access\":false}";
    }catch(const wr::GuestFault&){return std::string("{\"available\":false}");}};
    process_.state().reports["selected_directory_io"]=[this]{return save_files_report(process_.state().mutable_files);};
    process_.state().reports["native_resources"]=[]{return lzss_report();};
    std::function<std::uint64_t()> image_activity;
    if(shared_resources_){
        auto profile=image_resource_profile();profile.sha256=source_identity_;profile.verify=[this](wr::Memory& memory,wr::U32 pc){return &memory==&process_.memory && program().verify(pc);};
        wr::install_wa2_native_resources(process_,std::move(profile));
        image_activity=process_.state().resource_io_activity;
    }
    // Continuous BGM must not spend all 64 I/O snapshots before the user
    // opens a save page. Keep the initial codec progress and later file work.
    process_.state().resource_io_activity=[this,image_activity]{return (image_activity?image_activity():0)+lzss_activity()+std::min<std::uint64_t>(256,wr::wa2_native_vorbis_activity(process_))+aot::save_page_timing.pages+unsigned(aot::weather_batch_counts.completed!=0)+unsigned(aot::source_text_counts.layout_hits!=0)+unsigned(aot::source_text_counts.page_spans!=0);};
    if(shared_codecs_)wr::install_wa2_native_vorbis(process_,wr::NativeVorbisProfile{
        source_identity_,
        codec_boundary_entries[0],codec_boundary_entries[1],codec_boundary_entries[2],codec_boundary_entries[3],
        [this](wr::Memory& memory,wr::U32 pc){return &memory==&process_.memory && program().verify(pc);}});
    wr::install_wa2_registry(process_);
    wr::install_wa2_exceptions(process_);
    platform::compat::install_acm(process_);
    platform::compat::install_shell(process_,platform::compat::native_shell_services(process_.options.data_root));
    auto audio=wr::install_wa2_audio_service(process_);
    if(surface){
        auto renderer=graphics::install(process_,std::move(surface),subtitle_player_,[this]{return process_.state().current_thread_id==1 && aot::source_fast_forward(process_.memory,fast_forward_source);});
        platform::compat::install_media(process_,std::move(audio),std::move(renderer));
        platform::compat::install_d3dx43_aliases(process_);
    }
    process_.state().reports["program_profile"]=[language]{return std::string("{\"id\":\"special-contents\",\"language\":")+wr::quote(std::string(resource::language_profile(language,resource::ResourceTitle::special_contents).tag))+",\"backend\":\"shared-aot\",\"shared_static_functions\":true,\"main_story_address_hooks\":false,\"complete_gameplay_verified\":false}";};
}
std::uint32_t run(host::Startup startup){
    if(!startup.window)throw std::invalid_argument("missing Special window/device adapter");
    if(startup.test_mode)throw std::invalid_argument("Special Contents full-unlock test profile is not established");
    startup.options.image_name=startup.language.language==resource::GameLanguage::english?"WA2_Special_en.exe":"WA2_Special_cn.exe";
    startup.options.command_line="\"C:\\WinRecomp\\"+startup.options.image_name+"\"";
    const auto counter_sink=startup.io_diagnostic_sink;
    const auto begun=platform::Clock::counter();auto previous=begun;
    const auto phase=[&](std::string_view name)noexcept{
        if(!counter_sink)return;
        try{const auto now=platform::Clock::counter();
            counter_sink("special-contents-aot #0 startup_stage","{\"stage\":"+wr::quote(std::string(name))+
                ",\"elapsed_ns\":"+std::to_string(now-begun)+",\"previous_stage_ns\":"+std::to_string(now-previous)+",\"screen_access\":false}");previous=now;
        }catch(...){}
    };
    phase("runtime");
    wr::ProcessBackends backends;
    backends.gui=[adapter=startup.window](wr::Process& p){return window::install(p,adapter);};
    backends.directdraw=[adapter=startup.window](wr::Process& p){return platform::compat::install_legacy_display(p,adapter);};
    Runtime runtime(std::move(startup.options),std::move(backends),startup.window,true,startup.language.chinese_subtitles,std::move(startup.captions),true,startup.language.language);
    auto& process=runtime.process();
    process.state().reports["host_event_pump"]=[host=startup.window]{return host->event_pump_report();};
    const auto report=[&](std::string_view error){
        if(startup.report_file.empty())return;
        std::ofstream out(startup.report_file);out<<"{\"program\":\"special-contents\",\"error\":"<<wr::quote(std::string(error))<<",\"process\":"<<process.report()<<"}\n";
    };
    try{
        phase("save_directory");
        auto files=startup.files?std::move(startup.files):platform::compat::local_files(process.options.data_root);
        process.state().mutable_files=save_files(std::move(files),&process);
        phase("resource_mount");
        if(startup.mount_resources)startup.mount_resources(process);
        else platform::compat::install_resource_mount(process,platform::compat::local_resource_mount(process.options.data_root));
        platform::compat::select_language_resources(process,startup.language,resource::ResourceTitle::special_contents);
        phase("image_load");
        const auto image=localized_image(startup.language.language);std::ostringstream missing;unsigned count{};
        for(const auto& item:image.imports){const auto name=item.ordinal?"#"+std::to_string(*item.ordinal):item.name;
            if(!process.resolve(item.dll,name)){if(count++)missing<<", ";missing<<item.dll<<'!'<<name;}}
        if(count)throw std::runtime_error("Unsupported Special Contents imports: "+missing.str());
        process.load(image);
        platform::compat::install_io_diagnostics(process,startup.report_file,"special-contents-aot",std::move(startup.io_diagnostic_sink));
        phase("game_startup");const auto code=process.run();phase("finished");
        platform::compat::emit_final_io_counters(process,"special-contents-aot",counter_sink,true);report({});return code;
    }catch(const std::exception& error){phase("failed");
        platform::compat::emit_final_io_counters(process,"special-contents-aot",counter_sink,false);
        try{report(error.what());}catch(...){}throw;}
}
}
