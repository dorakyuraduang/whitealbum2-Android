#include "pak.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>

namespace wa2::paktool {
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint64_t word_limit = 0xffffffffull;
constexpr std::size_t member_limit = 1000000;
struct Entry {
    bool directory{};
    std::string name;
    Bytes record;
    fs::path local;
    std::uint32_t flags{};
    std::uint64_t offset{}, stored{}, expanded{};
};
struct Index {
    Format format = Format::kcap;
    unsigned codepage = 932;
    Bytes header;
    std::vector<Entry> entries;
};
void check(const Options& options) { if(options.cancel && options.cancel->load()) throw Cancelled(); }
void progress(const Options& options, unsigned percent, std::string_view text) {
    check(options);
    if(options.progress) options.progress(std::min(percent, 100u), text);
}
std::uint32_t u32(const std::uint8_t* data) {
    return std::uint32_t(data[0]) | std::uint32_t(data[1]) << 8 | std::uint32_t(data[2]) << 16 | std::uint32_t(data[3]) << 24;
}
void word(Bytes& data, unsigned at, std::uint64_t value) {
    if(value > word_limit || at + 4 > data.size()) throw Error("PAK 超过格式的 4 GiB 大小或索引限制");
    for(unsigned i = 0; i < 4; ++i) data[at + i] = std::uint8_t(value >> (8 * i));
}
void read(std::istream& input, std::span<std::uint8_t> bytes) {
    if(!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        throw Error("文件读取不完整，可能已损坏或被其他程序修改");
}
void write(std::ostream& output, std::span<const std::uint8_t> bytes) {
    if(!bytes.empty()) output.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if(!output) throw Error("写入失败，请检查剩余空间和文件夹权限");
}
void seek(std::istream& input, std::uint64_t offset) {
    input.clear(); input.seekg(std::streamoff(offset));
    if(!input) throw Error("无法定位 PAK 数据");
}
std::ifstream open_input(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if(!input) throw Error("无法读取文件：" + utf8(path));
    return input;
}
void finish(std::ofstream& output) {
    output.flush();
    if(!output) throw Error("写入失败，请检查剩余空间");
    output.close();
    if(output.fail()) throw Error("关闭输出文件失败");
}
void copy(std::istream& input, std::ostream& output, std::uint64_t size, const Options& options) {
    std::array<std::uint8_t, 256 * 1024> buffer;
    while(size) {
        check(options);
        const auto count = std::size_t(std::min<std::uint64_t>(buffer.size(), size));
        read(input, std::span(buffer).first(count)); write(output, std::span(buffer).first(count)); size -= count;
    }
}
bool within(const fs::path& parent, const fs::path& child) {
    const auto a = path_key(parent), b = path_key(child);
    return a == b || (b.size() > a.size() && b.compare(0, a.size(), a) == 0 && (a.back() == '/' || b[a.size()] == '/'));
}
void no_links(const fs::path& path) {
    if(is_link(path)) throw Error("不处理符号链接或目录联接：" + utf8(path));
}
// An exclusively-created directory owns every temporary file. Cleanup is only
// allowed after checking its absolute parent and excluding reparse points.
class Stage {
    fs::path parent_, path_;
public:
    explicit Stage(const fs::path& parent) : parent_(fs::canonical(parent)) {
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned n = 0; n < 100; ++n) {
            const auto leaf = ".wa2pak-work-" + std::to_string(seed) + "-" + std::to_string(n);
            path_ = parent_ / leaf;
            if(fs::create_directory(path_)) return;
        }
        throw Error("无法创建临时工作目录");
    }
    Stage(const Stage&) = delete;
    ~Stage() {
        try {
            if(!path_.empty() && path_.is_absolute() && path_.parent_path() == parent_ &&
               path_.filename().wstring().starts_with(L".wa2pak-work-") && !is_link(path_) &&
               fs::exists(path_) && fs::canonical(path_) == path_) {
                std::error_code error; fs::remove_all(path_, error);
            }
        } catch(...) {}
    }
    const fs::path& path() const { return path_; }
};
fs::path destination_path(const fs::path& destination) {
    if(destination.filename().empty() || destination.filename() == "." || destination.filename() == "..")
        throw Error("目标路径无效");
    auto result = fs::canonical(fs::absolute(destination).parent_path()) / destination.filename();
    if(fs::exists(result) || is_link(result)) throw Error("目标已存在，请选择新的文件或文件夹名称：" + utf8(result));
    return result;
}
std::string hex(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result; result.reserve(bytes.size() * 2);
    for(const unsigned char value : bytes) { result += digits[value >> 4]; result += digits[value & 15]; }
    return result;
}
std::string hex(const Bytes& bytes) { return hex({reinterpret_cast<const char*>(bytes.data()), bytes.size()}); }
std::string unhex(std::string_view text) {
    if(text.size() % 2) throw Error("PAK 清单格式错误");
    const auto nibble = [](char c) -> unsigned {
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw Error("PAK 清单包含无效十六进制数据");
    };
    std::string result; result.reserve(text.size() / 2);
    for(std::size_t i = 0; i < text.size(); i += 2) result += char((nibble(text[i]) << 4) | nibble(text[i+1]));
    return result;
}
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> parts;
    std::size_t at = 0;
    for(;;) {
        const auto end = line.find('\t', at);
        parts.push_back(line.substr(at, end - at));
        if(end == std::string::npos) return parts;
        at = end + 1;
    }
}
void validate_codepage(unsigned cp) {
    if(cp != 932 && cp != 936 && cp != 65001) throw Error("文件名编码仅支持 Shift-JIS、GBK 和 UTF-8");
}
// Check traversal AFTER decoding: a Shift-JIS trail byte may be 0x5c.
fs::path safe_relative(std::string text, bool repair = false) {
    if(text.empty() || text.find('\0') != std::string::npos) throw Error("PAK 包含空文件名或 NUL 字符");
    std::replace(text.begin(), text.end(), '\\', '/');
    if(text.front() == '/' || text.find(':') != std::string::npos) throw Error("PAK 包含绝对路径或驱动器路径");
    fs::path result;
    std::istringstream stream(text); std::string part;
    while(std::getline(stream, part, '/')) {
        if(part.empty() || part == "." || part == "..") throw Error("PAK 包含不安全的相对路径");
        for(char& c : part) {
            if(static_cast<unsigned char>(c) < 32 || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') {
                if(!repair) throw Error("PAK 清单或文件名包含不支持的字符");
                c = '_';
            }
        }
        if(part.back() == '.' || part.back() == ' ') {
            if(!repair) throw Error("文件名末尾不能是空格或点");
            while(!part.empty() && (part.back() == '.' || part.back() == ' ')) part.pop_back();
            part += '_';
        }
        auto base = part.substr(0, part.find('.'));
        for(char& c : base) if(c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if(base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
           (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3] >= '1' && base[3] <= '9')) {
            if(!repair) throw Error("文件名使用了 Windows 保留名称");
            part = "_" + part;
        }
        // Also validate the UTF-8 representation even for a hand-edited manifest.
        encode_name(part, 65001);
        result /= fs::u8path(part);
    }
    if(result.empty() || result.has_root_path()) throw Error("无效的相对路径");
    return result;
}
fs::path with_suffix(const fs::path& path, unsigned index) {
    return path.parent_path() / fs::u8path(utf8(path.stem()) + "~" + std::to_string(index) + utf8(path.extension()));
}
// Map collisions to distinct paths; the manifest retains the original names.
class Names {
    std::map<std::string, bool> paths_{{path_key(fs::u8path(manifest_name)), false}};
public:
    fs::path reserve(const fs::path& wanted, bool directory, unsigned index) {
        fs::path result;
        std::vector<fs::path> parts(wanted.begin(), wanted.end());
        for(std::size_t i = 0; i < parts.size(); ++i) {
            const bool dir = i + 1 < parts.size() || directory;
            auto next = result / parts[i]; unsigned attempt = index + 1;
            for(;;) {
                const auto found = paths_.find(path_key(next));
                if(found == paths_.end() || (dir && found->second)) break;
                next = with_suffix(result / parts[i], attempt++);
            }
            paths_[path_key(next)] = dir; result = next;
        }
        return result;
    }
};
std::string movie_extension(std::span<const std::uint8_t> h) {
    const auto starts = [&](std::initializer_list<unsigned> bytes) {
        return h.size() >= bytes.size() && std::equal(bytes.begin(), bytes.end(), h.begin());
    };
    if(starts({0x30,0x26,0xb2,0x75,0x8e,0x66,0xcf,0x11,0xa6,0xd9,0x00,0xaa,0x00,0x62,0xce,0x6c})) return ".wmv";
    if(h.size() >= 12 && std::equal(h.begin()+4, h.begin()+8, "ftyp")) return ".mp4";
    if(h.size() >= 12 && std::equal(h.begin(), h.begin()+4, "RIFF") && std::equal(h.begin()+8, h.begin()+12, "AVI ")) return ".avi";
    if(starts({0x1a,0x45,0xdf,0xa3})) return ".mkv";
    if(starts({'F','L','V'})) return ".flv";
    if(starts({0,0,1,0xba}) || starts({0,0,1,0xb3})) return ".mpg";
    if(h.size() > 376 && h[0] == 0x47 && h[188] == 0x47 && h[376] == 0x47) return ".ts";
    if(starts({'O','g','g','S'})) return ".ogg";
    return {};
}
Index read_index(std::ifstream& input, std::uint64_t size, unsigned codepage, const Options& options) {
    if(size < 4) throw Error("文件太短，不是有效的 WA2 PAK");
    Bytes header(std::size_t(std::min<std::uint64_t>(512, size))); read(input, header);
    Index result; result.codepage = codepage;
    const bool kcap = std::equal(header.begin(), header.begin()+4, "KCAP");
    const bool lac = std::equal(header.begin(), header.begin()+4, "LAC\0");
    if(!kcap && !lac) {
        const auto extension = movie_extension(header);
        if(extension.empty()) throw Error("不支持的 PAK 格式（支持 WA2 的 KCAP/LAC 资源包和常用直接视频 PAK）");
        result.format = Format::movie;
        Entry entry; entry.local = fs::u8path("video" + extension); entry.stored = entry.expanded = size;
        result.entries.push_back(std::move(entry)); return result;
    }
    result.format = kcap ? Format::kcap : Format::lac;
    const unsigned start = kcap ? 16 : 8, stride = kcap ? 44 : 40;
    if(size < start) throw Error("PAK 头部不完整");
    result.header.assign(header.begin(), header.begin()+start);
    const auto count = u32(header.data() + (kcap ? 12 : 4));
    const auto end = std::uint64_t(start) + std::uint64_t(stride) * count;
    if(count > member_limit || end > size) throw Error("PAK 索引越界或文件数量超过上限");
    seek(input, start); Bytes table(std::size_t(stride) * count); read(input, table);
    result.entries.reserve(count);
    std::uint64_t total = 0;
    for(unsigned n = 0; n < count; ++n) {
        check(options); const auto* row = table.data() + std::size_t(n) * stride; Entry entry;
        entry.record.assign(row, row + stride);
        if(kcap) {
            entry.flags = u32(row); entry.offset = u32(row+36); entry.stored = u32(row+40);
            for(unsigned i = 0; i < 24 && row[4+i]; ++i) entry.name += char(row[4+i]);
            entry.directory = entry.stored == 0;
        } else {
            entry.flags = row[31]; entry.offset = u32(row+36); entry.stored = u32(row+32);
            for(unsigned i = 0; i < 31 && row[i]; ++i) entry.name += char(row[i] ^ 255);
        }
        if(entry.name.empty()) throw Error("PAK 索引包含空文件名");
        if((!entry.directory && entry.flags > 1) ||
           (entry.stored && (entry.offset < end || entry.offset > size || entry.stored > size-entry.offset)))
            throw Error("PAK 成员越界或使用了未知压缩方式（索引 " + std::to_string(n) + "）");
        if(!entry.stored && !entry.directory && entry.offset > size) throw Error("空文件的索引越界");
        entry.expanded = entry.stored;
        if(entry.flags == 1 && !entry.directory) {
            const unsigned prefix = kcap ? 8 : 4;
            if(entry.stored < prefix) throw Error("压缩文件缺少长度头");
            std::array<std::uint8_t, 8> packed{}; seek(input, entry.offset); read(input, std::span(packed).first(prefix));
            if(kcap && u32(packed.data()) != entry.stored && std::uint64_t(u32(packed.data())) + 8 != entry.stored)
                throw Error("KCAP 压缩长度头与索引不一致");
            entry.expanded = u32(packed.data() + (kcap ? 4 : 0));
        }
        if(entry.expanded > options.maximum_expanded - total) throw Error("解包展开总量超过安全上限（默认 64 GiB）");
        total += entry.expanded; result.entries.push_back(std::move(entry));
    }
    return result;
}
// Bounded input/output buffers keep extraction independent of member size.
void expand(std::istream& input, std::ostream& output, std::uint64_t stored, std::uint64_t expanded,
            std::uint8_t fill, const Options& options) {
    std::array<std::uint8_t, 4096> ring{}; std::fill_n(ring.begin(), 0xfee, fill);
    std::array<std::uint8_t, 65536> source{}, target{};
    std::size_t next = 0, available = 0, buffered = 0, cursor = 0xfee;
    auto get = [&]() -> std::uint8_t {
        if(next == available) {
            check(options); available = std::size_t(std::min<std::uint64_t>(source.size(), stored)); next = 0;
            if(!available) throw Error("LZSS 压缩数据被截断");
            read(input, std::span(source).first(available)); stored -= available;
        }
        return source[next++];
    };
    auto put = [&](std::uint8_t byte) {
        ring[cursor] = byte; cursor = (cursor+1) & 4095;
        target[buffered++] = byte;
        if(buffered == target.size()) { check(options); write(output, target); buffered = 0; }
    };
    std::uint64_t written = 0;
    while(written < expanded) {
        const auto flags = get();
        for(unsigned bit = 0; bit < 8 && written < expanded; ++bit) {
            if(flags & (1u << bit)) { put(get()); ++written; }
            else {
                const auto low = get(), high = get(); unsigned offset = low | ((high & 0xf0u) << 4);
                const auto count = (high & 15u) + 3;
                if(count > expanded-written) throw Error("LZSS 解压长度超出声明值");
                for(unsigned n = 0; n < count; ++n) put(ring[(offset+n) & 4095]);
                written += count;
            }
        }
    }
    write(output, std::span(target).first(buffered));
}
const char* format_name(Format format) {
    return format == Format::kcap ? "KCAP" : format == Format::lac ? "LAC" : "MOVIE";
}
void write_manifest(const fs::path& root, const Index& index) {
    std::ofstream output(root / manifest_name, std::ios::binary);
    output << "WA2PAK\t1\t" << format_name(index.format) << '\t' << index.codepage << '\t' << hex(index.header) << '\n';
    for(const auto& entry : index.entries)
        output << (entry.directory ? "D" : "F") << '\t' << hex(entry.record) << '\t' << hex(utf8(entry.local)) << '\n';
    finish(output);
}
Index read_manifest(const fs::path& root) {
    const auto path = root / manifest_name; no_links(path);
    if(fs::file_size(path) > 256ull * 1024 * 1024) throw Error("PAK 清单过大");
    auto input = open_input(path); std::string line;
    if(!std::getline(input, line)) throw Error("PAK 清单为空");
    if(!line.empty() && line.back() == '\r') line.pop_back();
    const auto fields = split(line);
    if(fields.size() != 5 || fields[0] != "WA2PAK" || fields[1] != "1") throw Error("不支持的 PAK 清单版本");
    Index result;
    if(fields[2] == "KCAP") result.format = Format::kcap;
    else if(fields[2] == "LAC") result.format = Format::lac;
    else if(fields[2] == "MOVIE") result.format = Format::movie;
    else throw Error("PAK 清单的类型无效");
    if(fields[3] == "932") result.codepage = 932;
    else if(fields[3] == "936") result.codepage = 936;
    else if(fields[3] == "65001") result.codepage = 65001;
    else throw Error("PAK 清单的文件名编码无效");
    const auto header = unhex(fields[4]); result.header.assign(header.begin(), header.end());
    if(result.header.size() != (result.format == Format::kcap ? 16u : result.format == Format::lac ? 8u : 0u))
        throw Error("PAK 清单的头部长度错误");
    if(result.format != Format::movie && !std::equal(result.header.begin(), result.header.begin()+4,
            result.format == Format::kcap ? "KCAP" : "LAC\0")) throw Error("PAK 清单的文件头标识错误");
    std::set<std::string> files;
    while(std::getline(input, line)) {
        if(!line.empty() && line.back() == '\r') line.pop_back();
        const auto parts = split(line);
        if(parts.size() != 3 || (parts[0] != "D" && parts[0] != "F")) throw Error("PAK 清单条目错误");
        Entry entry; entry.directory = parts[0] == "D"; entry.local = safe_relative(unhex(parts[2]));
        if(path_key(entry.local) == path_key(fs::u8path(manifest_name))) throw Error("PAK 清单不能引用自身");
        const auto record = unhex(parts[1]); entry.record.assign(record.begin(), record.end());
        if(entry.record.size() != (result.format == Format::kcap ? 44u : result.format == Format::lac ? 40u : 0u) ||
           (entry.directory && result.format != Format::kcap)) throw Error("PAK 清单记录长度或类型错误");
        if(result.format == Format::kcap) {
            for(unsigned i = 0; i < 24 && entry.record[4+i]; ++i) entry.name += char(entry.record[4+i]);
        } else if(result.format == Format::lac) {
            for(unsigned i = 0; i < 31 && entry.record[i]; ++i) entry.name += char(entry.record[i] ^ 255);
        }
        if(result.format != Format::movie && entry.name.empty()) throw Error("PAK 清单记录缺少原文件名");
        if(!entry.directory && !files.insert(path_key(entry.local)).second) throw Error("PAK 清单重复引用同一个文件");
        if(result.entries.size() == member_limit) throw Error("PAK 清单文件数量超过上限");
        result.entries.push_back(std::move(entry));
    }
    if(!input.eof()) throw Error("读取 PAK 清单失败");
    if(result.format == Format::movie && (result.entries.size() != 1 || result.entries[0].directory))
        throw Error("视频 PAK 清单必须有且只有一个视频文件");
    return result;
}
std::vector<fs::path> inventory(const fs::path& root, const Options& options, std::vector<fs::path>& directories) {
    std::vector<fs::path> result; std::set<std::string> keys;
    for(const auto& item : fs::recursive_directory_iterator(root)) {
        check(options); no_links(item.path());
        if(item.is_directory()) {
            const auto relative = item.path().lexically_relative(root); safe_relative(utf8(relative));
            directories.push_back(relative);
            if(directories.size() > member_limit) throw Error("源目录文件夹数量超过上限");
            continue;
        }
        if(!item.is_regular_file()) throw Error("源目录中存在非常规文件：" + utf8(item.path()));
        const auto relative = item.path().lexically_relative(root);
        if(path_key(relative) == path_key(fs::u8path(manifest_name))) continue;
        safe_relative(utf8(relative));
        if(!keys.insert(path_key(relative)).second) throw Error("源目录存在大小写冲突的文件名");
        result.push_back(relative);
        if(result.size() > member_limit) throw Error("源目录文件数量超过上限");
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return utf8(a) < utf8(b); });
    std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) { return utf8(a) < utf8(b); });
    return result;
}
Entry new_entry(const fs::path& local, std::string name, bool directory, Format format) {
    if(name.empty() || name.size() > (format == Format::kcap ? 24u : 31u) || name.find('\0') != std::string::npos ||
       (format == Format::lac && name.find(char(255)) != std::string::npos))
        throw Error("包内名称超出格式上限（KCAP 24 字节、LAC 31 字节）：" + utf8(local));
    Entry entry; entry.local = local; entry.name = std::move(name); entry.directory = directory;
    entry.record.resize(format == Format::kcap ? 44 : 40);
    if(format == Format::kcap) std::copy(entry.name.begin(), entry.name.end(), entry.record.begin()+4);
    else for(std::size_t n = 0; n < entry.name.size(); ++n) entry.record[n] = std::uint8_t(entry.name[n]) ^ 255;
    return entry;
}
void prepare_entries(Index& index, const fs::path& root, const std::vector<fs::path>& files,
                     const std::vector<fs::path>& folders, unsigned cp) {
    struct Block { std::optional<Entry> marker; std::vector<Entry> files; };
    std::vector<Block> blocks(1); std::map<std::string, std::size_t> directories{{"", 0}};
    std::set<std::string> existing, implied_parents;
    std::size_t block = 0;
    for(auto& entry : index.entries) {
        const auto path = root / entry.local;
        if(entry.directory) {
            if(fs::exists(path) && !fs::is_directory(path)) throw Error("清单中的文件夹已被替换成文件：" + utf8(entry.local));
            blocks.push_back({entry, {}}); block = blocks.size()-1;
            directories.try_emplace(path_key(entry.local), block);
            for(auto parent = entry.local.parent_path(); !parent.empty(); parent = parent.parent_path())
                implied_parents.insert(path_key(parent));
        } else if(fs::exists(path)) {
            no_links(path);
            if(!fs::is_regular_file(path)) throw Error("清单中的文件已被替换成文件夹");
            existing.insert(path_key(entry.local)); blocks[block].files.push_back(entry);
        }
    }
    if(index.format == Format::kcap) for(const auto& local : folders) {
        if(directories.contains(path_key(local)) || implied_parents.contains(path_key(local))) continue;
        auto marker = new_entry(local, encode_name(utf8(local), cp), true, index.format);
        blocks.push_back({std::move(marker), {}}); directories.emplace(path_key(local), blocks.size()-1);
    }
    for(const auto& local : files) {
        if(existing.contains(path_key(local))) continue;
        if(index.format == Format::lac) blocks[0].files.push_back(new_entry(local, encode_name(utf8(local), cp), false, index.format));
        else {
            const auto parent = local.parent_path(); auto found = directories.find(path_key(parent));
            if(found == directories.end()) {
                auto marker = new_entry(parent, encode_name(utf8(parent), cp), true, index.format);
                blocks.push_back({std::move(marker), {}});
                found = directories.emplace(path_key(parent), blocks.size()-1).first;
            }
            blocks[found->second].files.push_back(new_entry(local, encode_name(utf8(local.filename()), cp), false, index.format));
        }
    }
    index.entries.clear();
    for(auto& item : blocks) {
        if(item.marker && (fs::is_directory(root/item.marker->local) || !item.files.empty())) index.entries.push_back(std::move(*item.marker));
        for(auto& entry : item.files) index.entries.push_back(std::move(entry));
    }
    if(index.entries.size() > member_limit) throw Error("PAK 索引条目超过上限");
}
}

