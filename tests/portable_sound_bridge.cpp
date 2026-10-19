#include "../port/audio.hpp"
#include "../port/audio_abi.hpp"
#include "../platform/compat/scheduler.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;
struct Counts {unsigned opens{},closes{},renders{};};
class MemoryOutput final:public wa2::platform::AudioOutput {
    wa2::audio::Mixer& mixer_;std::shared_ptr<Counts> counts_;bool open_{};
public:
    MemoryOutput(wa2::audio::Mixer& mixer,std::shared_ptr<Counts> counts):mixer_(mixer),counts_(std::move(counts)){}
    bool open(const std::string&) override{if(!open_){open_=true;++counts_->opens;}return true;}
    std::vector<std::string> devices() override{return {"Synthetic memory output"};}
    void close() override{if(open_){open_=false;++counts_->closes;}}
    void pause(bool) override{}
    wa2::platform::AudioStatus status() const override{return {48000,512,counts_->renders,0,0,"synthetic-memory",""};}
    std::vector<float> render(unsigned frames){CHECK(open_ && frames<=32);std::vector<float> samples(frames*2);mixer_.render(samples,48000);++counts_->renders;return samples;}
};
Image fixture_image(){Image value;value.base=0x400000;value.size=0x10000;value.entry=0x401000;value.headers_size=4096;value.bytes.resize(8192);value.sections={{".text",4096,4096,4096,4096,0x60000020}};return value;}
void same_samples(std::span<const float> samples,std::initializer_list<int> expected){CHECK(samples.size()==expected.size()*2);unsigned n{};for(auto value:expected){const auto sample=float(value)/32768;CHECK(std::abs(samples[n++]-sample)<0.000001f && std::abs(samples[n++]-sample)<0.000001f);}}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned checked{};
    for(bool linear:{false,true}){auto counts=std::make_shared<Counts>();{
        ProcessOptions options;options.prefer_linear_memory=linear;Process p(FunctionExecutor{[](Cpu&,Memory&){return false;}},options);
        wa2::platform::compat::install_scheduler(p);MemoryOutput* output{};
        install_wa2_audio(p,[&](wa2::audio::Mixer& mixer){auto result=std::make_unique<MemoryOutput>(mixer,counts);output=result.get();return result;});p.load(fixture_image());
        const auto work=p.allocate_bytes(4096);auto& m=p.memory;
        auto kernel=[&](const char* name,std::initializer_list<U32> args){const auto target=p.resolve("kernel32.dll",name);CHECK(target);return p.callback(target,{args.begin(),args.size()});};
        auto method=[&](U32 object,unsigned slot,std::initializer_list<U32> args){std::vector<U32> parameters{object};parameters.insert(parameters.end(),args.begin(),args.end());const auto target=m.load(m.load(object,32)+slot*4,32);return p.callback(target,parameters);};
        CHECK(p.callback(p.resolve("dsound.dll","#11"),std::array<U32,3>{0,work,0})==0 && counts->opens==1);const auto device=m.load(work,32);CHECK(device);
        const auto format=work+128,descriptor=work+64;
        for(auto [offset,value,bits]:{std::tuple{0u,1u,16u},{2u,1u,16u},{4u,48000u,32u},{8u,96000u,32u},{12u,2u,16u},{14u,16u,16u},{16u,0u,16u}})m.store(format+offset,value,bits);
        m.store(descriptor,36,32);m.store(descriptor+4,0x1e0,32);m.store(descriptor+8,16,32);m.store(descriptor+16,format,32);
        CHECK(method(device,3,{descriptor,work+4,0})==0);const auto buffer=m.load(work+4,32);CHECK(buffer);
        auto upload=[&](unsigned offset,std::initializer_list<int> values){const auto size=U32(values.size()*2);
            CHECK(method(buffer,11,{offset,size,work+256,work+260,work+264,work+268,0})==0);
            const auto first=m.load(work+256,32),first_bytes=m.load(work+260,32),second=m.load(work+264,32),second_bytes=m.load(work+268,32);CHECK(first_bytes+second_bytes==size);
            unsigned index{};for(auto sample:values){const auto at=index*2;const auto destination=at<first_bytes?first+at:second+at-first_bytes;m.store(destination,U32(sample)&65535,16);++index;}
            CHECK(method(buffer,19,{first,first_bytes,second,second_bytes})==0);
        };
        upload(0,{1000,2000,3000,4000,5000,6000,7000,8000});
        const auto event=kernel("CreateEventA",{0,0,0,0}),stopped=kernel("CreateEventA",{0,0,0,0});CHECK(event && stopped);
        const auto guid=audio_abi::notify;m.copy_in(work+320,{reinterpret_cast<const std::uint8_t*>(&guid),sizeof(guid)});
        CHECK(method(buffer,0,{work+320,work+8})==0);const auto notify=m.load(work+8,32);CHECK(notify);
        m.store(work+352,6,32);m.store(work+356,event,32);m.store(work+360,0xffffffff,32);m.store(work+364,stopped,32);CHECK(method(notify,3,{2,work+352})==0);
        CHECK(method(buffer,12,{0,0,1})==0);same_samples(output->render(4),{1000,2000,3000,4000});
        CHECK(method(buffer,4,{work+384,work+388})==0 && m.load(work+384,32)==8);CHECK(kernel("WaitForSingleObject",{event,0})==0);
        // Reissuing Play and replacing the next streaming half must retain
        // the playback cursor. This caught the old short-segment BGM repeat.
        upload(8,{9000,10000,11000,12000});CHECK(method(buffer,12,{0,0,1})==0);same_samples(output->render(4),{9000,10000,11000,12000});
        same_samples(output->render(8),{1000,2000,3000,4000,9000,10000,11000,12000});
        upload(12,{13000,14000,15000,16000});CHECK(method(buffer,13,{12})==0);same_samples(output->render(4),{13000,14000,15000,16000});
        CHECK(method(buffer,18,{})==0 && kernel("WaitForSingleObject",{stopped,0})==0);
        CHECK(method(device,5,{buffer,work+12})==0);const auto duplicate=m.load(work+12,32);CHECK(duplicate);
        CHECK(method(duplicate,12,{0,0,1})==0);same_samples(output->render(2),{15000,16000});CHECK(method(duplicate,2,{})==0);
        CHECK(method(buffer,2,{})==0 && method(notify,2,{})==0 && method(device,2,{})==0);
        CHECK(kernel("CloseHandle",{event})==1 && kernel("CloseHandle",{stopped})==1);
        const auto report=p.state().reports.at("directsound")();CHECK(report.find("\"mixer_live_buffers\":0")!=report.npos && report.find("\"staging_bytes\":0")!=report.npos && report.find("\"live_objects\":0")!=report.npos);
        CHECK(counts->opens==1 && counts->renders==5 && p.executed_instructions==0);++checked;
    }CHECK(counts->closes==1);}
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.shared-sound-bridge.v1\",\"memory_backends\":"<<checked<<",\"streaming_halves_and_repeated_play\":true,\"wrapped_upload_and_duplicate_storage\":true,\"portable_event_notifications\":true,\"buffers_and_staging_released\":true,\"audio_hardware_opened\":false,\"sdl_dummy_device_opened\":false,\"instruction_step_calls\":0,\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS shared sound bridge: streamed halves, cursor, wrapped locks, notifications and release with memory output only.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
