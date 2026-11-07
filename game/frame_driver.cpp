#include "frame_driver.hpp"
#include <algorithm>
#include <cmath>

namespace wa2::game {
void FrameInput::add(const Input& input){
    if(input.choice< -1 || input.choice>=8 || input.selection.navigation< -1 || input.selection.navigation>1 ||
        input.page_delta< -1 || input.page_delta>1 || input.scroll< -1 || input.scroll>1 || !std::isfinite(input.selection.x) || !std::isfinite(input.selection.y))throw std::invalid_argument("Invalid native frame input");
    if(input.commands.size()>maximum_user_commands || edges_.commands.size()>maximum_user_commands-input.commands.size())throw std::invalid_argument("Native user-command queue exceeds its limit");
    for(auto command:input.commands)if(!valid(command))throw std::invalid_argument("Unknown native user command");
    held_=input.continued();
    edges_.commands.insert(edges_.commands.end(),input.commands.begin(),input.commands.end());
    edges_.advance|=input.advance;edges_.toggle_window^=input.toggle_window;edges_.cancel|=input.cancel;
    edges_.selection.confirm|=input.selection.confirm;
    edges_.selection.navigation=std::clamp(edges_.selection.navigation+input.selection.navigation,-1,1);
    edges_.page_delta=std::clamp(edges_.page_delta+input.page_delta,-1,1);
    edges_.scroll=std::clamp(edges_.scroll+input.scroll,-1,1);
    if(edges_.choice<0 && input.choice>=0)edges_.choice=input.choice;
    if(input.selection.activate && !edges_.selection.activate){
        edges_.selection.activate=true;edges_.selection.pointer=input.selection.pointer;
        edges_.selection.x=input.selection.x;edges_.selection.y=input.selection.y;
    }
}
Input FrameInput::take(){
    auto next=held_;next.advance=edges_.advance;next.toggle_window=edges_.toggle_window;next.cancel=edges_.cancel;next.choice=edges_.choice;
    next.selection.confirm=edges_.selection.confirm;next.selection.navigation=edges_.selection.navigation;
    next.page_delta=edges_.page_delta;
    next.scroll=edges_.scroll;
    next.commands=std::move(edges_.commands);
    if(edges_.selection.activate){next.selection.activate=true;next.selection.pointer=edges_.selection.pointer;next.selection.x=edges_.selection.x;next.selection.y=edges_.selection.y;}
    edges_={};return next;
}
void DriverSettings::validate()const{
    if(presentation_hz<1 || presentation_hz>240 || maximum_catch_up<1 || maximum_catch_up>8 ||
        skip.microseconds<0 || skip.microseconds>50000 || !skip.maximum_ticks || skip.maximum_ticks>4096)
        throw std::invalid_argument("Native frame driver settings are out of range");
}
FrameDriver::FrameDriver(Application& application,DriverSettings settings):application_(application),settings_(settings){settings_.validate();}
namespace {
constexpr std::uint64_t period=1000000;
std::uint64_t advance(std::uint64_t& phase,std::int64_t elapsed,unsigned rate){
    // Splitting seconds avoids elapsed*rate overflow near INT64_MAX.
    const auto seconds=std::uint64_t(elapsed)/period;
    phase+=(std::uint64_t(elapsed)%period)*rate;
    const auto due=seconds*rate+phase/period;phase%=period;return due;
}
std::int64_t remaining(std::uint64_t phase,unsigned rate){return std::int64_t((period-phase+rate-1)/rate);}
}
DriverFrame FrameDriver::pump(std::int64_t now,DriverInput input,const std::function<std::int64_t()>& clock){
    if(now<0 || (started_ && now<observed_))throw std::invalid_argument("Native frame driver clock moved backwards");
    if(!clock)throw std::invalid_argument("Missing native frame driver budget clock");
    DriverFrame result;
    if(application_.page()==Page::closed){pending_.clear();result.closed=true;return result;}
    if(input.quit){application_.close(now);pending_.clear();result.closed=true;return result;}
    // Validate input before advancing clocks or changing application state.
    auto pending=pending_;pending.add(input.game);
    const bool first=!started_,resume=started_ && suspended_ && !input.background,recover=clock_revision_!=application_.clock_revision();
    clock_revision_=application_.clock_revision();const auto elapsed=first || recover?0:now-observed_;observed_=now;started_=true;
    if(application_.background()!=input.background)application_.background(input.background,now);
    suspended_=input.background;
    if(suspended_){pending_=std::move(pending);pending_.suspend();logic_phase_=present_phase_=0;result.suspended=true;result.sleep_us=16000;return result;}
    if(first || resume || recover){pending_.clear();if(first)pending_=pending;else if(resume && !recover){pending_=std::move(pending);pending_.suspend();}if(recover)logic_phase_=present_phase_=0;}
    else pending_=pending;
    auto due=first || resume || recover?1:advance(logic_phase_,elapsed,60);
    result.draw=first || resume || recover || advance(present_phase_,elapsed,settings_.presentation_hz)!=0;
    if(due>settings_.maximum_catch_up){result.dropped_updates=due-settings_.maximum_catch_up;due=settings_.maximum_catch_up;}
    if(due){
        auto previous=clock();if(previous<0)throw std::invalid_argument("Native frame driver budget clock is negative");const auto start=previous;
        const auto budget_clock=[&]{const auto value=clock();if(value<previous)throw std::invalid_argument("Native frame driver budget clock moved backwards");previous=value;return value;};
        auto game_input=pending_.take();
        for(std::uint64_t n=0;n<due;++n){
            // Only the final due update may spend the remaining shared skip
            // budget. A slow host frame cannot multiply it by eight.
            FrameBudget budget{0,1};
            if(n+1==due){
                const auto spent=budget_clock()-start;budget.microseconds=std::max<std::int64_t>(0,settings_.skip.microseconds-spent);
                budget.maximum_ticks=result.batch.ticks<settings_.skip.maximum_ticks?settings_.skip.maximum_ticks-result.batch.ticks:1;
            }
            const auto batch=application_.frame(now,game_input,budget_clock,budget);++result.updates;
            result.batch.state=batch.state;result.batch.ticks+=batch.ticks;result.batch.limit=batch.limit;game_input=game_input.continued();
            if(application_.page()==Page::closed){pending_.clear();result.closed=true;result.draw=false;break;}
            if(clock_revision_!=application_.clock_revision()){pending_.clear();logic_phase_=present_phase_=0;break;}
        }
    }
    if(!result.closed)result.sleep_us=std::min(remaining(logic_phase_,60),remaining(present_phase_,settings_.presentation_hz));
    return result;
}
}
