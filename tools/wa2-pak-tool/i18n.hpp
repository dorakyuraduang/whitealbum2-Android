#pragma once
#include "pak.hpp"
#include <array>
#include <string_view>

namespace wa2::paktool::ui {
enum class Language { english, simplified_chinese, traditional_chinese, japanese, korean,
                      ukrainian, russian, french, german, spanish, portuguese };
inline constexpr Language default_language = Language::english;
struct LanguageInfo { Language id; std::string_view code; std::wstring_view name; };
inline constexpr std::array<LanguageInfo, 11> languages{{
    {Language::english, "en", L"English"},
    {Language::simplified_chinese, "zh-CN", L"简体中文"},
    {Language::traditional_chinese, "zh-TW", L"繁體中文"},
    {Language::japanese, "ja", L"日本語"},
    {Language::korean, "ko", L"한국어"},
    {Language::ukrainian, "uk", L"Українська"},
    {Language::russian, "ru", L"Русский"},
    {Language::french, "fr", L"Français"},
    {Language::german, "de", L"Deutsch"},
    {Language::spanish, "es", L"Español"},
    {Language::portuguese, "pt", L"Português"},
}};
enum class Text {
    window_title, heading, introduction, language_label, pack_button, unpack_button,
    name_encoding, encoding_auto, encoding_sjis, encoding_gbk, compression, help,
    ready, cancel, preparing, cancelling, cancelled,
    choose_source, choose_destination, choose_pak, save_pak, all_files,
    exists_title, exists_message, packed, unpacked, files,
    checking_index, scanning_folder, packing_file, unpacking_file, copying_movie,
    error_general, error_read, error_write, error_format, error_unsafe, error_filename,
    error_limit, error_exists, error_target, error_link, error_changed,
    preference_not_saved, preserve_original, checking_original, copying_original, restored, count
};
Language parse_language(std::string_view code) noexcept;
std::string_view language_code(Language) noexcept;
std::wstring_view tr(Language, Text) noexcept;
std::wstring progress_text(Language, ProgressStage, std::wstring_view detail = {});
std::wstring error_text(Language, ErrorKind, std::wstring_view detail);
}
