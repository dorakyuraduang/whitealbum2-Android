#pragma once
#include <memory>
#include <string>
namespace wr {
class Process;
// Native COM pointers stay behind read-only guest objects/vtables. This is a
// bounded DirectDraw 7 boundary, not arbitrary COM or a Direct3D renderer.
class DirectDrawBackend {
public:
    virtual ~DirectDrawBackend()=default;
    virtual void shutdown() noexcept=0;
    virtual std::string report() const=0;
};
std::unique_ptr<DirectDrawBackend> install_directdraw(Process&);
}