std::vector<std::uint8_t> compress_lzss(const Bytes& input, const Options& options) {
    // Offline hash-chain encoder; no game or guest CPU runtime is linked here.
    if(input.size() > 256ull * 1024 * 1024) throw Error("单次压缩输入超过 256 MiB");
    check(options);
    std::array<int, 65536> heads; heads.fill(-1); std::array<int, 4096> previous; previous.fill(-1);
    const auto hash = [&](std::size_t at) { return (unsigned(input[at])*251 + unsigned(input[at+1])*31 + input[at+2]) & 65535; };
    Bytes output; output.reserve(input.size());
    for(std::size_t at = 0; at < input.size();) {
        if((at & 0xffffu) < 18) check(options);
        const auto flag = output.size(); output.push_back(0);
        for(unsigned bit = 0; bit < 8 && at < input.size(); ++bit) {
            unsigned length = 0; int match = -1;
            if(at+2 < input.size()) {
                int candidate = heads[hash(at)]; const auto limit = std::max(0, int(at)-4096);
                for(unsigned tries = 0; tries < 32 && candidate >= limit && candidate < int(at); ++tries) {
                    unsigned size = 0;
                    while(size < 18 && at+size < input.size() && input[candidate+size] == input[at+size]) ++size;
                    if(size >= 3 && size > length) { length = size; match = candidate; if(size == 18) break; }
                    candidate = previous[unsigned(candidate) & 4095];
                }
            }
            if(length >= 3) {
                const auto offset = (0xfeeu + unsigned(match)) & 4095;
                output.push_back(std::uint8_t(offset)); output.push_back(std::uint8_t(((offset >> 4) & 0xf0) | (length-3)));
            } else { length = 1; output[flag] |= std::uint8_t(1u << bit); output.push_back(input[at]); }
            for(unsigned n = 0; n < length; ++n) {
                const auto pos = at+n;
                if(pos+2 < input.size()) { const auto h = hash(pos); previous[pos & 4095] = heads[h]; heads[h] = int(pos); }
            }
            at += length;
        }
    }
    return output;
}

