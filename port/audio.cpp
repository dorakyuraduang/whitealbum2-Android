// Compatibility adapter for the original serialized sound calls. The engine
// mixes ordinary native PCM buffers; SDL owns output, never native DirectSound.
#include "guest_api.hpp"
#include "wa2_win32.hpp"
#include "audio.hpp"
#include "audio_abi.hpp"
#include "pcm_audio.hpp"
#include "sdl_audio.hpp"
#include "../platform/compat/interface_handles.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <deque>
#include <map>
#include <sstream>
#include <utility>
namespace wr { namespace {
using namespace port;
namespace abi=audio_abi;
using wa2::audio::Format;
using wa2::audio::Encoding;
enum class Kind {unknown,sound,sound8,buffer,buffer8,notify};
#include "audio_tables.inc"
std::uint64_t clock_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
U32 guest_bytes(Process& p,U32 bytes){const auto at=p.virtual_alloc(0,std::max<U32>(bytes,1),0x3000,4);if(!at)throw std::bad_alloc();p.state().virtual_reservations.erase(at);return at;}
struct Wave {Format format;std::vector<std::uint8_t> bytes;};
std::optional<Wave> wave_format(Process& p,U32 at){
    const auto tag=p.memory.load(at,16),channels=p.memory.load(at+2,16),rate=p.memory.load(at+4,32),average=p.memory.load(at+8,32),align=p.memory.load(at+12,16),bits=p.memory.load(at+14,16),extra=p.memory.load(at+16,16);
    if(extra>4096)return {};Wave value;value.bytes.resize(18+extra);p.memory.copy_out(at,value.bytes);
    U32 encoding=tag;if(tag==0xfffe){if(extra<22)return {};const auto id=read<abi::Guid>(p,at+24);const abi::Guid pcm{1,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}},floating{3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};if(id==pcm)encoding=1;else if(id==floating)encoding=3;else return {};if(p.memory.load(at+18,16)!=bits)return {};}
    if(encoding==1){switch(bits){case 8:value.format.encoding=Encoding::unsigned8;break;case 16:value.format.encoding=Encoding::signed16;break;case 24:value.format.encoding=Encoding::signed24;break;case 32:value.format.encoding=Encoding::signed32;break;default:return {};}}
    else if(encoding==3 && bits==32)value.format.encoding=Encoding::float32;else return {};
    value.format.channels=std::uint16_t(channels);value.format.rate=rate;
    if(!value.format.valid() || align!=value.format.frame_bytes() || std::uint64_t(align)*rate!=average)return {};return value;
}
std::vector<std::uint8_t> default_format(){return {1,0,2,0,0x80,0xbb,0,0,0,0xee,2,0,4,0,16,0,0,0};}
struct Lock {U32 first_guest{},second_guest{},first_size{},second_size{},offset{},flags{};std::uint64_t started{};};
struct Buffer {
    Process& p;std::shared_ptr<wa2::audio::Buffer> pcm;
    std::vector<std::uint8_t> format;
    U32 flags{},staging{},staging_capacity{};std::optional<Lock> lock;
    explicit Buffer(Process& process):p(process){}
    ~Buffer(){if(staging)try{p.memory.release(staging);}catch(...) {}}
    void reserve_staging(U32 bytes){
        if(bytes<=staging_capacity)return;const auto fresh=guest_bytes(p,bytes);
        if(staging)p.memory.release(staging);staging=fresh;staging_capacity=bytes;
    }
};
struct Identity {std::shared_ptr<Buffer> buffer;std::map<Kind,U32> interfaces;};
struct Object {Kind kind;std::shared_ptr<Identity> identity;U32 refs{1};std::uint64_t lifetime{};};
struct AudioTraceEvent {std::uint64_t milliseconds{},locked_ms{},sample_hash{};const char* operation{};U32 thread{},offset{},bytes{},play_cursor{},write_cursor{},flags{};};
struct AudioBufferTrace {U32 bytes{},bytes_per_second{},frequency{};std::uint64_t plays{},stops{},uploads{},uploaded_bytes{},lifetime{};std::vector<AudioTraceEvent> recent;};
class Sound {
    Process& p;wa2::platform::compat::InterfaceHandles handles;wa2::audio::Mixer mixer;std::unique_ptr<wa2::platform::AudioOutput> output;
    std::map<U32,Object> objects;std::map<Kind,U32> vtables;
    std::map<U32,AudioBufferTrace> traces;std::deque<std::pair<U32,AudioBufferTrace>> retired;
    bool stopped{};U32 speaker{0x00140004};
    std::uint64_t created{},devices{},buffers{},plays{},uploads{},enumerations{},largest_lock{},staging_growths{};
    const std::uint64_t start=clock_ms();
    [[noreturn]]void stop(const std::string& message){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"sound ABI: "+message);}
    Object& object(U32 at){const auto it=objects.find(at);if(it==objects.end() || !it->second.refs)stop("stale sound interface");return it->second;}
    wa2::audio::Position position(const Buffer& b) const {const auto s=output->status();return b.pcm->position(s.frames*2,s.rate);}
    void trace(Object& o,Args a,const char* operation,U32 offset=0,U32 bytes=0,U32 flags=0,std::uint64_t locked_ms=0,std::uint64_t hash=0){
        const auto& b=*o.identity->buffer;auto& t=traces[a[0]];
        if(t.lifetime!=o.lifetime){t=AudioBufferTrace{};t.lifetime=o.lifetime;t.bytes=b.pcm->bytes();const auto f=b.pcm->format();t.bytes_per_second=f.rate*f.frame_bytes();}
        const auto cursor=position(b);t.frequency=cursor.frequency;
        if(std::string_view(operation)=="play")++t.plays;
        if(std::string_view(operation)=="stop")++t.stops;
        if(std::string_view(operation)=="unlock"){++t.uploads;t.uploaded_bytes+=bytes;if(bytes<t.bytes/4)return;}
        if(t.recent.size()==96)t.recent.erase(t.recent.begin());
        t.recent.push_back({clock_ms()-start,locked_ms,hash,operation,p.state().current_thread_id,offset,bytes,cursor.play,cursor.write,flags});
    }
    U32 vtable(Kind kind){
        if(const auto it=vtables.find(kind);it!=vtables.end())return it->second;const auto info=methods(kind);const auto at=guest_bytes(p,U32(info.size()*4));
        for(unsigned n=0;n<info.size();++n){const auto name="COM."+std::to_string(unsigned(kind))+"."+info[n].name;
            api(p,"wa2.dsound",name,info[n].arguments,[this,kind,n](Process&,Args a){auto& o=object(a[0]);if(o.kind!=kind)stop("wrong sound vtable");try{return method(o,n,a);}catch(const std::bad_alloc&){return abi::out_of_memory;}});
            p.memory.store(at+n*4,p.resolve("wa2.dsound",name),32);
        }p.memory.protect(at,4096,Memory::Read);vtables.emplace(kind,at);return at;
    }
    U32 wrap(std::shared_ptr<Identity> identity,Kind kind){
        if(const auto it=identity->interfaces.find(kind);it!=identity->interfaces.end()){++object(it->second).refs;return it->second;}
        if(objects.size()>=4096)stop("sound interface count limit");const auto table=vtable(kind),at=handles.acquire(table);
        try{objects.emplace(at,Object{kind,identity,1,++created});identity->interfaces[kind]=at;}catch(...){objects.erase(at);handles.release(at);throw;}return at;
    }
    U32 query(Object& o,Args a){
        const auto id=read<abi::Guid>(p,a[1]);p.memory.check(a[2],4,Memory::Write);p.memory.store(a[2],0,32);std::optional<Kind> kind;
        if(id==abi::unknown)kind=Kind::unknown;
        else if(!o.identity->buffer){if(id==abi::sound)kind=Kind::sound;else if(id==abi::sound8)kind=Kind::sound8;}
        else{if(id==abi::buffer)kind=Kind::buffer;else if(id==abi::buffer8 && !(o.identity->buffer->flags&abi::primary))kind=Kind::buffer8;else if(id==abi::notify && o.identity->buffer->flags&abi::control_notify)kind=Kind::notify;}
        if(!kind)return abi::no_interface;p.memory.store(a[2],wrap(o.identity,*kind),32);return abi::success;
    }
    U32 release(Object& o,U32 at){
        if(--o.refs)return o.refs;
        if(auto t=traces.find(at);t!=traces.end()){retired.emplace_back(at,std::move(t->second));traces.erase(t);if(retired.size()>32)retired.pop_front();}
        o.identity->interfaces.erase(o.kind);handles.release(at);objects.erase(at);return 0;
    }
    U32 create_buffer(Args a){
        if(a[3])return abi::no_aggregation;p.memory.check(a[2],4,Memory::Write);p.memory.store(a[2],0,32);
        const auto size=p.memory.load(a[1],32);if(size!=20 && size!=36)return abi::invalid_parameter;p.memory.check(a[1],size,Memory::Read);
        const auto flags=p.memory.load(a[1]+4,32),bytes=p.memory.load(a[1]+8,32),format=p.memory.load(a[1]+16,32);
        if(flags&abi::control3d)return abi::control_unavailable;
        if(flags&abi::hardware)return abi::control_unavailable;
        auto b=std::make_shared<Buffer>(p);b->flags=(flags&~abi::hardware)|abi::software;
        Format pcm;
        if(flags&abi::primary){if(bytes || format)return abi::invalid_parameter;b->format=default_format();}
        else{if(!format || bytes<4 || bytes>abi::max_bytes)return abi::invalid_parameter;const auto wave=wave_format(p,format);if(!wave || bytes%wave->format.frame_bytes())return abi::bad_format;pcm=wave->format;b->format=wave->bytes;}
        b->pcm=mixer.create(pcm,flags&abi::primary?32768:bytes);auto id=std::make_shared<Identity>();id->buffer=std::move(b);
        p.memory.store(a[2],wrap(std::move(id),Kind::buffer),32);++buffers;return abi::success;
    }
    U32 lock(Object& o,Args a){
        auto& b=*o.identity->buffer;if(b.lock)return abi::invalid_call;
        if(!a[3] || !a[4] || bool(a[5])!=bool(a[6]) || a[7]&~3u)return abi::invalid_parameter;
        for(unsigned n=3;n<=6;++n)if(a[n])p.memory.check(a[n],4,Memory::Write);
        const auto size=b.pcm->bytes();const U32 offset=a[7]&abi::from_write_cursor?position(b).write:a[1],bytes=a[7]&abi::entire_buffer?size:a[2];
        if(offset>=size || bytes>size)return abi::invalid_parameter;
        const auto first=std::min(bytes,size-offset),second=a[5]?bytes-first:0,total=first+second;
        // The accepted range comes from the actual buffer, not a smaller
        // arbitrary transfer cap. Reuse one staging region until release.
        if(total>b.staging_capacity){b.reserve_staging(total);++staging_growths;}
        if(first){b.pcm->edit(offset,first,[&](auto data){p.memory.copy_in(b.staging,data);});}
        if(second){b.pcm->edit(0,second,[&](auto data){p.memory.copy_in(b.staging+first,data);});}
        Lock value{first?b.staging:0,second?b.staging+first:0,first,second,offset,a[7],clock_ms()};
        p.memory.store(a[3],value.first_guest,32);p.memory.store(a[4],first,32);if(a[5])p.memory.store(a[5],value.second_guest,32);if(a[6])p.memory.store(a[6],second,32);
        b.lock=value;largest_lock=std::max<std::uint64_t>(largest_lock,total);return abi::success;
    }
    U32 unlock(Object& o,Args a){
        auto& b=*o.identity->buffer;if(!b.lock)return abi::invalid_call;const auto value=*b.lock;
        if(a[1]!=value.first_guest || a[3]!=value.second_guest || a[2]>value.first_size || a[4]>value.second_size)return abi::invalid_parameter;
        p.memory.check(a[1],a[2],Memory::Read);p.memory.check(a[3],a[4],Memory::Read);
        std::uint64_t hash=14695981039346656037ull;
        const auto upload=[&](U32 offset,U32 guest,U32 bytes){if(bytes)b.pcm->edit(offset,bytes,[&](auto data){p.memory.copy_out(guest,data);for(std::size_t n=0;n<data.size();n+=64){hash^=data[n];hash*=1099511628211ull;}});};
        upload(value.offset,a[1],a[2]);upload(0,a[3],a[4]);b.lock.reset();++uploads;
        trace(o,a,"unlock",value.offset,a[2]+a[4],value.flags,clock_ms()-value.started,hash);
        // Cache ordinary streaming halves, but do not retain an extra 50 MiB
        // copy for the whole duration of a long sound effect. This is a cache
        // limit only: valid larger transfers are still accepted in full.
        if(b.staging_capacity>1024u*1024){p.memory.release(b.staging);b.staging=0;b.staging_capacity=0;}
        return abi::success;
    }
    U32 scalar(U32 at,U32 value){p.memory.store(at,value,32);return abi::success;}
    U32 method(Object& o,unsigned slot,Args a){
        if(slot==0)return query(o,a);if(slot==1)return ++o.refs;if(slot==2)return release(o,a[0]);
        if(o.kind==Kind::sound || o.kind==Kind::sound8){switch(slot){
            case 3:return create_buffer(a);
            case 4:{if(p.memory.load(a[1],32)!=96)return abi::invalid_parameter;std::array<U32,24> caps{};caps[0]=96;caps[1]=0xf1f;caps[2]=100;caps[3]=200000;caps[4]=1;write(p,a[1],caps);return abi::success;}
            case 5:{p.memory.check(a[2],4,Memory::Write);p.memory.store(a[2],0,32);auto& original=object(a[1]);if(!original.identity->buffer || original.identity->buffer->flags&abi::primary)return abi::invalid_parameter;
                const auto& source=*original.identity->buffer;auto b=std::make_shared<Buffer>(p);b->pcm=mixer.duplicate(source.pcm);b->flags=source.flags;b->format=source.format;auto id=std::make_shared<Identity>();id->buffer=std::move(b);p.memory.store(a[2],wrap(std::move(id),Kind::buffer),32);++buffers;return abi::success;}
            case 6:return a[2]>=1 && a[2]<=4?abi::success:abi::invalid_parameter;
            case 7:return abi::success;case 8:return scalar(a[1],speaker);case 9:speaker=a[1];return abi::success;
            case 10:return abi::already_initialized;case 11:return scalar(a[1],0);
        }}
        if(o.kind==Kind::buffer || o.kind==Kind::buffer8){auto& b=*o.identity->buffer;switch(slot){
            case 3:if(p.memory.load(a[1],32)!=20)return abi::invalid_parameter;write(p,a[1],std::array<U32,5>{20,b.flags,b.pcm->bytes(),0,0});return abi::success;
            case 4:{const auto c=position(b);if(a[1])p.memory.check(a[1],4,Memory::Write);if(a[2])p.memory.check(a[2],4,Memory::Write);if(a[1])p.memory.store(a[1],c.play,32);if(a[2])p.memory.store(a[2],c.write,32);return abi::success;}
            case 5:{if(!a[1] && !a[3])return abi::invalid_parameter;if(a[1] && a[2]<b.format.size())return abi::invalid_parameter;if(a[1])p.memory.check(a[1],b.format.size(),Memory::Write);if(a[3])p.memory.check(a[3],4,Memory::Write);if(a[1])p.memory.copy_in(a[1],b.format);if(a[3])p.memory.store(a[3],U32(b.format.size()),32);return abi::success;}
            case 6:if(!(b.flags&abi::control_volume))return abi::control_unavailable;return scalar(a[1],U32(position(b).volume));
            case 7:if(!(b.flags&abi::control_pan))return abi::control_unavailable;return scalar(a[1],U32(position(b).pan));
            case 8:return scalar(a[1],position(b).frequency);
            case 9:{const auto s=position(b);return scalar(a[1],s.playing?(1u|(s.looping?4u:0u)):0);}
            case 10:return abi::already_initialized;case 11:return lock(o,a);
            case 12:if(a[1] || a[3]&~7u)return abi::invalid_parameter;b.pcm->play((a[3]&abi::looping)!=0);++plays;trace(o,a,"play",0,0,a[3]);return abi::success;
            case 13:if(!b.pcm->seek(a[1]))return abi::invalid_parameter;trace(o,a,"position",a[1]);return abi::success;
            case 14:{if(!(b.flags&abi::primary))return abi::invalid_call;const auto f=wave_format(p,a[1]);if(!f)return abi::bad_format;b.pcm->stop();b.pcm=mixer.create(f->format,32768/f->format.frame_bytes()*f->format.frame_bytes());b.format=f->bytes;return abi::success;}
            case 15:if(!(b.flags&abi::control_volume))return abi::control_unavailable;return b.pcm->volume(std::bit_cast<std::int32_t>(a[1]))?abi::success:abi::invalid_parameter;
            case 16:if(!(b.flags&abi::control_pan))return abi::control_unavailable;return b.pcm->pan(std::bit_cast<std::int32_t>(a[1]))?abi::success:abi::invalid_parameter;
            case 17:if(!(b.flags&abi::control_frequency))return abi::control_unavailable;return b.pcm->frequency(a[1])?abi::success:abi::invalid_parameter;
            case 18:b.pcm->stop();trace(o,a,"stop");return abi::success;case 19:return unlock(o,a);case 20:return abi::success;
            case 21:case 22:case 23:return abi::control_unavailable;
        }}
        if(o.kind==Kind::notify && slot==3){
            if(a[1]>256)return abi::invalid_parameter;std::vector<wa2::audio::Notification> notifications;
            for(U32 n=0;n<a[1];++n){const auto row=read<std::array<U32,2>>(p,a[2]+n*8);const auto it=p.state().events.find(row[1]);if(it==p.state().events.end() || !it->second->signal)return abi::invalid_parameter;
                std::weak_ptr<Win32State::NativeEvent> event=it->second;notifications.push_back({row[0],[event]{if(const auto value=event.lock())value->signal();}});}
            return o.identity->buffer->pcm->notifications(std::move(notifications))?abi::success:abi::invalid_parameter;
        }
        return abi::unsupported;
    }
    static abi::Guid adapter_id(unsigned n){return {0x57413200u+n,0x5344,0x4c32,{0x80,0,0,0,0,0,0,1}};}
    U32 create(Args a){
        if(a[2])return abi::no_aggregation;p.memory.check(a[1],4,Memory::Write);p.memory.store(a[1],0,32);std::string name;
        if(a[0]){const auto id=read<abi::Guid>(p,a[0]);if(id!=abi::default_playback && id!=abi::default_voice && id!=abi::Guid{}){const auto names=output->devices();bool found=false;for(unsigned n=0;n<names.size();++n)if(id==adapter_id(n)){name=names[n];found=true;break;}if(!found)return abi::no_driver;}}
        if(!output->open(name))return abi::no_driver;p.memory.store(a[1],wrap(std::make_shared<Identity>(),Kind::sound8),32);++devices;return abi::success;
    }
    U32 enumerate(Args a){
        p.memory.check(a[0],1,Memory::Execute);const auto scratch=guest_bytes(p,16384);
        struct Cleanup {Process& p;U32 at;~Cleanup(){p.memory.release(at);}} cleanup{p,scratch};
        auto visit=[&](const std::string& name,U32 id){if(name.size()>8192)return true;string(p,scratch+32,name);string(p,scratch+8256,"SDL2");const std::array<U32,4> args{id,scratch+32,scratch+8256,a[1]};++enumerations;return p.callback(a[0],args)!=0;};
        if(!visit("Default audio output",0))return abi::success;const auto names=output->devices();for(unsigned n=0;n<names.size() && n<256;++n){write(p,scratch,adapter_id(n));if(!visit(names[n],scratch))break;}return abi::success;
    }
public:
    explicit Sound(Process& process,const wa2::platform::AudioOutputFactory& factory):p(process),handles(process),output(factory?factory(mixer):std::make_unique<wa2::platform::SdlAudio>(mixer)){if(!output)throw std::invalid_argument("audio output factory returned no output");}
    wa2::audio::Mixer& engine_mixer(){return mixer;}
    bool ensure_output(){return !stopped && output->open({});}
    ~Sound(){shutdown();}
    void shutdown() noexcept {if(stopped)return;stopped=true;output->close();objects.clear();handles.clear();}
    std::string report() const {
        const auto status=output->status();std::uint64_t staging=0;std::vector<const Buffer*> seen;
        for(const auto& [at,o]:objects)if(const auto* b=o.identity->buffer.get();b && std::find(seen.begin(),seen.end(),b)==seen.end()){seen.push_back(b);staging+=b->staging_capacity;}
        std::ostringstream out;out<<"{\"backend\":\"sdl2-native-pcm\",\"devices_created\":"<<devices<<",\"buffers_created\":"<<buffers<<",\"plays\":"<<plays<<",\"uploads\":"<<uploads<<",\"adapter_callbacks\":"<<enumerations<<",\"largest_lock_bytes\":"<<largest_lock<<",\"staging_bytes\":"<<staging<<",\"staging_growths\":"<<staging_growths<<",\"output_rate\":"<<status.rate<<",\"output_frames\":"<<status.frames<<",\"output_callbacks\":"<<status.callbacks<<",\"rendered_frames\":"<<status.rendered_frames<<",\"callback_errors\":"<<status.errors<<",\"driver\":"<<quote(status.driver)<<",\"output_error\":"<<quote(status.error)<<",\"mixer_live_buffers\":"<<mixer.live_buffers()<<",\"buffer_timing\":[";
        bool first=true;const auto emit=[&](U32 at,const AudioBufferTrace& t){if(!first)out<<',';first=false;out<<"{\"object\":"<<at<<",\"lifetime\":"<<t.lifetime<<",\"bytes\":"<<t.bytes<<",\"bytes_per_second\":"<<t.bytes_per_second<<",\"frequency\":"<<t.frequency<<",\"plays\":"<<t.plays<<",\"stops\":"<<t.stops<<",\"uploads\":"<<t.uploads<<",\"uploaded_bytes\":"<<t.uploaded_bytes<<",\"recent\":[";bool next=false;for(const auto& e:t.recent){if(next)out<<',';next=true;out<<"{\"milliseconds\":"<<e.milliseconds<<",\"operation\":\""<<e.operation<<"\",\"thread\":"<<e.thread<<",\"offset\":"<<e.offset<<",\"bytes\":"<<e.bytes<<",\"flags\":"<<e.flags<<",\"play_cursor\":"<<e.play_cursor<<",\"write_cursor\":"<<e.write_cursor<<",\"locked_ms\":"<<e.locked_ms<<",\"sample_hash\":"<<e.sample_hash<<'}';}out<<"]}";};
        for(const auto& [at,t]:retired)emit(at,t);for(const auto& [at,t]:traces)emit(at,t);out<<"],\"interface_handles\":"<<handles.report()<<",\"live_objects\":"<<objects.size()<<",\"active_traces\":"<<traces.size()<<",\"retired_traces\":"<<retired.size()<<'}';return out.str();
    }
    void install(){auto make=[this](Process&,Args a){try{return create(a);}catch(const std::bad_alloc&){return abi::out_of_memory;}};api(p,"dsound.dll","DirectSoundCreate8",3,make);api(p,"dsound.dll","#11",3,make);
        auto list=[this](Process&,Args a){return enumerate(a);};api(p,"dsound.dll","DirectSoundEnumerateA",2,list);api(p,"dsound.dll","#2",2,list);}
};
}}
namespace wr {
SharedAudio install_wa2_audio_service(Process& p,const wa2::platform::AudioOutputFactory& factory){auto sound=std::make_shared<Sound>(p,factory);sound->install();p.state().shutdown.push_back([sound]{sound->shutdown();});p.state().reports["directsound"]=[sound]{return sound->report();};
    return {std::shared_ptr<wa2::audio::Mixer>(sound,&sound->engine_mixer()),[weak=std::weak_ptr<Sound>(sound)]{if(auto state=weak.lock())return state->ensure_output();return false;}};
}
void install_wa2_audio(Process& p,const wa2::platform::AudioOutputFactory& factory){(void)install_wa2_audio_service(p,factory);}
void install_wa2_audio(Process& p){install_wa2_audio(p,{});}
}
