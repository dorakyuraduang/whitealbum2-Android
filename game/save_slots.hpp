#pragma once
#include "saved_scene.hpp"
#include <memory>
#include <optional>

namespace wa2::game {
inline constexpr unsigned native_save_slots=1000;
struct SaveSlotInfo {unsigned slot{};std::uint64_t bytes{},modified_ms{};std::string diagnostic;};
class SaveStorage {
public:
    virtual ~SaveStorage()=default;
    virtual std::optional<SavedScene::Bytes> read(unsigned slot)=0;
    virtual void replace(unsigned slot,std::span<const std::uint8_t>)=0;
    virtual std::vector<SaveSlotInfo> list()=0;
};
// The application supplies native snapshots; no original manual save/settings
// filename can be constructed through this interface.
class SaveSlots {
    std::shared_ptr<SaveStorage> storage_;
    static void check(unsigned slot);
public:
    explicit SaveSlots(std::shared_ptr<SaveStorage>);
    void write(unsigned slot,std::span<const std::uint8_t> snapshot);
    std::optional<SavedScene::Bytes> read(unsigned slot);
    std::vector<SaveSlotInfo> list();
};
}