Result unpack(const fs::path& archive, const fs::path& destination, const Options& options) {
    progress(options, 0, "正在检查 PAK 索引…");
    const auto target = destination_path(destination); no_links(archive);
    auto input = open_input(archive); const auto size = fs::file_size(archive);
    const auto cp = options.codepage ? options.codepage : 932; validate_codepage(cp);
    auto index = read_index(input, size, cp, options); Names names; fs::path group;
    std::uint64_t total = 0, done = 0, count = 0;
    for(std::size_t n = 0; n < index.entries.size(); ++n) {
        auto& entry = index.entries[n]; fs::path local;
        if(index.format == Format::movie) local = entry.local;
        else {
            std::string decoded;
            try { decoded = decode_name(entry.name, cp); }
            catch(const Error&) { decoded = "entry_" + std::to_string(n); }
            local = safe_relative(decoded, true);
            if(!entry.directory) local = group / local;
        }
        entry.local = names.reserve(local, entry.directory, unsigned(n));
        if(entry.directory) group = entry.local;
        else { total += entry.expanded; ++count; }
    }
    Stage stage(target.parent_path()); const auto tree = stage.path() / "contents"; fs::create_directory(tree);
    for(const auto& entry : index.entries) {
        check(options);
        if(entry.directory) { fs::create_directories(tree / entry.local); continue; }
        progress(options, total ? unsigned(done * 99 / total) : 0, "正在解包：" + utf8(entry.local));
        const auto output_path = tree / entry.local; fs::create_directories(output_path.parent_path());
        std::ofstream output(output_path, std::ios::binary);
        if(!output) throw Error("无法创建输出文件：" + utf8(entry.local));
        seek(input, entry.offset);
        if(entry.flags == 1 && index.format != Format::movie) {
            const unsigned prefix = index.format == Format::kcap ? 8 : 4; seek(input, entry.offset+prefix);
            expand(input, output, entry.stored-prefix, entry.expanded, index.format == Format::kcap ? 32 : 0, options);
        } else copy(input, output, entry.stored, options);
        finish(output); done += entry.expanded;
    }
    write_manifest(tree, index); check(options);
    if(fs::exists(target)) throw Error("目标文件夹在操作期间被其他程序创建，未覆盖");
    publish_no_replace(tree, target);
    // Completion must not turn an already-published result into cancellation.
    if(options.progress) options.progress(100, "解包完成");
    return {target, index.format, count, done};
}

