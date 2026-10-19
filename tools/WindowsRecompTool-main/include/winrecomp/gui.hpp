#pragma once
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <functional>
namespace wr {
class Process;
// OS objects and native pointers stay exclusively in the host backend. Only
// typed 32-bit tokens and explicit legacy structure layouts cross into guest RAM.
class GuiBackend {
public:
    virtual ~GuiBackend()=default;
    virtual void shutdown() noexcept=0;
    virtual std::string report() const=0;
    // Host-only integration boundary; never place this value in guest memory.
    virtual std::uintptr_t native_window(std::uint32_t) {throw std::runtime_error("native window lookup unavailable");}
    virtual std::uint32_t native_window_token(std::uintptr_t) {throw std::runtime_error("native window token unavailable");}
    virtual std::uintptr_t native_callback(std::uint32_t,std::uintptr_t,std::uint32_t,std::uintptr_t,std::intptr_t) {throw std::runtime_error("native callback marshalling unavailable");}
    // A typed source token for a platform-owned modal system dialog. Its
    // control messages are handled by the common shell adapter, not by casting
    // a native dialog pointer into the source address space.
    virtual std::uint32_t attach_system_dialog(std::function<std::uint32_t(std::uint32_t,std::uint32_t,std::uint32_t)>) {throw std::runtime_error("system dialog token unavailable");}
    virtual void detach_system_dialog(std::uint32_t) {throw std::runtime_error("system dialog token unavailable");}
};
// A non-Windows host returns an unavailable backend: it must never pretend that
// a window/context exists. Headless/null rendering is not a fidelity mode.
std::unique_ptr<GuiBackend> install_gui(Process& process);
}
