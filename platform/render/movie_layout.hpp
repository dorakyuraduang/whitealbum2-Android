#pragma once
#include "movie_presenter.hpp"
#include "surface_host.hpp"
#include <optional>
namespace wa2::graphics {
struct MovieLayout {
    // Top-left drawable x/y/width/height and normalized crop u0/v0/u1/v1.
    std::array<double,4> destination{};
    std::array<float,4> texture{};
};
std::optional<MovieLayout> layout_movie(const movie::VideoFrame&,const MovieView&,const ClientViewport&);
}
