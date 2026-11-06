#include "save_slots.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <stdexcept>

namespace wa2::game {
void SaveSlots::check(unsigned slot){if(slot>=native_save_slots)throw std::out_of_range("Native save slot is out of range");}
SaveSlots::SaveSlots(std::shared_ptr<SaveStorage> storage):storage_(std::move(storage)){if(!storage_)throw std::invalid_argument("Missing native save storage");}
void SaveSlots::write(unsigned slot,std::span<const std::uint8_t> bytes){check(slot);SavedScene::decode(bytes);storage_->replace(slot,bytes);}
std::optional<SavedScene::Bytes> SaveSlots::read(unsigned slot){check(slot);auto value=storage_->read(slot);if(value)SavedScene::decode(*value);return value;}
std::vector<SaveSlotInfo> SaveSlots::list(){auto result=storage_->list();std::array<bool,native_save_slots> seen{};for(auto& item:result){check(item.slot);if(seen[item.slot])throw std::runtime_error("Duplicate native save slot in storage inventory");if(item.diagnostic.empty() && (item.bytes<16 || item.bytes>checkpoint::maximum_bytes))item.diagnostic="Native save size is outside supported bounds";seen[item.slot]=true;}std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.slot<b.slot;});return result;}
}
