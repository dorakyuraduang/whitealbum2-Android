#include "startup.hpp"
#include "../../recompiled/process.hpp"
#include "../../platform/window/gui.hpp"
#include "../../platform/compat/legacy_display.hpp"
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
    startup.options.image_name="WA2_chs.exe";startup.options.command_line="\"C:\\WinRecomp\\WA2_chs.exe\"";
    wr::ProcessBackends backends;
    backends.gui=[host=startup.window](wr::Process& p){return window::install(p,host);};
    backends.directdraw=[host=startup.window](wr::Process& p){return platform::compat::install_legacy_display(p,host);};
    aot::Runtime runtime(std::move(startup.options),std::move(backends),startup.window);
    auto& process=runtime.process();
    const auto report=[&](std::string_view error){
        if(startup.report_file.empty())return;
        if(startup.report_file.extension()!=".json")throw std::invalid_argument("Runtime reports require a .json filename");
        std::ofstream output(startup.report_file,std::ios::binary|std::ios::trunc);
        if(!output)throw std::runtime_error("Cannot write runtime report");
        output<<"{\"host_release\":"<<wr::quote(WA2_HOST_VERSION)<<",\"error\":"<<wr::quote(std::string(error))<<",\"process\":"<<process.report()<<"}\n";
        if(!output)throw std::runtime_error("Cannot finish runtime report");
    };
    try{const auto image=aot::initial_image();require_imports(process,image);
        if(startup.mount_resources)startup.mount_resources(process);
        runtime.load(image);const auto result=runtime.run();report({});return result;
    }catch(const std::exception& error){try{report(error.what());}catch(...){}throw;}
}
}
