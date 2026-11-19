#pragma once
#include <cstdint>

namespace wa2::graphics {
struct ParticleCommandCounts {
    std::uint64_t submitted{}, tiles{}, continued{};
};
void enable_particle_continuation(bool enabled) noexcept;
bool particle_continuation_enabled() noexcept;
void publish_particle_commands(ParticleCommandCounts frame) noexcept;
ParticleCommandCounts particle_command_counts() noexcept;
}
