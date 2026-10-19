#include "stream_voice.hpp"
#include "ogg_reader.hpp"
#include "wave_reader.hpp"
#include "pcm_stream.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <thread>

namespace wa2::audio {
namespace {
template<class Reader>class StreamVoice final : public Voice {
    struct State {
        mutable std::mutex mutex;
        std::condition_variable changed;
        std::shared_ptr<PcmStream> pcm;
        std::string error;
        std::atomic<bool> ready{};
        std::atomic<bool> stop{};
        std::uint8_t volume{};
        bool paused{};
    };
    std::shared_ptr<State> state_{std::make_shared<State>()};
    // Declared last: the worker stops before state is released.
    std::thread worker_;
public:
    StreamVoice(Mixer& mixer,OggPlaylist playlist,std::function<void()> initialize,bool paused){
        if(!playlist.first)throw resource::Error("missing audio playlist resource");
        auto reader=std::make_unique<Reader>(playlist.first);
        const auto format=reader->format();
        auto pcm=mixer.create_stream(format.rate,format.channels);pcm->gain(0);state_->pcm=pcm;state_->paused=paused;
        worker_=std::thread([state=state_,pcm,reader=std::move(reader),repeat_source=std::move(playlist.repeat),format,
                             initialize=std::move(initialize)]()mutable{
            try{
                if(initialize)initialize();
                std::array<std::uint8_t,8192> output{};
                std::size_t begin=0,end=0,decoded=0;bool repeat=false,done=false;
                while(!state->stop.load(std::memory_order_acquire)){
                    const auto status=pcm->status();if(status.finished)break;
                    if(begin<end){begin+=pcm->write(std::span(output).subspan(begin,end-begin));}
                    else if(!done){
                        begin=0;end=reader->read(output);decoded+=end;
                        if(!end && reader->finished()){
                            if(!decoded)throw resource::Error("audio resource contains no PCM frames");
                            if(repeat_source){
                                if(repeat)reader->rewind();
                                else {reader=std::make_unique<Reader>(repeat_source);repeat=true;
                                    if(reader->format()!=format)throw VorbisError("music intro/loop formats differ");}
                                decoded=0;
                            }else done=true;
                        }
                    }
                    const auto now=pcm->status();
                    if(!state->ready.load(std::memory_order_relaxed) && (now.queued_frames>=2048 || (done && begin==end))){
                        std::lock_guard lock(state->mutex);if(!state->paused)pcm->play();state->ready.store(true,std::memory_order_release);
                    }
                    if(done && begin==end){pcm->finish_input();break;}
                    if(now.free_frames==0){std::unique_lock lock(state->mutex);
                        state->changed.wait_for(lock,std::chrono::milliseconds(2),[&]{return state->stop.load(std::memory_order_acquire);});}
                }
            }catch(const std::exception& error){
                if(!state->stop.load(std::memory_order_acquire)){std::lock_guard lock(state->mutex);state->error=error.what();}
                pcm->stop();
            }catch(...){
                if(!state->stop.load(std::memory_order_acquire)){std::lock_guard lock(state->mutex);state->error="native streamed audio playback failed";}
                pcm->stop();
            }
        });
    }
    ~StreamVoice()override{stop();}
    bool ready()const override{return state_->ready.load(std::memory_order_acquire);}
    bool finished()const override{
        std::lock_guard lock(state_->mutex);return state_->pcm?state_->pcm->status().finished:!state_->error.empty();
    }
    std::string failure()const override{std::lock_guard lock(state_->mutex);return state_->error;}
    void gain(std::uint8_t volume)override{
        std::lock_guard lock(state_->mutex);state_->volume=volume;if(state_->pcm)state_->pcm->gain(volume/255.f);
    }
    void pause(bool value)override{
        std::lock_guard lock(state_->mutex);state_->paused=value;
        if(value)state_->pcm->pause();else if(state_->ready.load(std::memory_order_acquire))state_->pcm->play();
    }
    void stop()override{
        state_->stop.store(true,std::memory_order_release);state_->changed.notify_all();std::shared_ptr<PcmStream> pcm;
        {std::lock_guard lock(state_->mutex);pcm=state_->pcm;}
        if(pcm)pcm->stop();if(worker_.joinable())worker_.join();
    }
};
}
std::unique_ptr<Voice> play_ogg(Mixer& mixer,OggPlaylist playlist,std::function<void()> initialize,bool paused){
    return std::make_unique<StreamVoice<OggReader>>(mixer,std::move(playlist),std::move(initialize),paused);
}
std::unique_ptr<Voice> play_wave(Mixer& mixer,std::shared_ptr<resource::Source> source,bool loop,std::function<void()> initialize,bool paused){
    return std::make_unique<StreamVoice<WaveReader>>(mixer,OggPlaylist{source,loop?source:nullptr},std::move(initialize),paused);
}
OggPlaylist music_playlist(const resource::Archive& archive,std::int32_t id,bool loop){
    if(id<0 || id>=200)throw VorbisError("invalid music resource number");
    std::array<char,32> name{};std::snprintf(name.data(),name.size(),"BGM_%03d.ogg",int(id));
    auto index=archive.find(name.data());
    if(index!=resource::Archive::missing){auto source=archive.open_member(index);return {source,loop?source:nullptr};}
    std::snprintf(name.data(),name.size(),"BGM_%03d_a.ogg",int(id));index=archive.find(name.data());
    if(index==resource::Archive::missing)throw VorbisError("missing music intro resource");
    OggPlaylist result{archive.open_member(index),{}};
    if(loop){
        std::snprintf(name.data(),name.size(),"BGM_%03d_b.ogg",int(id));index=archive.find(name.data());
        if(index==resource::Archive::missing)throw VorbisError("missing music loop resource");
        result.repeat=archive.open_member(index);
    }
    return result;
}
}
