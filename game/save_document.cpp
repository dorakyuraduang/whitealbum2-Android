#include "save_document.hpp"
#include "checkpoint_codec.hpp"
#include <chrono>

namespace wa2::game {
void SaveDate::validate()const{
    using namespace std::chrono;
    if(year<1 || year>9999 || month<1 || month>12 || day<1 || day>31 || !year_month_day(std::chrono::year(year),std::chrono::month(unsigned(month)),std::chrono::day(unsigned(day))).ok() || weekday<0 || weekday>6 || hour<0 || hour>23 || minute<0 || minute>59 || second<0 || second>59)
        throw std::runtime_error("Invalid native save date");
}
void SavePreview::validate()const{
    date.validate();if(legacy || scene.empty() || scene.size()>240 || description.size()>256 || saved_ms>253402300799999ull)throw std::runtime_error("Invalid native save preview metadata");
    for(unsigned char c:scene)if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-'))throw std::runtime_error("Invalid native preview scene name");
    if(description.find('\0')!=std::string::npos)throw std::runtime_error("Invalid native save description");
    if(thumbnail && (thumbnail->width!=128 || thumbnail->height!=72 || thumbnail->rgba.size()!=128u*72*4))throw std::runtime_error("Invalid native save thumbnail");
}
std::vector<std::uint8_t> SavePreview::encode()const{
    validate();checkpoint::Writer out;for(auto value:{date.year,date.month,date.day,date.weekday,date.hour,date.minute,date.second})out.i32(value);
    out.u64(saved_ms);out.string(scene);out.string(description);out.boolean(bool(thumbnail));if(thumbnail)out.blob(thumbnail->rgba);return out.seal("WASP");
}
SavePreview SavePreview::decode(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("WASP",bytes);SavePreview value;for(auto* field:{&value.date.year,&value.date.month,&value.date.day,&value.date.weekday,&value.date.hour,&value.date.minute,&value.date.second})*field=in.i32();
    value.saved_ms=in.u64();value.scene=in.string(240);value.description=in.string(256);if(in.boolean()){const auto rgba=in.blob(128u*72*4);if(rgba.size()!=128u*72*4)throw std::runtime_error("Truncated native save thumbnail");value.thumbnail=std::make_shared<const image::Raster>(image::Raster{128,72,{rgba.begin(),rgba.end()}});}in.finish();value.validate();return value;
}
namespace {
std::pair<SavePreview,std::size_t> preview_record(std::span<const std::uint8_t> bytes){
    if(!SaveDocument::wrapped(bytes))throw std::runtime_error("Missing native save document header");
    checkpoint::Reader header(bytes.subspan(4));if(header.u32()!=1)throw std::runtime_error("Unsupported native save document version");const auto count=header.u32();
    if(count<16 || count>SaveDocument::maximum_preview || bytes.size()<12u+count)throw std::runtime_error("Truncated native save preview record");
    return {SavePreview::decode(bytes.subspan(12,count)),12u+count};
}
}
bool SaveDocument::wrapped(std::span<const std::uint8_t> bytes)noexcept{return bytes.size()>=4 && std::string_view(reinterpret_cast<const char*>(bytes.data()),4)=="WASF";}
SavedScene::Bytes SaveDocument::encode()const{
    const auto scene=SavedScene::decode(snapshot);if(preview.scene!=scene.scene)throw std::runtime_error("Native save preview does not match its scene");
    const auto record=preview.encode();if(record.size()>maximum_preview || snapshot.size()>checkpoint::maximum_bytes-12-record.size())throw std::runtime_error("Native save document exceeds its size limit");
    // The two enclosed envelopes have independent checksums; no redundant
    // whole-file checksum forces a preview read through the scene payload.
    auto result=SavedScene::Bytes{'W','A','S','F',1,0,0,0};result.reserve(12+record.size()+snapshot.size());for(unsigned n=0;n<4;++n)result.push_back(std::uint8_t(record.size()>>(n*8)));result.insert(result.end(),record.begin(),record.end());result.insert(result.end(),snapshot.begin(),snapshot.end());return result;
}
SaveDocument SaveDocument::decode(std::span<const std::uint8_t> bytes){
    if(bytes.size()>checkpoint::maximum_bytes)throw std::runtime_error("Native save document exceeds its size limit");auto [preview,offset]=preview_record(bytes);const auto snapshot=bytes.subspan(offset);
    const auto scene=SavedScene::decode(snapshot);if(preview.scene!=scene.scene)throw std::runtime_error("Native save preview does not match its scene");return {std::move(preview),{snapshot.begin(),snapshot.end()}};
}
SavePreview SaveDocument::inspect(std::span<const std::uint8_t> prefix){return preview_record(prefix).first;}
}
