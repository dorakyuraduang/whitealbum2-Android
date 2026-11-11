#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "pak.hpp"
#include <limits>

namespace wa2::paktool {
namespace {
std::wstring wide(std::string_view bytes, unsigned codepage) {
    if(bytes.empty()) return {};
    if(bytes.size() > INT_MAX) throw Error(ErrorKind::filename, "Filename is too long");
    const int size = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, bytes.data(), int(bytes.size()), nullptr, 0);
    if(!size) throw Error(ErrorKind::filename, "Invalid filename bytes; choose the matching filename encoding and try again");
    std::wstring result(size, L'\0');
    if(!MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, bytes.data(), int(bytes.size()), result.data(), size))
        throw Error(ErrorKind::filename, "Cannot convert filename");
    return result;
}
std::string narrow(std::wstring_view text, unsigned codepage) {
    if(text.empty()) return {};
    if(text.size() > INT_MAX) throw Error(ErrorKind::filename, "Filename is too long");
    BOOL replaced = FALSE;
    const bool unicode = codepage == CP_UTF8;
    const DWORD flags = unicode ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    const int size = WideCharToMultiByte(codepage, flags, text.data(), int(text.size()), nullptr, 0, nullptr, unicode ? nullptr : &replaced);
    if(!size || replaced) throw Error(ErrorKind::filename, "New filename cannot be represented in the selected encoding; rename it or choose another encoding");
    std::string result(size, '\0');
    if(!WideCharToMultiByte(codepage, flags, text.data(), int(text.size()), result.data(), size, nullptr, unicode ? nullptr : &replaced) || replaced)
        throw Error(ErrorKind::filename, "Cannot convert filename");
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
    if(!size) throw Error(ErrorKind::filename, "Cannot compare filenames");
    std::wstring lower(size, L'\0');
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), int(text.size()), lower.data(), size, nullptr, nullptr, 0))
        throw Error(ErrorKind::filename, "Cannot compare filenames");
    return narrow(lower, CP_UTF8);
}
bool is_link(const fs::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
void publish_no_replace(const fs::path& source, const fs::path& destination) {
    // std::filesystem::rename may replace an existing file. Publish with the
    // Windows no-replace operation, including targets created during the job.
    for(unsigned attempt = 0; ; ++attempt) {
        if(MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) return;
        const auto error = GetLastError();
        if(error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS || GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES)
            throw Error(ErrorKind::target_exists, "Destination already exists; it was not overwritten: " + utf8(destination));
        // A scanner can briefly hold a just-written file or directory. Retry
        // only transient lock/access failures, always without REPLACE_EXISTING.
        if(attempt < 4 && (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION)) {
            Sleep(25); continue;
        }
        throw Error(ErrorKind::write, "Cannot publish output; check permissions and whether the destination is in use (Windows error " +
                    std::to_string(error) + "): " + utf8(destination));
    }
}
}
