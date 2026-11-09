#include "startup.hpp"
#include "../../recompiled/process.hpp"
#include "../../recompiled/save_read_scope.hpp"
#include "../../platform/window/gui.hpp"
#include "../../platform/compat/legacy_display.hpp"
#include "../../platform/compat/test_profile.hpp"
#include "../../platform/compat/resource_mount.hpp"
#include "../../platform/compat/save_files.hpp"
#include <winrecomp/win32_state.hpp>
#include <sstream>
#include <fstream>
namespace wa2::host {
void require_imports(wr::Process& process,const wr::Image& image){
    std::ostringstream missing;unsigned count{};
    for(const auto& item:image.imports){const auto name=item.ordinal?"#"+std::to_string(*item.ordinal):item.name;
        if(!process.resolve(item.dll,name)){if(count++)missing<<", ";missing<<item.dll<<'!'<<name;}}
    if(count)throw std::runtime_error("Shared platform services are incomplete: "+missing.str());
}
std::uint32_t run(Startup startup){
    if(!startup.window)throw std::invalid_argument("missing window/device adapter");
    if(startup.files && !startup.mount_resources){const auto root=startup.options.data_root;startup.mount_resources=[root](wr::Process& p){platform::compat::install_resource_mount(p,platform::compat::local_resource_mount(root));};}
    if(startup.test_mode && !startup.files){
        if(!startup.options.allow_file_write)throw std::runtime_error("Test mode needs a writable private save directory");
        const auto normal_root=startup.options.data_root;
        startup.options.data_root=platform::compat::select_test_profile(normal_root,true);
        // Android already mounts the authorized external provider. Desktop
        // must keep PAKs in the original root while saves use the test root.
        if(!startup.mount_resources)startup.mount_resources=[normal_root](wr::Process& p){
            platform::compat::install_resource_mount(p,platform::compat::local_resource_mount(normal_root));
        };
    }
    startup.options.image_name="WA2_chs.exe";startup.options.command_line="\"C:\\WinRecomp\\WA2_chs.exe\"";
    wr::ProcessBackends backends;
    backends.gui=[host=startup.window](wr::Process& p){return window::install(p,host);};
    backends.directdraw=[host=startup.window](wr::Process& p){return platform::compat::install_legacy_display(p,host);};
    aot::Runtime runtime(std::move(startup.options),std::move(backends),startup.window);
    auto& process=runtime.process();
    process.state().reports["host_event_pump"]=[host=startup.window]{return host->event_pump_report();};
    const auto report=[&](std::string_view error){
        if(startup.report_file.empty())return;
        if(startup.report_file.extension()!=".json")throw std::invalid_argument("Runtime reports require a .json filename");
        std::ofstream output(startup.report_file,std::ios::binary|std::ios::trunc);
        if(!output)throw std::runtime_error("Cannot write runtime report");
        output<<"{\"host_release\":"<<wr::quote(WA2_HOST_VERSION)<<",\"error\":"<<wr::quote(std::string(error))<<",\"process\":"<<process.report()<<"}\n";
        if(!output)throw std::runtime_error("Cannot finish runtime report");
    };
    try{const auto image=aot::initial_image();require_imports(process,image);
        if(startup.files){process.state().mutable_files=platform::compat::save_files(std::move(startup.files),startup.test_mode,aot::traced_save_read_scope(process));
            process.state().reports["selected_directory_io"]=[&process]{return platform::compat::save_files_report(process.state().mutable_files);};}
        if(startup.mount_resources)startup.mount_resources(process);
        runtime.load(image);platform::compat::install_io_diagnostics(process,startup.report_file,WA2_HOST_VERSION,std::move(startup.io_diagnostic_sink));
        const auto result=runtime.run();report({});return result;
    }catch(const std::exception& error){try{report(error.what());}catch(...){}throw;}
}
}
