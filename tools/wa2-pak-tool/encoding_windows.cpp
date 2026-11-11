#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "pak.hpp"
#include <limits>

namespace wa2::paktool {
namespace {
std::wstring wide(std::string_view bytes, unsigned codepage) {
    if(bytes.empty()) return {};
    if(bytes.size() > INT_MAX) throw Error("文件名过长");
    const int size = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, bytes.data(), int(bytes.size()), nullptr, 0);
    if(!size) throw Error("文件名编码不正确，请选择匹配的文件名编码后重试");
    std::wstring result(size, L'\0');
    if(!MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, bytes.data(), int(bytes.size()), result.data(), size))
        throw Error("无法转换文件名");
    return result;
}
std::string narrow(std::wstring_view text, unsigned codepage) {
    if(text.empty()) return {};
    if(text.size() > INT_MAX) throw Error("文件名过长");
    BOOL replaced = FALSE;
    const bool unicode = codepage == CP_UTF8;
    const DWORD flags = unicode ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    const int size = WideCharToMultiByte(codepage, flags, text.data(), int(text.size()), nullptr, 0, nullptr, unicode ? nullptr : &replaced);
    if(!size || replaced) throw Error("新文件名无法用所选编码表示，请改名或选择其他文件名编码");
    std::string result(size, '\0');
    if(!WideCharToMultiByte(codepage, flags, text.data(), int(text.size()), result.data(), size, nullptr, unicode ? nullptr : &replaced) || replaced)
        throw Error("无法转换文件名");
    return result;
}
}
std::string utf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}
std::string decode_name(std::string_view bytes, unsigned cp) { return narrow(wide(bytes, cp), CP_UTF8); }
std::string encode_name(std::string_view text, unsigned cp) { return narrow(wide(text, CP_UTF8), cp); }
std::string path_key(const fs::path& path) {
    auto text = path.lexically_normal().generic_wstring();
    if(text.empty()) return {};
    const int size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr, 0);
    if(!size) throw Error("无法比较文件名");
    std::wstring lower(size, L'\0');
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), int(text.size()), lower.data(), size, nullptr, nullptr, 0))
        throw Error("无法比较文件名");
    return narrow(lower, CP_UTF8);
}
bool is_link(const fs::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
void publish_no_replace(const fs::path& source, const fs::path& destination) {
    // std::filesystem::rename may replace an existing file. Publish with the
    // Windows no-replace operation, including targets created during the job.
    if(!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        if(error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
            throw Error("目标已存在，未覆盖：" + utf8(destination));
        throw Error("无法发布输出，请检查目标权限和是否被占用（Windows 错误 " + std::to_string(error) + "）");
    }
}
}
