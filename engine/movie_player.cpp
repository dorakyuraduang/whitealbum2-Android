#include "movie_player.hpp"
#include "pcm_stream.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace wa2::movie{
struct Player::Impl{
    std::shared_ptr<audio::Mixer> mixer;
    std::shared_ptr<audio::PcmStream> audio;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    std::atomic<bool> stopping{};
    std::deque<VideoFrame> frames;
    std::optional<VideoFrame> current;
    std::exception_ptr failure;
    ReaderStatistics reader_stats;
    std::size_t peak_frames{};
    std::uint64_t skipped{};
    unsigned audio_rate{};
    float amplitude{};
    bool opened{},has_audio{},got_audio{},eof{},started{},audio_started{},audio_ended{},paused{},ended{};
    std::int64_t audio_time{},start_time{},last_time{-1},elapsed{},pause_time{},tail_time{},tail_clock{};
    ~Impl(){stop();}
    void stop(){
        stopping.store(true,std::memory_order_release);changed.notify_all();
        if(worker.joinable())worker.join();
        std::lock_guard lock(mutex);if(audio)audio->stop();frames.clear();current.reset();ended=true;
    }
    void rethrow()const{if(failure)std::rethrow_exception(failure);}
    static ReaderStatistics combined(const Reader& video,const std::unique_ptr<Reader>& audio){
        auto out=video.statistics();if(!audio)return out;const auto other=audio->statistics();
        out.bytes_read+=other.bytes_read;out.read_calls+=other.read_calls;out.seek_calls+=other.seek_calls;
        out.packets+=other.packets;out.video_frames+=other.video_frames;out.audio_frames+=other.audio_frames;
        out.largest_read=std::max(out.largest_read,other.largest_read);
        out.queued_packet_bytes+=other.queued_packet_bytes;out.peak_packet_bytes+=other.peak_packet_bytes;out.peak_packet_count+=other.peak_packet_count;
        return out;
    }
    void decode(std::shared_ptr<resource::Source> source,std::function<void()> initializer){
        try{
            if(initializer)initializer();const auto cancelled=[this]{return stopping.load(std::memory_order_acquire);};
            Reader video_reader(source,cancelled,Track::video);const auto info=video_reader.information();
            std::unique_ptr<Reader> audio_reader;
            if(info.audio_rate)audio_reader=std::make_unique<Reader>(source,cancelled,Track::audio);
            {
                std::lock_guard lock(mutex);has_audio=info.audio_rate!=0;audio_rate=info.audio_rate;
                if(has_audio){audio=mixer->create_stream(info.audio_rate,info.audio_channels,16384);audio->gain(amplitude);}opened=true;
            }
            bool audio_eof=!info.audio_rate,video_eof=false;
            std::optional<AudioFrame> pending_audio;std::size_t audio_cursor{};
            while(!stopping.load(std::memory_order_acquire)){
                // A WMA superframe can contain more PCM than the output ring.
                // Write what fits, then still decode video while it drains.
                if(pending_audio){
                    audio_cursor+=audio->write(std::span<const std::uint8_t>(pending_audio->pcm16).subspan(audio_cursor));
                    if(audio_cursor==pending_audio->pcm16.size()){pending_audio.reset();audio_cursor=0;}
                }
                Track preference=Track::video;
                {
                    std::unique_lock lock(mutex);
                    if(!pending_audio && !audio_eof && (video_eof || frames.size()==4 || audio->status().queued_frames<8192))preference=Track::audio;
                    if(preference==Track::video){
                        if(video_eof || (pending_audio && frames.size()==4)){
                            changed.wait_for(lock,std::chrono::milliseconds(2),[this]{return stopping.load(std::memory_order_acquire);});continue;
                        }
                        changed.wait(lock,[this]{return stopping.load(std::memory_order_acquire) || frames.size()<4;});
                        if(stopping.load(std::memory_order_acquire))return;
                    }
                }
                // Independent bounded cursors share the original Source.
                // Asking for audio beyond its final packet must not retain
                // the entire remaining video as a compressed backlog.
                auto decoded=preference==Track::audio?audio_reader->next(Track::audio):video_reader.next(Track::video);
                {std::lock_guard lock(mutex);reader_stats=combined(video_reader,audio_reader);}
                if(!decoded){
                    std::lock_guard lock(mutex);
                    if(preference==Track::audio){audio_eof=true;if(audio)audio->finish_input();}else video_eof=true;
                    if(audio_eof && video_eof){eof=true;changed.notify_all();return;}
                    continue;
                }
                if(auto* video=std::get_if<VideoFrame>(&*decoded)){
                    std::unique_lock lock(mutex);changed.wait(lock,[this]{return stopping.load(std::memory_order_acquire) || frames.size()<4;});
                    if(stopping.load(std::memory_order_acquire))return;
                    frames.push_back(std::move(*video));peak_frames=std::max(peak_frames,frames.size());changed.notify_all();
                }else{
                    auto& samples=std::get<AudioFrame>(*decoded);
                    {std::lock_guard lock(mutex);if(!got_audio){audio_time=samples.time_us;got_audio=true;}}
                    if(!audio)throw Error("movie decoder produced undeclared audio");pending_audio=std::move(samples);audio_cursor=0;
                }
            }
        }catch(...){
            if(stopping.load(std::memory_order_acquire))return;
            std::lock_guard lock(mutex);failure=std::current_exception();eof=true;if(audio)audio->stop();changed.notify_all();
        }
    }
    bool ready()const{
        return opened && !frames.empty() && (!has_audio || got_audio || eof || frames.size()==4);
    }
    void update(std::int64_t now){
        std::lock_guard lock(mutex);rethrow();
        if(now<0 || (last_time>=0 && now<last_time))throw Error("movie clock moved backwards");last_time=now;
        if(ended || paused)return;
        if(!started){if(!ready()){if(eof && frames.empty())ended=true;return;}started=true;start_time=now;}
        auto clock=now-start_time;
        if(audio && got_audio){
            auto status=audio->status();
            if(!audio_started && clock>=audio_time){audio->play();audio_started=true;status=audio->status();}
            if(audio_started){
                const auto consumed=audio_time+std::int64_t(status.consumed_frames)*1000000/audio_rate;
                if(status.finished){
                    if(!audio_ended){audio_ended=true;tail_time=now;tail_clock=consumed;}
                    clock=tail_clock+(now-tail_time);
                }else clock=consumed;
            }
        }
        // Once audio starts it is the clock, including temporary starvation.
        // Never run video backwards when an initial audio packet arrives late.
        elapsed=std::max(elapsed,clock);
        unsigned selected=0;
        while(!frames.empty() && frames.front().time_us<=elapsed){current=std::move(frames.front());frames.pop_front();++selected;}
        if(selected>1)skipped+=selected-1;changed.notify_all();
        const auto end=current?current->time_us+current->duration_us:0;
        if(eof && frames.empty() && (!audio || audio->status().finished) && elapsed>=end)ended=true;
    }
};
Player::Player(std::shared_ptr<resource::Source> source,std::shared_ptr<audio::Mixer> mixer,std::function<void()> initializer)
    :impl_(std::make_unique<Impl>()){
    if(!source || !mixer)throw Error("missing native movie source or mixer");impl_->mixer=std::move(mixer);
    impl_->worker=std::thread([state=impl_.get(),source=std::move(source),initializer=std::move(initializer)]()mutable{
        state->decode(std::move(source),std::move(initializer));});
}
Player::~Player()=default;
void Player::update(std::int64_t now){impl_->update(now);}
void Player::pause(bool value,std::int64_t now){
    impl_->update(now);std::lock_guard lock(impl_->mutex);impl_->rethrow();
    if(value==impl_->paused || impl_->ended)return;
    if(value){impl_->paused=true;impl_->pause_time=now;if(impl_->audio)impl_->audio->pause();}
    else{
        if(impl_->started)impl_->start_time+=now-impl_->pause_time;
        if(impl_->audio_ended)impl_->tail_time+=now-impl_->pause_time;
        impl_->paused=false;if(impl_->audio_started && impl_->audio)impl_->audio->play();
    }
}
void Player::gain(float value){
    if(!std::isfinite(value) || value<0 || value>1)throw Error("invalid native movie gain");
    std::lock_guard lock(impl_->mutex);impl_->amplitude=value;if(impl_->audio)impl_->audio->gain(value);
}
std::optional<VideoFrame> Player::frame()const{std::lock_guard lock(impl_->mutex);impl_->rethrow();return impl_->current;}
bool Player::finished()const{std::lock_guard lock(impl_->mutex);impl_->rethrow();return impl_->ended;}
void Player::stop(){impl_->stop();}
PlayerStatistics Player::statistics()const{
    std::lock_guard lock(impl_->mutex);impl_->rethrow();const auto audio=impl_->audio?impl_->audio->status():audio::StreamStatus{};
    return {impl_->reader_stats,impl_->frames.size(),impl_->peak_frames,audio.queued_frames,audio.underruns,impl_->skipped,
        impl_->ready() || impl_->started,impl_->started,impl_->eof,impl_->ended};
}
}
