#include "legacy_slots.hpp"
#include "checkpoint_codec.hpp"
#include "legacy_save.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace wa2::game {
namespace {
class LegacySlots final:public SaveStorage {
    std::shared_ptr<SaveStorage> native_;
    LegacySaveSources sources_;
    bool combined_,indexed_{};
    std::map<unsigned,LegacySaveFile> files_;
    void refresh(){
        auto files=sources_.list();if(files.size()>404)throw resource::Error("Original manual-save inventory exceeds four bounded directories");
        std::array<std::map<unsigned,LegacySaveFile>,4> directories;
        for(auto& file:files){const auto path=resource::legacy_save_path(file.name);if(!path)throw resource::Error("Unknown original manual-save path");if(!directories[path->directory].emplace(path->slot,std::move(file)).second)throw resource::Error("Ambiguous original manual-save slot");}
        const auto primary=!directories[0].empty()?0u:combined_?1u:2u;auto next=std::move(directories[primary]);
        // The original load menu's alternate bank is the separate IC directory.
        // Its quick slot has no alternate-bank control in the shared save view.
        if(combined_)for(auto& [slot,file]:directories[3])if(slot<100)next.emplace(200+slot,std::move(file));
        files_=std::move(next);indexed_=true;
    }
    std::shared_ptr<resource::Source> original(unsigned slot){
        if(slot>=native_save_slots)throw std::out_of_range("Original native slot is out of range");if(!indexed_)refresh();const auto found=files_.find(slot);if(found==files_.end())return {};
        if(!found->second.diagnostic.empty())throw resource::Error(found->second.diagnostic);auto source=sources_.open(found->second.name);if(!source)throw resource::Error("Original manual save disappeared after inventory");
        if(source->size()<88 || source->size()>checkpoint::maximum_bytes)throw resource::Error("Original manual-save size is outside supported bounds");return source;
    }
public:
    LegacySlots(std::shared_ptr<SaveStorage> native,LegacySaveSources sources,bool combined):native_(std::move(native)),sources_(std::move(sources)),combined_(combined){}
    std::optional<SavedScene::Bytes> read(unsigned slot)override{
        if(auto value=native_->read(slot))return value;auto source=original(slot);if(!source)return {};SavedScene::Bytes bytes(static_cast<std::size_t>(source->size()));source->read_at(0,bytes);return bytes;
    }
    std::optional<SavedScene::Bytes> prefix(unsigned slot,std::size_t count)override{
        if(count>SaveDocument::prefix_bytes)throw resource::Error("Original preview request exceeds its read bound");if(auto value=native_->prefix(slot,count))return value;auto source=original(slot);if(!source)return {};count=std::min(count,original_save_header_bytes+65536);SavedScene::Bytes bytes(std::size_t(std::min<std::uint64_t>(source->size(),count)));if(!bytes.empty())source->read_at(0,bytes);return bytes;
    }
    void replace(unsigned slot,std::span<const std::uint8_t> bytes)override{native_->replace(slot,bytes);}
    std::vector<SaveSlotInfo> list()override{
        auto result=native_->list();std::set<unsigned> present;for(const auto& item:result)if(item.slot>=native_save_slots || !present.insert(item.slot).second)throw resource::Error("Invalid native save inventory over original slots");refresh();
        for(const auto& [slot,file]:files_)if(!present.contains(slot)){SaveSlotInfo info{slot,file.bytes,file.modified_ms,file.diagnostic};if(info.diagnostic.empty() && (info.bytes<88 || info.bytes>checkpoint::maximum_bytes))info.diagnostic="Original manual-save size is outside supported bounds";result.push_back(std::move(info));}return result;
    }
};
}
std::shared_ptr<SaveStorage> legacy_save_storage(std::shared_ptr<SaveStorage> native,LegacySaveSources sources,bool combined){
    if(!native)throw std::invalid_argument("Missing primary native save storage");if(!sources.list && !sources.open)return native;if(!sources.list || !sources.open)throw std::invalid_argument("Incomplete original save directory services");return std::make_shared<LegacySlots>(std::move(native),std::move(sources),combined);
}
}
