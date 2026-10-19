#pragma once
#include "../../engine/movie_reader.hpp"
#include <array>
#include <cstdint>
#include <memory>
namespace wa2::graphics {
struct MovieView {
    std::uint32_t owner{};
    std::array<int,4> source{},destination{};
    bool visible{true};
};
class MoviePresenter {
public:
    virtual ~MoviePresenter()=default;
    virtual void movie_frame(const movie::VideoFrame&,const MovieView&)=0;
    virtual void restore_game()=0;
};
}
