#include "save_slots.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <stdexcept>

namespace wa2::game {
void SaveSlots::check(unsigned slot){if(slot>=native_save_slots)throw std::out_of_range("Native save slot is out of range");}
SaveSlots::SaveSlots(std::shared_ptr<SaveStorage> storage):storage_(std::move(storage)){if(!storage_)throw std::invalid_argument("Missing native save storage");}
void SaveSlots::write(unsigned slot,std::span<const std::uint8_t> bytes){check(slot);SavedScene::decode(bytes);std::lock_guard lock(io_);storage_->replace(slot,bytes);}
void SaveSlots::write(unsigned slot,const SaveDocument& document){check(slot);auto bytes=document.encode();std::lock_guard lock(io_);storage_->replace(slot,bytes);}
std::optional<SavedScene::Bytes> SaveSlots::read(unsigned slot){check(slot);std::optional<SavedScene::Bytes> value;{std::lock_guard lock(io_);value=storage_->read(slot);}if(!value)return {};if(SaveDocument::wrapped(*value))return SaveDocument::decode(*value).snapshot;SavedScene::decode(*value);return value;}
std::optional<SavePreview> SaveSlots::preview(unsigned slot){check(slot);std::optional<SavedScene::Bytes> bytes;{std::lock_guard lock(io_);bytes=storage_->prefix(slot,SaveDocument::prefix_bytes);}if(!bytes)return {};if(bytes->size()>SaveDocument::prefix_bytes)throw std::runtime_error("Native save storage exceeded its preview read bound");if(SaveDocument::wrapped(*bytes))return SaveDocument::inspect(*bytes);
    if(bytes->size()<4 || std::string_view(reinterpret_cast<const char*>(bytes->data()),4)!="LSGS")throw std::runtime_error("Unknown native save slot format");SavePreview preview;preview.legacy=true;return preview;
}
std::vector<SaveSlotInfo> SaveSlots::list(){std::vector<SaveSlotInfo> result;{std::lock_guard lock(io_);result=storage_->list();}std::array<bool,native_save_slots> seen{};for(auto& item:result){check(item.slot);if(seen[item.slot])throw std::runtime_error("Duplicate native save slot in storage inventory");if(item.diagnostic.empty() && (item.bytes<16 || item.bytes>checkpoint::maximum_bytes))item.diagnostic="Native save size is outside supported bounds";seen[item.slot]=true;}std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.slot<b.slot;});return result;}
}
