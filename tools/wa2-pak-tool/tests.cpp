#include "pak.hpp"
#include "engine/resource_archive.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <span>

namespace {
using namespace wa2::paktool;
using Bytes = std::vector<std::uint8_t>;
unsigned assertions = 0;
void require(bool condition, const char* text) {
    ++assertions;
    if(!condition) throw std::runtime_error(text);
}
template<class F> void rejects(F action, const char* text) {
    bool rejected = false;
    try { action(); } catch(const std::exception&) { rejected = true; }
    require(rejected, text);
}
void put(const fs::path& path, const Bytes& data) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary); output.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if(!output) throw std::runtime_error("test fixture write failed");
}
void put(const fs::path& path, std::string_view text) { put(path, Bytes(text.begin(), text.end())); }
Bytes get(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if(!input) throw std::runtime_error("test fixture read failed");
    return Bytes((std::istreambuf_iterator<char>(input)), {});
}
void word(Bytes& data, unsigned offset, std::uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) data[offset+i] = std::uint8_t(value >> (8*i));
}
std::uint32_t word(const Bytes& data, unsigned offset) {
    return std::uint32_t(data[offset]) | std::uint32_t(data[offset+1])<<8 | std::uint32_t(data[offset+2])<<16 | std::uint32_t(data[offset+3])<<24;
}
class Source : public wa2::resource::Source {
    Bytes bytes_;
public:
    explicit Source(Bytes bytes) : bytes_(std::move(bytes)) {}
    std::uint64_t size() const override { return bytes_.size(); }
    void read_at(std::uint64_t at, std::span<std::uint8_t> target) override {
        if(at > bytes_.size() || target.size() > bytes_.size()-at) throw std::runtime_error("test source range");
        std::copy_n(bytes_.begin()+std::ptrdiff_t(at), target.size(), target.begin());
    }
};
struct Fixture { std::string name; Bytes data; unsigned flags = 0; bool directory = false; };
Bytes make_archive(bool kcap, const std::vector<Fixture>& files) {
    const unsigned start = kcap ? 16 : 8, stride = kcap ? 44 : 40;
    Bytes bytes(start+files.size()*stride); std::copy_n(kcap ? "KCAP" : "LAC\0", 4, bytes.begin());
    if(kcap) { bytes[4] = 0x29; bytes[6] = 0x23; bytes[10] = 0x42; }
    word(bytes, kcap ? 12 : 4, std::uint32_t(files.size()));
    for(std::size_t n = 0; n < files.size(); ++n) {
        const auto& file = files[n]; const auto at = unsigned(start+n*stride);
        if(kcap) {
            require(file.name.size() <= 24, "KCAP fixture name");
            word(bytes, at, file.flags); std::copy(file.name.begin(), file.name.end(), bytes.begin()+at+4);
            bytes[at+28] = 0x71; bytes[at+29] = std::uint8_t(n);
            word(bytes, at+36, std::uint32_t(bytes.size())); word(bytes, at+40, std::uint32_t(file.data.size()));
        } else {
            require(file.name.size() <= 31, "LAC fixture name");
            for(std::size_t i = 0; i < file.name.size(); ++i) bytes[at+i] = std::uint8_t(file.name[i]) ^ 255;
            bytes[at+31] = std::uint8_t(file.flags); word(bytes, at+32, std::uint32_t(file.data.size())); word(bytes, at+36, std::uint32_t(bytes.size()));
        }
        bytes.insert(bytes.end(), file.data.begin(), file.data.end());
    }
    return bytes;
}
Bytes stored(const Bytes& original, bool kcap, bool alternate = false) {
    const auto encoded = compress_lzss(original, {}); Bytes result(kcap ? 8 : 4);
    word(result, kcap ? 4 : 0, std::uint32_t(original.size()));
    if(kcap) word(result, 0, std::uint32_t(encoded.size()+(alternate ? 0 : 8)));
    result.insert(result.end(), encoded.begin(), encoded.end()); return result;
}
void no_staging(const fs::path& root) {
    for(const auto& entry : fs::directory_iterator(root))
        require(!entry.path().filename().wstring().starts_with(L".wa2pak-work-"), "temporary staging leaked");
}
void codec_tests() {
    std::mt19937 random(17);
    for(unsigned size : {0u, 1u, 2u, 3u, 18u, 4095u, 4096u, 4097u, 200003u}) {
        for(unsigned mode = 0; mode < 3; ++mode) {
            Bytes input(size);
            for(unsigned i = 0; i < size; ++i) input[i] = mode == 0 ? 32 : mode == 1 ? std::uint8_t(i % 113) : std::uint8_t(random());
            const auto compressed = compress_lzss(input, {});
            for(unsigned fill : {0u, 32u}) {
                Bytes output(size); wa2::resource::decode_lzss(compressed, output, std::uint8_t(fill));
                require(input == output, "LZSS independent decoder mismatch");
            }
        }
    }
    require(encode_name("ソ.txt", 932) == std::string("\x83\x5c.txt", 6), "Shift-JIS trail byte fixture");
    require(decode_name(std::string("\x83\x5c.txt", 6), 932) == "ソ.txt", "Shift-JIS trail byte split");
    require(decode_name(encode_name("字库.txt", 936), 936) == "字库.txt", "GBK codec");
    require(decode_name(encode_name("Україна.txt", 65001), 65001) == "Україна.txt", "UTF8 codec");
    rejects([] { encode_name("Україна.txt", 932); }, "unrepresentable names silently substituted");
}
void roundtrips(const fs::path& root) {
    const auto folder = root/fs::u8path("源文件夹"); fs::create_directory(folder);
    const Bytes text(360001, 'A'); Bytes random(131073); std::mt19937 rng(7); for(auto& byte : random) byte = std::uint8_t(rng());
    put(folder/"a.txt", text); put(folder/"random.bin", random); put(folder/"empty.txt", std::string_view{});
    put(folder/fs::u8path("ソ.txt"), "japanese trail byte");
    put(folder/"script"/"scene.txt", "main scene"); put(folder/"ic"/"script"/"scene.txt", "IC scene");
    fs::create_directories(folder/"empty-folder/nested");
    const auto archive = root/fs::u8path("测试.pak"); const auto packed = pack(folder, archive);
    require(packed.files == 6 && packed.format == Format::kcap, "KCAP pack count");
    auto reader = wa2::resource::Archive::open(std::make_shared<Source>(get(archive)));
    require(reader.load(reader.find("a.txt")) == text, "game archive reader rejected new compressed member");
    require(reader.load(reader.find("random.bin")) == random, "stored random member mismatch");
    require(reader.load(reader.find("empty.txt")).empty(), "zero-length file represented as directory");
    require(reader.members()[reader.find("a.txt")].compressed, "compression not used");
    require(!reader.members()[reader.find("random.bin")].compressed, "incompressible member expanded");
    const auto extracted = root/"extracted"; unpack(archive, extracted);
    for(const auto& relative : {fs::path("a.txt"), fs::path("random.bin"), fs::path("empty.txt"), fs::u8path("ソ.txt"), fs::path("script/scene.txt"), fs::path("ic/script/scene.txt")})
        require(get(folder/relative) == get(extracted/relative), "KCAP extraction mismatch");
    require(fs::is_regular_file(extracted/"empty.txt"), "empty file became folder");
    require(fs::is_directory(extracted/"empty-folder/nested"), "empty folder omitted");
    put(extracted/"script"/"scene.txt", "changed main scene"); put(extracted/"script"/"added.txt", "new file"); fs::remove(extracted/"random.bin");
    Options uncompressed; uncompressed.compress = false;
    const auto repacked = root/"repacked.pak"; pack(extracted, repacked, uncompressed); unpack(repacked, root/"again");
    require(get(root/"again/script/scene.txt") == get(extracted/"script/scene.txt"), "modified file not packed");
    require(get(root/"again/script/added.txt") == get(extracted/"script/added.txt"), "new member not assigned to group");
    require(!fs::exists(root/"again/random.bin"), "deleted member remained");
    reader = wa2::resource::Archive::open(std::make_shared<Source>(get(repacked)));
    require(!reader.members()[reader.find("a.txt")].compressed, "compression switch ignored");
    const auto before = get(archive);
    rejects([&] { pack(folder, archive); }, "existing archive overwritten");
    require(get(archive) == before, "existing file mutation");
    rejects([&] { unpack(archive, extracted); }, "existing output folder overwritten");
    rejects([&] { pack(folder, folder/"inside.pak"); }, "output packed inside source folder");
    const auto multilingual = root/"multilingual"; put(multilingual/fs::u8path("Україна.txt"), "ukrainian");
    Options unicode; unicode.codepage = 65001; pack(multilingual, root/"utf8.pak", unicode); unpack(root/"utf8.pak", root/"utf8", unicode);
    pack(root/"utf8", root/"utf8-again.pak"); unpack(root/"utf8-again.pak", root/"utf8-again", unicode);
    require(get(root/fs::u8path("utf8-again/Україна.txt")) == get(multilingual/fs::u8path("Україна.txt")), "manifest did not retain encoding");
    const auto gbk = root/"gbk-source"; put(gbk/fs::u8path("汉字.txt"), "GBK data");
    Options chinese; chinese.codepage = 936; pack(gbk, root/"gbk.pak", chinese); unpack(root/"gbk.pak", root/"gbk", chinese);
    require(get(root/fs::u8path("gbk/汉字.txt")) == get(gbk/fs::u8path("汉字.txt")), "GBK filename roundtrip");
}
void format_fixtures(const fs::path& root) {
    const Bytes phrase(10000, 32);
    const auto bytes = make_archive(true, {
        {"root.txt", stored(phrase, true, true), 1},
        {"script", {}, 0xccccccccu, true},
        {"same.txt", Bytes{'a'}}, {"SAME.TXT", Bytes{'b'}},
        {"ic/script", {}, 0, true}, {"same.txt", Bytes{'c'}},
        {"123456789012345678901234", Bytes{'d'}},
        {std::string("\xff", 1), Bytes{'e'}}
    });
    put(root/"groups.pak", bytes); unpack(root/"groups.pak", root/"groups");
    require(get(root/"groups/root.txt") == phrase, "alternate KCAP size header");
    require(get(root/"groups/script/same.txt") == Bytes{'a'}, "first duplicate lost");
    require(get(root/"groups/script/SAME~4.TXT") == Bytes{'b'}, "case-folding collision lost");
    require(get(root/"groups/ic/script/same.txt") == Bytes{'c'}, "IC scope collision");
    Options raw; raw.compress = false; pack(root/"groups", root/"groups-repacked.pak", raw);
    const auto rebuilt = get(root/"groups-repacked.pak");
    require(std::equal(bytes.begin()+4, bytes.begin()+12, rebuilt.begin()+4), "reserved KCAP header lost");
    require(word(bytes, 12) == word(rebuilt, 12), "directory entry count changed");
    for(unsigned i = 0; i < word(bytes, 12); ++i) {
        const auto at = 16+i*44;
        require(std::equal(bytes.begin()+at+4, bytes.begin()+at+36, rebuilt.begin()+at+4), "original name/reserved fields lost");
    }
    unpack(root/"groups-repacked.pak", root/"groups-again");
    require(get(root/"groups-again/script/SAME~4.TXT") == Bytes{'b'}, "duplicate roundtrip");
    const auto lac = make_archive(false, {{"plain.txt", Bytes{'l','a','c'}}, {"packed.txt", stored(phrase, false), 1}, {"empty", {}}});
    put(root/"legacy.pak", lac); unpack(root/"legacy.pak", root/"legacy");
    require(get(root/"legacy/packed.txt") == phrase, "LAC expansion");
    pack(root/"legacy", root/"legacy-again.pak");
    auto game = wa2::resource::Archive::open(std::make_shared<Source>(get(root/"legacy-again.pak")));
    require(!game.is_kcap() && game.load(game.find("packed.txt")) == phrase, "LAC format not retained/readable");
    require(game.load(game.find("empty")).empty(), "LAC empty member");
    // Reference an untouched initial dictionary, which differs by format.
    for(bool kcap : {false, true}) {
        Bytes payload(kcap ? 8 : 4); word(payload, kcap ? 4 : 0, 3);
        if(kcap) word(payload, 0, 11);
        payload.insert(payload.end(), {0, 0, 0});
        const auto name = kcap ? "spaces" : "zeros";
        put(root/(std::string(name)+".pak"), make_archive(kcap, {{"initial", payload, 1}}));
        unpack(root/(std::string(name)+".pak"), root/name);
        require(get(root/name/"initial") == Bytes(3, kcap ? 32 : 0), "initial LZSS dictionary fill");
    }
}
void malformed_tests(const fs::path& root) {
    auto reject_archive = [&](Bytes bytes, const char* text) {
        const auto path = root/"damaged.pak"; put(path, bytes);
        rejects([&] { unpack(path, root/"must-not-exist"); }, text);
        require(!fs::exists(root/"must-not-exist"), "partial output escaped staging"); no_staging(root);
    };
    reject_archive(Bytes{'K','C'}, "short header accepted");
    auto bad = make_archive(true, {{"file", Bytes{'x'}}}); word(bad, 12, 0xffffffffu); reject_archive(bad, "huge index accepted");
    bad = make_archive(true, {{"file", Bytes{'x'}}}); word(bad, 16+36, 0xfffffffeu); reject_archive(bad, "out of range accepted");
    bad = make_archive(true, {{"file", Bytes{'x'}, 7}}); reject_archive(bad, "unknown compression accepted");
    for(const auto* name : {"../outside", "C:/outside", "/outside", "a/../../outside", "a:stream"})
        reject_archive(make_archive(true, {{name, Bytes{'x'}}}), "unsafe archive path accepted");
    bad = stored(Bytes(100, 'X'), true); bad.resize(8); word(bad, 0, 8);
    reject_archive(make_archive(true, {{"broken", bad, 1}}), "truncated LZSS accepted");
    bad = stored(Bytes(100, 'X'), true); word(bad, 0, 12345);
    reject_archive(make_archive(true, {{"broken", bad, 1}}), "inconsistent packed length accepted");
    Options bounded; bounded.maximum_expanded = 99;
    put(root/"expansion.pak", make_archive(true, {{"limit", stored(Bytes(100, 'X'), true), 1}}));
    rejects([&] { unpack(root/"expansion.pak", root/"must-not-exist", bounded); }, "expanded-byte cap ignored");
    const auto malicious = root/"malicious-manifest"; fs::create_directory(malicious);
    put(malicious/manifest_name, "WA2PAK\t1\tMOVIE\t932\t\nF\t\t2e2e2f6f757473696465\n");
    rejects([&] { pack(malicious, root/"malicious.pak"); }, "manifest traversal accepted");
    put(malicious/manifest_name, "WA2PAK\t1\tMOVIE\t932\t\nF\t\t2e77613270616b2d6d616e6966657374\n");
    rejects([&] { pack(malicious, root/"malicious.pak"); }, "manifest self-reference accepted");
    put(malicious/manifest_name, "WA2PAK\t1\tKCAP\t932\t00000000000000000000000000000000\n");
    rejects([&] { pack(malicious, root/"malicious.pak"); }, "invalid manifest header accepted");
    std::atomic_bool cancel = false;
    Options stopped; stopped.cancel = &cancel;
    stopped.progress = [&](unsigned, std::string_view text) { if(text.starts_with("正在打包：")) cancel = true; };
    rejects([&] { pack(root/fs::u8path("源文件夹"), root/"cancelled.pak", stopped); }, "cancellation ignored");
    require(!fs::exists(root/"cancelled.pak"), "cancelled output published"); no_staging(root);
    cancel = false;
    stopped.progress = [&](unsigned, std::string_view text) { if(text.starts_with("正在解包：")) cancel = true; };
    rejects([&] { unpack(root/"groups.pak", root/"cancelled", stopped); }, "extract cancellation ignored");
    require(!fs::exists(root/"cancelled"), "cancelled extraction published"); no_staging(root);
    Options race; race.compress = false;
    race.progress = [&](unsigned, std::string_view text) { if(text.starts_with("正在打包：")) put(root/"race.pak", "keep"); };
    rejects([&] { pack(root/fs::u8path("源文件夹"), root/"race.pak", race); }, "concurrent target overwritten");
    require(get(root/"race.pak") == Bytes({'k','e','e','p'}), "concurrent target changed"); no_staging(root);
    put(root/"publish-source", "source"); put(root/"publish-destination", "destination");
    rejects([&] { publish_no_replace(root/"publish-source", root/"publish-destination"); }, "no-replace publish replaced file");
    require(get(root/"publish-destination") == Bytes({'d','e','s','t','i','n','a','t','i','o','n'}), "publish target changed");
    const auto long_name = root/"long-name"; put(long_name/"1234567890123456789012345", "long");
    rejects([&] { pack(long_name, root/"long-name.pak"); }, "name silently truncated");
    // Reparse points are never followed, if creating one is permitted here.
    std::error_code link_error; fs::create_directory_symlink(root/"groups", root/"linked", link_error);
    if(!link_error) rejects([&] { pack(root/"linked", root/"link.pak"); }, "root directory link followed");
}
void movie_tests(const fs::path& root) {
    const Bytes guid{0x30,0x26,0xb2,0x75,0x8e,0x66,0xcf,0x11,0xa6,0xd9,0,0xaa,0,0x62,0xce,0x6c};
    auto bytes = guid; bytes.resize(800001, 17); put(root/"mv100.pak", bytes);
    const auto extracted = unpack(root/"mv100.pak", root/"movie");
    require(extracted.format == Format::movie && get(root/"movie/video.wmv") == bytes, "direct video extraction");
    pack(root/"movie", root/"movie-again.pak"); require(get(root/"movie-again.pak") == bytes, "direct video not byte-identical");
    put(root/"movie/extra.txt", "extra"); rejects([&] { pack(root/"movie", root/"movie-with-extra.pak"); }, "movie metadata ignored extra files");
    const std::vector<std::pair<std::string, Bytes>> formats{
        {"mp4", Bytes{0,0,0,24,'f','t','y','p','i','s','o','m'}},
        {"avi", Bytes{'R','I','F','F',0,0,0,0,'A','V','I',' '}},
        {"mkv", Bytes{0x1a,0x45,0xdf,0xa3}}, {"flv", Bytes{'F','L','V',1}},
        {"mpg", Bytes{0,0,1,0xba}}, {"ogg", Bytes{'O','g','g','S'}}
    };
    for(const auto& [ext, data] : formats) {
        put(root/(ext+".pak"), data); unpack(root/(ext+".pak"), root/(ext+"-video"));
        require(get(root/(ext+"-video")/("video."+ext)) == data, "direct movie signature");
    }
}
}
int main(int argc, char** argv) {
    try {
        if(argc != 2) throw std::runtime_error("usage: wa2_pak_tests <existing-test-parent>");
        const auto parent = fs::canonical(fs::u8path(argv[1]));
        const auto root = parent/("pak-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if(!fs::create_directory(root)) throw std::runtime_error("test directory already exists");
        // Keep fixtures on failure; only remove this verified, exclusively-owned
        // absolute directory after all assertions have passed.
        codec_tests(); roundtrips(root); format_fixtures(root); malformed_tests(root); movie_tests(root); no_staging(root);
        require(root.is_absolute() && root.parent_path() == parent && fs::canonical(root) == root && !is_link(root), "test cleanup containment");
        fs::remove_all(root);
        std::cout << "PAK component checks passed: " << assertions << " assertions; no game process or screen used\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
