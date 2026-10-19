#pragma once
#include <SDL_events.h>
#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <optional>
#include <thread>
#include <utility>

namespace wa2::platform {
struct InputSnapshot {
    std::bitset<SDL_NUM_SCANCODES> keys,pressed_keys,released_keys;
    std::bitset<5> buttons,pressed_buttons,released_buttons;
    float x{},y{},wheel_x{},wheel_y{};
    bool quit{},focused{true},background{},minimized{},renderer_reset{};
};
class SdlInput {
public:
    using Coordinates=std::function<std::pair<float,float>(float,float)>;
    explicit SdlInput(std::uint32_t window_id=0,Coordinates coordinates={});
    void window_size(int width,int height);
    // SDL's logical renderer has already transformed mouse and normalized
    // touch events. Keep their touch extent independent of physical resizes.
    void logical_size(int width,int height);
    void begin_frame();
    void consume(const SDL_Event& event);
    // Polls only this application's SDL event queue. Tests feed owned event
    // structs to consume() and never poll or inject operating-system input.
    void poll();
    const InputSnapshot& snapshot() const noexcept {return snapshot_;}
private:
    const std::thread::id owner_{std::this_thread::get_id()};
    std::uint32_t window_id_{};
    Coordinates coordinates_;
    InputSnapshot snapshot_;
    std::array<bool,5> mouse_buttons_{};
    std::optional<std::pair<SDL_TouchID,SDL_FingerID>> finger_;
    int width_{1},height_{1};
    bool logical_extent_{};
    void enter() const;
    bool accepts(std::uint32_t id) const noexcept;
    void position(float x,float y);
    void button(unsigned index);
    void release_all();
};
}