Result pack(const fs::path& folder, const fs::path& destination, const Options& options) {
    progress(options, 0, "正在扫描源文件夹…"); no_links(folder);
    const auto root = fs::canonical(folder), target = destination_path(destination);
    if(!fs::is_directory(root)) throw Error("请选择一个文件夹");
    if(within(root, target)) throw Error("请把 PAK 保存在源文件夹之外，避免把输出文件打进包里");
    std::vector<fs::path> folders;
    auto files = inventory(root, options, folders); Index index;
    if(fs::exists(root/manifest_name)) index = read_manifest(root);
    const auto cp = options.codepage ? options.codepage : index.codepage; validate_codepage(cp);
    if(index.format == Format::movie) {
        if(files.size() != 1 || path_key(files[0]) != path_key(index.entries[0].local))
            throw Error("直接视频 PAK 的文件夹必须只保留清单中指定的视频文件");
        Stage stage(target.parent_path()); const auto temporary = stage.path()/"result.pak";
        auto input = open_input(root/files[0]); std::ofstream output(temporary, std::ios::binary);
        const auto size = fs::file_size(root/files[0]);
        progress(options, 5, "正在复制直接视频 PAK（不转码）…"); copy(input, output, size, options); finish(output); check(options);
        publish_no_replace(temporary, target);
        if(options.progress) options.progress(100, "打包完成");
        return {target, Format::movie, 1, size};
    }
    prepare_entries(index, root, files, folders, cp);
    const bool kcap = index.format == Format::kcap;
    const unsigned start = kcap ? 16 : 8, stride = kcap ? 44 : 40;
    if(index.header.empty()) {
        index.header.resize(start); std::copy_n(kcap ? "KCAP" : "LAC\0", 4, index.header.begin());
    }
    word(index.header, kcap ? 12 : 4, index.entries.size());
    Bytes table(index.entries.size() * stride);
    Stage stage(target.parent_path()); const auto temporary = stage.path()/"result.pak";
    std::ofstream output(temporary, std::ios::binary); write(output, index.header); write(output, table);
    std::uint64_t offset = start + table.size(), done = 0, total = 0, count = 0;
    for(const auto& entry : index.entries) if(!entry.directory) total += fs::file_size(root/entry.local);
    for(auto& entry : index.entries) {
        check(options); entry.offset = offset; entry.stored = 0;
        if(!entry.directory) {
            progress(options, total ? unsigned(done * 99 / total) : 0, "正在打包：" + utf8(entry.local));
            const auto path = root/entry.local; no_links(path);
            auto input = open_input(path); const auto size = fs::file_size(path);
            if(size > word_limit) throw Error("成员文件超过 4 GiB：" + utf8(entry.local));
            entry.flags = 0;
            const unsigned prefix = kcap ? 8 : 4;
            // Empty KCAP files need a real compressed payload so they cannot be
            // mistaken for the format's zero-length directory markers.
            const bool empty_file = kcap && size == 0;
            if((options.compress && size && size <= 256ull * 1024 * 1024) || empty_file) {
                Bytes bytes{ }; bytes.resize(std::size_t(size)); read(input, bytes);
                auto packed = compress_lzss(bytes, options);
                if(packed.size()+prefix < size || empty_file) {
                    Bytes header(prefix); word(header, kcap ? 4 : 0, size);
                    if(kcap) word(header, 0, packed.size()+prefix);
                    if(offset+header.size()+packed.size() > word_limit) throw Error("PAK 总大小超过 4 GiB");
                    write(output, header); write(output, packed); entry.flags = 1; entry.stored = header.size()+packed.size();
                } else {
                    if(offset+size > word_limit) throw Error("PAK 总大小超过 4 GiB");
                    write(output, bytes); entry.stored = size;
                }
            } else {
                if(offset+size > word_limit) throw Error("PAK 总大小超过 4 GiB");
                copy(input, output, size, options); entry.stored = size;
            }
            if(fs::file_size(path) != size) throw Error("源文件在打包期间发生变化：" + utf8(entry.local));
            done += size; ++count; offset += entry.stored;
        }
        if(kcap) {
            if(!entry.directory) word(entry.record, 0, entry.flags);
            word(entry.record, 36, entry.offset); word(entry.record, 40, entry.stored);
        } else {
            entry.record[31] = std::uint8_t(entry.flags); word(entry.record, 32, entry.stored); word(entry.record, 36, entry.offset);
        }
    }
    output.seekp(start);
    for(const auto& entry : index.entries) write(output, entry.record);
    finish(output); check(options);
    if(fs::exists(target)) throw Error("目标文件在操作期间被其他程序创建，未覆盖");
    publish_no_replace(temporary, target);
    if(options.progress) options.progress(100, "打包完成");
    return {target, index.format, count, offset};
}
}
