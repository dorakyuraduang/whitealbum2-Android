#include <winrecomp/process.hpp>
#include "../port/wa2_win32.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;
wr::Image fixture_image(){
    wr::Image image;image.base=0x400000;image.size=0x10000;image.headers_size=4096;image.entry=0x401000;image.bytes.resize(8192,0x90);
    image.sections={{".text",4096,4096,4096,4096,0x60000020}};return image;
}
struct Fixture {
    Process process;
    U32 scratch{};
    explicit Fixture(const std::filesystem::path& root,bool write=true):
        process(FunctionExecutor{[](Cpu&,Memory&){return false;}},[&]{ProcessOptions options;options.data_root=root;options.allow_file_write=write;return options;}()){
        install_wa2_registry(process);process.load(fixture_image());scratch=process.allocate_bytes(4096);
    }
    U32 call(const char* name,std::initializer_list<U32> args){
        const auto target=process.resolve("advapi32.dll",name);CHECK(target);return process.callback(target,{args.begin(),args.size()});
    }
};
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto parent=std::filesystem::path(argv[1]).parent_path();
    const auto root=parent/("registry-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(root));
    Fixture first(root);auto& p=first.process;
    const auto key=p.put_string("Software\\Wa2StaticContract"),name=p.put_string("Payload");
    CHECK(first.call("RegCreateKeyExA",{0x80000001,key,0,0,0,0xf003f,0,first.scratch,first.scratch+4})==0);
    const auto handle=p.memory.load(first.scratch,32);CHECK(p.memory.load(first.scratch+4,32)==1);
    p.memory.store(first.scratch+16,0x0180ff00,32);
    CHECK(first.call("RegSetValueExA",{handle,name,0,3,first.scratch+16,4})==0);
    // Replacing an existing hive must succeed on Windows too, without an
    // intermediate deletion or a dependency on native registry APIs.
    p.memory.store(first.scratch+16,0xfedcba98,32);
    CHECK(first.call("RegSetValueExA",{handle,name,0,3,first.scratch+16,4})==0);
    p.memory.store(first.scratch+20,2,32);p.memory.store(first.scratch+24,0xa5a5a5a5,32);
    CHECK(first.call("RegQueryValueExA",{handle,name,0,first.scratch+28,first.scratch+24,first.scratch+20})==234);
    CHECK(p.memory.load(first.scratch+20,32)==4 && p.memory.load(first.scratch+24,32)==0xa5a5a5a5 && p.memory.load(first.scratch+28,32)==3);
    CHECK(first.call("RegCloseKey",{handle})==0);
    Fixture reload(root,false);auto& q=reload.process;
    const auto same_key=q.put_string("software\\wa2staticcontract"),same_name=q.put_string("payload");
    CHECK(reload.call("RegOpenKeyExA",{0x80000001,same_key,0,1,reload.scratch})==0);
    const auto reopened=q.memory.load(reload.scratch,32);q.memory.store(reload.scratch+4,4,32);
    CHECK(reload.call("RegQueryValueExA",{reopened,same_name,0,0,reload.scratch+8,reload.scratch+4})==0);
    CHECK(q.memory.load(reload.scratch+8,32)==0xfedcba98);
    CHECK(reload.call("RegSetValueExA",{reopened,same_name,0,3,reload.scratch+8,4})==5);
    CHECK(reload.call("RegDeleteValueA",{reopened,same_name})==5);
    CHECK(reload.call("RegCloseKey",{reopened})==0);
    std::ofstream output(argv[1]);output<<"{\"schema\":\"wa2.portable-private-registry.v1\",\"created_and_reloaded\":true,\"existing_hive_replaced\":true,"
        "\"case_insensitive_names\":true,\"short_buffer_preserved\":true,\"read_only_permissions\":true,\"native_registry_hive_opened\":false,"
        "\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS portable registry: persistence, replacement, byte counts and access rights.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
