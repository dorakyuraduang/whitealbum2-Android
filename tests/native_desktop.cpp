#include "native_desktop.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

namespace p=wa2::platform;namespace r=wa2::resource;namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}CHECK(failed);}
int main(int argc,char** argv){try{
    CHECK(argc==3);unsigned cases{},guards{};const auto root=fs::absolute(argv[2])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());fs::create_directories(root);
    const auto executable=root/"program",working=root/"working";fs::create_directory(executable);fs::create_directory(working);fs::create_directory(executable/"res");fs::create_directory(working/"res");
    const auto file=[](const fs::path& path){std::ofstream out(path,std::ios::binary);const std::array<char,16> bytes{'K','C','A','P'};out.write(bytes.data(),bytes.size());CHECK(out);};
    const auto resources=[&](const fs::path& directory){file(directory/"grp.pak");file(directory/"ck-gal.pak");};
    CHECK(!p::native_desktop_startup({}).resources && !p::native_desktop_startup({}).game.test_mode);++cases;
    const std::array<std::string_view,3> args{"--test-mode","--resources","owned"};const auto startup=p::native_desktop_startup(args);CHECK(startup.game.test_mode && startup.resources==fs::path("owned"));++cases;
    for(const auto args:{std::vector<std::string_view>{"--resources"},{"--resources",""},{"--resources","a","--resources","b"},{"--test-mode","--test-mode"},{"--unknown"}}){rejects([&]{p::native_desktop_startup(args);});++guards;}
    std::vector<std::string_view> excessive(33,"--test-mode");rejects([&]{p::native_desktop_startup(excessive);});++guards;++cases;
    rejects([&]{p::native_desktop_directory("relative",working);});rejects([&]{p::native_desktop_directory(executable,"relative");});rejects([&]{p::native_desktop_banks("relative");});guards+=3;
    rejects([&]{p::native_desktop_directory(executable,working);});++guards;file(executable/"res"/"grp.pak");rejects([&]{p::native_desktop_directory(executable,working);});++guards;file(executable/"res"/"ck-gal.pak");CHECK(p::native_desktop_directory(executable,working)==executable/"res");++cases;
    resources(working/"res");CHECK(p::native_desktop_directory(executable,working)==executable/"res");resources(executable);CHECK(p::native_desktop_directory(executable,working)==executable);++cases;
    CHECK(p::native_desktop_directory(executable,working,working/"res")==working/"res" && p::native_desktop_directory(executable,working,fs::path("res"))==working/"res");++cases;
    fs::create_directory(root/"empty");CHECK(p::native_desktop_directory(root/"empty",working)==working/"res");resources(working);CHECK(p::native_desktop_directory(root/"empty",working)==working);++cases;
    rejects([&]{p::native_desktop_directory(executable,working,root/"empty");});rejects([&]{p::native_desktop_directory(executable,working,fs::path{});});guards+=2;++cases;
    const auto unicode=root/fs::path(u8"owned-\u8d44\u6e90");fs::create_directory(unicode);resources(unicode);const auto bytes=unicode.generic_u8string();const std::string encoded(bytes.begin(),bytes.end());const std::array<std::string_view,2> unicode_args{"--resources",encoded};const auto unicode_startup=p::native_desktop_startup(unicode_args);CHECK(unicode_startup.resources && *unicode_startup.resources==unicode && p::native_desktop_directory(executable,working,unicode_startup.resources)==unicode);++cases;
    auto banks=p::native_desktop_banks(unicode);CHECK(!banks("absent.pak"));auto data=banks("grp.pak");CHECK(data && data->size()==16);std::array<std::uint8_t,4> header{};data->read_at(0,header);CHECK((header==std::array<std::uint8_t,4>{'K','C','A','P'}));++cases;
    fs::create_directory(unicode/"IC");file(unicode/"IC"/"bak.pak");CHECK(banks("IC/bak.pak")->size()==16);++cases;
    for(auto name:{"../grp.pak","/grp.pak","C:/grp.pak","IC\\bak.pak","grp.txt","grp..pak",""}){rejects([&]{banks(name);});++guards;}++cases;
    fs::create_directory(unicode/"unreadable.pak");rejects([&]{banks("unreadable.pak");});++guards;++cases;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-desktop.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"external_resource_discovery\":true,\"unicode_directories\":true,\"provider_failures_not_missing\":true,\"metadata_only_discovery\":true,\"synthetic_files_only\":true,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);std::cout<<"PASS native desktop arguments, external resource discovery and file-provider errors.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
