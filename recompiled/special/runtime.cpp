#include "runtime.hpp"
#include "lzss.hpp"
#include "render.hpp"
#include "native_entries.hpp"
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
aot::StaticProgram& Runtime::program(){
    if(!program_)program_=std::make_unique<aot::StaticProgram>(process_.memory,functions(),locate,
        shared_codecs_?std::span<const std::uint32_t>(codec_boundary_entries):std::span<const std::uint32_t>{});
    return *program_;
}
Runtime::Runtime(wr::ProcessOptions options,wr::ProcessBackends backends,std::shared_ptr<graphics::SurfaceHost> surface,bool shared_codecs):
    shared_codecs_(shared_codecs),
    process_(wr::FunctionExecutor{[this,samples=std::make_shared<aot::FunctionWorkSamples>(functions()),reported=false](wr::Cpu& cpu,wr::Memory& memory)mutable{
        if(!reported){process_.state().reports["static_cpu_work"]=[samples]{return samples->report();};reported=true;}
        aot::FunctionWorkScope work{samples.get()};
        aot::LeafServiceScope services{[](wr::Cpu& cpu,wr::Memory& memory,void* owner){
            auto& p=*static_cast<wr::Process*>(owner);return &cpu==&p.cpu && &memory==&p.memory && p.dispatch_leaf_api();
        },&process_};
        return program().invoke(cpu);
    }},std::move(options),std::move(backends)){
    platform::compat::install_scheduler(process_);
    platform::compat::install_system(process_);
    platform::compat::install_text(process_);
    platform::compat::install_locale(process_);
    platform::compat::install_extended_system(process_);
    process_.state().reports["native_sprite_commands"]=sprite_report;
    process_.state().reports["native_glyph_commands"]=glyph_report;
    process_.state().reports["selected_directory_io"]=[this]{return save_files_report(process_.state().mutable_files);};
    process_.state().reports["native_resources"]=[]{return lzss_report();};
    process_.state().resource_io_activity=[this]{return lzss_activity()+wr::wa2_native_vorbis_activity(process_);};
    if(shared_codecs_)wr::install_wa2_native_vorbis(process_,wr::NativeVorbisProfile{
        "1a93d3126a90c68d07c0f79ec19e3b282b3084538e1f92c3456525ba93103ae1",
        codec_boundary_entries[0],codec_boundary_entries[1],codec_boundary_entries[2],codec_boundary_entries[3],
        [this](wr::Memory& memory,wr::U32 pc){return &memory==&process_.memory && program().verify(pc);}});
    wr::install_wa2_registry(process_);
    wr::install_wa2_exceptions(process_);
    platform::compat::install_acm(process_);
    platform::compat::install_shell(process_,platform::compat::native_shell_services(process_.options.data_root));
    auto audio=wr::install_wa2_audio_service(process_);
    if(surface){
        auto renderer=graphics::install(process_,std::move(surface));
        platform::compat::install_media(process_,std::move(audio),std::move(renderer));
        platform::compat::install_d3dx43_aliases(process_);
    }
    process_.state().reports["program_profile"]=[]{return std::string(
        "{\"id\":\"special-contents-cn\",\"backend\":\"shared-aot\",\"main_story_address_hooks\":false,\"complete_gameplay_verified\":false}");};
}
std::uint32_t run(host::Startup startup){
    if(!startup.window)throw std::invalid_argument("missing Special window/device adapter");
    if(startup.test_mode)throw std::invalid_argument("Special Contents full-unlock test profile is not established");
    startup.options.image_name="WA2_Special_cn.exe";
    startup.options.command_line="\"C:\\WinRecomp\\WA2_Special_cn.exe\"";
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
    Runtime runtime(std::move(startup.options),std::move(backends),startup.window);
    auto& process=runtime.process();
    process.state().reports["host_event_pump"]=[host=startup.window]{return host->event_pump_report();};
    const auto report=[&](std::string_view error){
        if(startup.report_file.empty())return;
        std::ofstream out(startup.report_file);out<<"{\"program\":\"special-contents-cn\",\"error\":"<<wr::quote(std::string(error))<<",\"process\":"<<process.report()<<"}\n";
    };
    try{
        phase("save_directory");
        auto files=startup.files?std::move(startup.files):platform::compat::local_files(process.options.data_root);
        process.state().mutable_files=save_files(std::move(files));
        phase("resource_mount");
        if(startup.mount_resources)startup.mount_resources(process);
        else platform::compat::install_resource_mount(process,platform::compat::local_resource_mount(process.options.data_root));
        phase("image_load");
        const auto image=initial_image();std::ostringstream missing;unsigned count{};
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
