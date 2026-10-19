#include "winrecomp/core.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
namespace {
std::uint32_t number(const std::string& value) {
    if(value.empty() || value.front()=='-')throw std::runtime_error("invalid unsigned number");
    std::size_t used{};const auto n=std::stoull(value,&used,0);
    if(used!=value.size() || n>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("invalid 32-bit number");return std::uint32_t(n);
}
}
int main(int argc,char** argv) {
    try {
#ifdef _WIN32
        if(_setmode(_fileno(stdout),_O_BINARY)==-1)throw std::runtime_error("cannot set binary stdout");
#endif
        if(argc==2 && std::string(argv[1])=="--version"){std::cout<<"WinRecomp 0.2.0-runtime-experimental (Zydis 4.1.1)\n";return 0;}
        if(argc<3){std::cerr<<"usage: winrecomp analyze|cfg|lift|dump-image|coverage|project <input.exe> [output] [--entry 0xVA] [--max-instructions N] [--name identifier] [--entry-only] [--seed 0xVA] [--trap-unsupported]\n";return 2;}
        const std::string command=argv[1];
        if(command!="analyze" && command!="cfg" && command!="lift" && command!="dump-image" && command!="coverage" && command!="project")throw std::runtime_error("unknown command");
        const auto image=wr::Image::load(argv[2]);auto entry=image.entry;std::uint32_t budget=1000000;bool image_roots=true,partial=false;
        std::string output,name="recompiled";std::set<wr::Address> seeds;
        for(int n=3;n<argc;++n) {
            const std::string arg=argv[n];
            if(arg=="--trap-unsupported"){partial=true;continue;}
            if(arg=="--entry-only"){image_roots=false;continue;}
            if(arg=="--entry" || arg=="--max-instructions" || arg=="--name" || arg=="--seed") {
                if(n+1>=argc)throw std::runtime_error("option requires a value: "+arg);
                const std::string value=argv[++n];
                if(arg=="--seed")seeds.insert(number(value));
                else if(arg=="--entry"){entry=number(value);image_roots=false;}
                else if(arg=="--max-instructions")budget=number(value);
                else name=value;
            } else if(arg.starts_with("--"))throw std::runtime_error("unknown option: "+arg);
            else if(output.empty())output=arg;else throw std::runtime_error("unexpected argument: "+arg);
        }
        if(!output.empty()) {
            auto in=std::filesystem::weakly_canonical(argv[2]);
            auto out=std::filesystem::weakly_canonical(output);
            if(in==out || (std::filesystem::exists(out) && std::filesystem::equivalent(in,out)))throw std::runtime_error("refusing to overwrite the input executable");
        }
        if(command=="dump-image") {
            if(output.empty())throw std::runtime_error("dump-image needs an output path");
            const auto bytes=image.mapped();wr::write_file(output,std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));return 0;
        }
        if(command=="analyze") {const auto text=image.json()+"\n";if(output.empty())std::cout<<text;else wr::write_file(output,text);return 0;}
        auto g=wr::discover(image,entry,budget,image_roots,seeds);
        if(command=="coverage"){const auto text=wr::coverage_json(g);if(output.empty())std::cout<<text;else wr::write_file(output,text);return g.budget_exhausted?3:0;}
        if(command=="cfg") {const auto text=g.json(image);if(output.empty())std::cout<<text;else wr::write_file(output,text);return g.budget_exhausted?3:0;}
        if(command=="project") {if(output.empty())throw std::runtime_error("project requires an output directory");wr::emit_project(image,g,output,partial);std::cout<<"generated native project with "<<g.instructions.size()<<" admitted instructions\n";return 0;}
        if(output.empty())throw std::runtime_error("lift needs an output C++ path");
        // Complete preflight before opening/truncating any output file.
        const auto source=wr::emit_cpp(g,name,partial);wr::write_file(output,source);
        std::cout<<"lifted "<<g.instructions.size()<<" instructions in "<<g.blocks.size()<<" blocks\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"WinRecomp: "<<e.what()<<'\n';return 1;}
}
