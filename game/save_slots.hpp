#pragma once
#include "saved_scene.hpp"
#include "save_document.hpp"
#include <memory>
#include <optional>
#include <mutex>

namespace wa2::game {
inline constexpr unsigned native_save_slots=1000;
struct SaveSlotInfo {unsigned slot{};std::uint64_t bytes{},modified_ms{};std::string diagnostic;};
class SaveStorage {
public:
    virtual ~SaveStorage()=default;
    virtual std::optional<SavedScene::Bytes> read(unsigned slot)=0;
    virtual void replace(unsigned slot,std::span<const std::uint8_t>)=0;
    virtual std::vector<SaveSlotInfo> list()=0;
    // Finite test stores can inherit the fallback; private file adapters read
    // at most count bytes and avoid loading the scene when browsing slots.
    virtual std::optional<SavedScene::Bytes> prefix(unsigned slot,std::size_t count){auto bytes=read(slot);if(bytes && bytes->size()>count)bytes->resize(count);return bytes;}
};
// The application supplies native snapshots; no original manual save/settings
// filename can be constructed through this interface.
class SaveSlots {
    std::shared_ptr<SaveStorage> storage_;
    std::mutex io_;
    static void check(unsigned slot);
public:
    explicit SaveSlots(std::shared_ptr<SaveStorage>);
    void write(unsigned slot,std::span<const std::uint8_t> snapshot);
    void write(unsigned slot,const SaveDocument&);
    std::optional<SavedScene::Bytes> read(unsigned slot);
    std::optional<SavePreview> preview(unsigned slot);
    std::vector<SaveSlotInfo> list();
};
}
