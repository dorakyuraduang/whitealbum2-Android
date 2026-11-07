#pragma once
#include "../game/user_command.hpp"
#include <atomic>

namespace wa2::platform {
// Only the game thread computes menu policy. Android's UI thread reads this
// immutable-size atomic snapshot; it never reads an Application or its scene.
class NativeUserMenu {
    std::atomic<std::uint64_t> state_{};
public:
    void publish(game::UserMenuState state)noexcept{state_.store(state.packed(),std::memory_order_release);}
    std::uint64_t state()const noexcept{return state_.load(std::memory_order_acquire);}
};
}
