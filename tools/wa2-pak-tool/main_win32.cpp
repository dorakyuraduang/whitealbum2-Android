#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include "pak.hpp"
#include "i18n.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace {
using namespace wa2::paktool;
using ui::Language;
using ui::Text;
constexpr int pack_id = 101, unpack_id = 102, cancel_id = 103, language_id = 104;
constexpr UINT finished_message = WM_APP + 1;
HWND window, heading, introduction, language_label, language_box, pack_button, unpack_button,
     cancel_button, encoding_label, codepage_box, compression_box, help, status, bar;
HFONT font, title_font;
Language language = ui::default_language;
std::thread worker;
std::atomic_bool cancelled = false;
bool running = false;
enum class State { idle, preparing, working, success, cancelled, failure };
struct Update {
    State state = State::idle;
    unsigned percent{};
    ProgressStage stage = ProgressStage::scanning_folder;
    std::wstring detail;
    Result result;
    ErrorKind error = ErrorKind::general;
};
std::mutex update_mutex;
Update update;

std::wstring wide(std::string_view text) {
    if(text.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if(!count) return L"File system error. Check the paths and permissions.";
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), count);
    return result;
}
const wchar_t* tr(Text text) { return ui::tr(language, text).data(); }
void fail(const std::wstring& text) { MessageBoxW(window, text.c_str(), tr(Text::window_title), MB_OK | MB_ICONERROR); }
Language load_language() {
    char code[32]{}; DWORD size = sizeof(code);
    if(RegGetValueA(HKEY_CURRENT_USER, "Software\\WA2PakTool", "Language", RRF_RT_REG_SZ, nullptr, code, &size) != ERROR_SUCCESS)
        return ui::default_language;
    code[sizeof(code)-1] = '\0';
    return ui::parse_language(code);
}
bool save_language(Language value) {
    HKEY key{};
    if(RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\WA2PakTool", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    const auto code = ui::language_code(value);
    const auto result = RegSetValueExA(key, "Language", 0, REG_SZ, reinterpret_cast<const BYTE*>(code.data()), DWORD(code.size()+1));
    RegCloseKey(key); return result == ERROR_SUCCESS;
}
std::wstring render_update(const Update& value) {
    switch(value.state) {
    case State::idle: return tr(Text::ready);
    case State::preparing: return tr(Text::preparing);
    case State::working: return ui::progress_text(language, value.stage, value.detail);
    case State::cancelled: return tr(Text::cancelled);
    case State::failure: return ui::error_text(language, value.error, value.detail);
    case State::success: {
        wchar_t bytes[80]; swprintf(bytes, 80, L"%.2f MiB", double(value.result.bytes)/(1024*1024));
        return std::wstring(tr(value.stage == ProgressStage::packed ? Text::packed : Text::unpacked)) + L" · " +
               tr(Text::files) + L": " + std::to_wstring(value.result.files) + L"\r\n" + value.result.output.wstring() + L"\r\n" + bytes;
    }
    }
    return tr(Text::error_general);
}
void refresh_status() {
    Update value; { std::lock_guard lock(update_mutex); value = update; }
    SendMessageW(bar, PBM_SETPOS, value.percent, 0);
    const auto text = running && cancelled ? std::wstring(tr(Text::cancelling)) : render_update(value);
    SetWindowTextW(status, text.c_str());
}
void apply_language() {
    SetWindowTextW(window, tr(Text::window_title)); SetWindowTextW(heading, tr(Text::heading));
    SetWindowTextW(introduction, tr(Text::introduction)); SetWindowTextW(language_label, tr(Text::language_label));
    SetWindowTextW(pack_button, tr(Text::pack_button)); SetWindowTextW(unpack_button, tr(Text::unpack_button));
    SetWindowTextW(encoding_label, tr(Text::name_encoding)); SetWindowTextW(compression_box, tr(Text::compression));
    SetWindowTextW(help, tr(Text::help)); SetWindowTextW(cancel_button, tr(Text::cancel));
    auto selected = SendMessageW(codepage_box, CB_GETCURSEL, 0, 0); if(selected == CB_ERR) selected = 0;
    SendMessageW(codepage_box, CB_RESETCONTENT, 0, 0);
    for(const auto text : {Text::encoding_auto, Text::encoding_sjis, Text::encoding_gbk})
        SendMessageW(codepage_box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tr(text)));
    SendMessageW(codepage_box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"UTF-8"));
    SendMessageW(codepage_box, CB_SETCURSEL, selected, 0);
    refresh_status();
}
template<class T> struct Com {
    T* value{};
    ~Com() { if(value) value->Release(); }
    T* operator->() const { return value; }
};
fs::path choose(bool folder, bool save, const fs::path& suggested = {}) {
    Com<IFileDialog> dialog;
    const auto hr = CoCreateInstance(save && !folder ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IFileDialog, reinterpret_cast<void**>(&dialog.value));
    if(FAILED(hr)) throw Error("Cannot open the system file picker");
    DWORD flags{}; dialog->GetOptions(&flags);
    flags |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
    if(folder) flags |= FOS_PICKFOLDERS;
    else if(!save) flags |= FOS_FILEMUSTEXIST;
    if(save) flags &= ~FOS_OVERWRITEPROMPT;
    if(FAILED(dialog->SetOptions(flags))) throw Error("Cannot configure the system file picker");
    dialog->SetTitle(tr(folder ? (save ? Text::choose_destination : Text::choose_source) : (save ? Text::save_pak : Text::choose_pak)));
    if(!folder) {
        const COMDLG_FILTERSPEC filters[]{{L"PAK (*.pak)", L"*.pak"}, {tr(Text::all_files), L"*.*"}};
        dialog->SetFileTypes(2, filters); dialog->SetDefaultExtension(L"pak");
    }
    if(!suggested.empty()) {
        dialog->SetFileName(suggested.filename().c_str());
        Com<IShellItem> parent;
        if(SUCCEEDED(SHCreateItemFromParsingName(suggested.parent_path().c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(&parent.value))))
            dialog->SetFolder(parent.value);
    }
    for(;;) {
        const auto result = dialog->Show(window);
        if(result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
        if(FAILED(result)) throw Error("The system file picker failed");
        Com<IShellItem> item;
        if(FAILED(dialog->GetResult(&item.value))) throw Error("Cannot read the selected path");
        PWSTR name{};
        if(FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) throw Error("The selected location is not a local filesystem path");
        fs::path path(name); CoTaskMemFree(name);
        if(save && !folder && fs::exists(path)) {
            MessageBoxW(window, tr(Text::exists_message), tr(Text::exists_title), MB_OK | MB_ICONINFORMATION);
            continue;
        }
        return path;
    }
}
void controls(bool busy) {
    EnableWindow(pack_button, !busy); EnableWindow(unpack_button, !busy);
    EnableWindow(codepage_box, !busy); EnableWindow(compression_box, !busy); EnableWindow(cancel_button, busy);
    // The worker only publishes typed stages and data, so language switching is
    // safe even during a job and immediately re-renders progress/results.
}
void start(bool packing) {
    if(running) return;
    try {
        const auto source = choose(packing, false);
        if(source.empty()) return;
        fs::path destination;
        if(packing) destination = choose(false, true, source.parent_path()/(source.filename().wstring()+L".pak"));
        else {
            const auto parent = choose(true, true);
            if(parent.empty()) return;
            auto name = source.stem(); if(name.empty()) name = L"unpacked";
            destination = parent/name;
            for(unsigned n = 2; fs::exists(destination); ++n) destination = parent/(name.wstring()+L"_"+std::to_wstring(n));
        }
        if(destination.empty()) return;
        Options options;
        const auto selected = SendMessageW(codepage_box, CB_GETCURSEL, 0, 0);
        options.codepage = selected == 1 ? 932 : selected == 2 ? 936 : selected == 3 ? 65001 : 0;
        options.compress = SendMessageW(compression_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
        options.cancel = &cancelled;
        options.progress = [](unsigned percent, ProgressStage stage, std::string_view detail) {
            std::lock_guard lock(update_mutex);
            update.state = State::working; update.percent = percent; update.stage = stage; update.detail = wide(detail);
        };
        cancelled = false; { std::lock_guard lock(update_mutex); update = {}; update.state = State::preparing; }
        running = true; controls(true); refresh_status(); SetTimer(window, 1, 100, nullptr);
        try {
            worker = std::thread([packing, source, destination, options] {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                try {
                    const auto result = packing ? pack(source, destination, options) : unpack(source, destination, options);
                    std::lock_guard lock(update_mutex); update.state = State::success; update.percent = 100;
                    update.result = result; update.stage = packing ? ProgressStage::packed : ProgressStage::unpacked;
                } catch(const Cancelled&) {
                    std::lock_guard lock(update_mutex); update.state = State::cancelled;
                } catch(const std::exception& error) {
                    const auto* known = dynamic_cast<const Error*>(&error);
                    std::lock_guard lock(update_mutex); update.state = State::failure;
                    update.error = known ? known->kind : ErrorKind::general; update.detail = wide(error.what());
                }
                PostMessageW(window, finished_message, 0, 0);
            });
        } catch(...) { running = false; controls(false); KillTimer(window, 1); throw; }
    } catch(const std::exception& error) {
        const auto* known = dynamic_cast<const Error*>(&error);
        fail(ui::error_text(language, known ? known->kind : ErrorKind::general, wide(error.what())));
    }
}
HWND control(const wchar_t* klass, DWORD style, int x, int y, int width, int height, int id = 0) {
    const auto handle = CreateWindowExW(0, klass, L"", WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
                                      reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); return handle;
}
LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    switch(message) {
    case WM_CREATE: {
        window = handle;
        font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        title_font = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        heading = control(L"STATIC", 0, 25, 18, 604, 38);
        SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
        language_label = control(L"STATIC", 0, 25, 73, 165, 27);
        language_box = control(WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 200, 69, 429, 320, language_id);
        for(const auto& entry : ui::languages) SendMessageW(language_box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.name.data()));
        SendMessageW(language_box, CB_SETCURSEL, WPARAM(language), 0);
        introduction = control(L"STATIC", 0, 25, 113, 604, 44);
        pack_button = control(L"BUTTON", BS_PUSHBUTTON | BS_MULTILINE | WS_TABSTOP, 25, 169, 294, 56, pack_id);
        unpack_button = control(L"BUTTON", BS_PUSHBUTTON | BS_MULTILINE | WS_TABSTOP, 335, 169, 294, 56, unpack_id);
        encoding_label = control(L"STATIC", 0, 25, 245, 175, 42);
        codepage_box = control(WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 210, 241, 419, 180);
        compression_box = control(L"BUTTON", BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, 25, 297, 604, 44);
        SendMessageW(compression_box, BM_SETCHECK, BST_CHECKED, 0);
        help = control(L"STATIC", 0, 25, 354, 604, 72);
        bar = control(PROGRESS_CLASSW, PBS_SMOOTH, 25, 441, 604, 20);
        SendMessageW(bar, PBM_SETRANGE32, 0, 100);
        status = control(L"STATIC", SS_LEFT, 25, 477, 604, 92);
        cancel_button = control(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, 499, 585, 130, 35, cancel_id);
        controls(false); apply_language(); return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wparam) == pack_id) start(true);
        else if(LOWORD(wparam) == unpack_id) start(false);
        else if(LOWORD(wparam) == cancel_id && running) {
            cancelled = true; EnableWindow(cancel_button, FALSE); refresh_status();
        } else if(LOWORD(wparam) == language_id && HIWORD(wparam) == CBN_SELCHANGE) {
            const auto selected = SendMessageW(language_box, CB_GETCURSEL, 0, 0);
            language = selected >= 0 && std::size_t(selected) < ui::languages.size() ? ui::languages[std::size_t(selected)].id : ui::default_language;
            apply_language();
            if(!save_language(language)) SetWindowTextW(status, tr(Text::preference_not_saved));
        }
        return 0;
    case WM_TIMER: refresh_status(); return 0;
    case finished_message: {
        if(worker.joinable()) worker.join();
        running = false; KillTimer(handle, 1); controls(false); refresh_status();
        Update value; { std::lock_guard lock(update_mutex); value = update; }
        if(value.state == State::failure) fail(render_update(value));
        return 0;
    }
    case WM_CLOSE:
        if(running) { cancelled = true; EnableWindow(cancel_button, FALSE); refresh_status(); }
        else DestroyWindow(handle);
        return 0;
    case WM_DESTROY:
        if(font) DeleteObject(font);
        if(title_font) DeleteObject(title_font);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(handle, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    SetProcessDPIAware(); language = load_language();
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if(FAILED(initialized)) return 1;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_PROGRESS_CLASS}; InitCommonControlsEx(&common);
    WNDCLASSW klass{}; klass.lpfnWndProc = procedure; klass.hInstance = instance;
    klass.lpszClassName = L"WA2PakTool"; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hIcon = LoadIconW(nullptr, IDI_APPLICATION); klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    RegisterClassW(&klass);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rectangle{0, 0, 654, 638}; AdjustWindowRectEx(&rectangle, style, FALSE, 0);
    window = CreateWindowExW(0, klass.lpszClassName, tr(Text::window_title), style, CW_USEDEFAULT, CW_USEDEFAULT,
                           rectangle.right-rectangle.left, rectangle.bottom-rectangle.top, nullptr, nullptr, instance, nullptr);
    if(!window) { CoUninitialize(); return 1; }
    ShowWindow(window, show); MSG message{};
    while(GetMessageW(&message, nullptr, 0, 0) > 0) {
        if(!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if(worker.joinable()) { cancelled = true; worker.join(); }
    CoUninitialize(); return int(message.wParam);
}
