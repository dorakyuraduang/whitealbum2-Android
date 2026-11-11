#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include "pak.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace {
using namespace wa2::paktool;
constexpr int pack_id = 101, unpack_id = 102, cancel_id = 103;
constexpr UINT finished_message = WM_APP + 1;
HWND window, pack_button, unpack_button, cancel_button, codepage_box, compression_box, status, bar;
HFONT font, title_font;
std::thread worker;
std::atomic_bool cancelled = false;
bool running = false;
struct Update {
    unsigned percent{};
    std::wstring text, result;
    bool success{}, cancelled{};
};
std::mutex update_mutex;
Update update;

std::wstring wide(std::string_view text) {
    if(text.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if(!count) return L"发生文件系统错误，请检查路径和权限。";
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), count);
    return result;
}
void fail(const std::wstring& text) { MessageBoxW(window, text.c_str(), L"WA2 PAK 工具", MB_OK | MB_ICONERROR); }
template<class T> struct Com {
    T* value{};
    ~Com() { if(value) value->Release(); }
    T* operator->() const { return value; }
};
fs::path choose(bool folder, bool save, const fs::path& suggested = {}) {
    Com<IFileDialog> dialog;
    const auto hr = CoCreateInstance(save && !folder ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IFileDialog, reinterpret_cast<void**>(&dialog.value));
    if(FAILED(hr)) throw Error("无法打开系统文件选择器");
    DWORD flags{}; dialog->GetOptions(&flags);
    flags |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
    if(folder) flags |= FOS_PICKFOLDERS;
    else if(!save) flags |= FOS_FILEMUSTEXIST;
    if(save) flags &= ~FOS_OVERWRITEPROMPT; // Existing targets are refused below.
    if(FAILED(dialog->SetOptions(flags))) throw Error("无法设置系统文件选择器");
    dialog->SetTitle(folder ? (save ? L"选择解包位置（会在其中新建同名文件夹）" : L"选择要打包的文件夹") :
                               save ? L"保存 PAK（请选择新文件名）" : L"选择要解包的 PAK");
    if(!folder) {
        const COMDLG_FILTERSPEC filters[]{{L"WA2 资源包 (*.pak)", L"*.pak"}, {L"所有文件", L"*.*"}};
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
        if(FAILED(result)) throw Error("系统文件选择失败");
        Com<IShellItem> item;
        if(FAILED(dialog->GetResult(&item.value))) throw Error("无法读取选择的路径");
        PWSTR name{};
        if(FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) throw Error("所选位置不是本机文件路径");
        fs::path path(name); CoTaskMemFree(name);
        if(save && !folder && fs::exists(path)) {
            MessageBoxW(window, L"为保留原文件，请选择一个尚不存在的 PAK 文件名。", L"目标文件已存在", MB_OK | MB_ICONINFORMATION);
            continue;
        }
        return path;
    }
}
void controls(bool busy) {
    EnableWindow(pack_button, !busy); EnableWindow(unpack_button, !busy);
    EnableWindow(codepage_box, !busy); EnableWindow(compression_box, !busy); EnableWindow(cancel_button, busy);
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
        options.progress = [](unsigned percent, std::string_view text) {
            std::lock_guard lock(update_mutex); update.percent = percent; update.text = wide(text);
        };
        cancelled = false; { std::lock_guard lock(update_mutex); update = {}; update.text = L"准备中…"; }
        running = true; controls(true); SendMessageW(bar, PBM_SETPOS, 0, 0); SetWindowTextW(status, L"准备中…");
        SetTimer(window, 1, 100, nullptr);
        try {
            worker = std::thread([packing, source, destination, options] {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                try {
                    const auto result = packing ? pack(source, destination, options) : unpack(source, destination, options);
                    const auto mb = double(result.bytes) / (1024 * 1024);
                    wchar_t bytes[80]; swprintf(bytes, 80, L"%.2f MiB", mb);
                    const auto text = (packing ? L"打包完成" : L"解包完成") + std::wstring(L"，") + std::to_wstring(result.files) + L" 个文件\r\n" +
                                      result.output.wstring() + L"\r\n" + bytes;
                    std::lock_guard lock(update_mutex); update.success = true; update.percent = 100; update.result = text;
                } catch(const Cancelled&) {
                    std::lock_guard lock(update_mutex); update.cancelled = true; update.result = L"已取消，未生成不完整的目标文件。";
                } catch(const std::exception& error) {
                    std::lock_guard lock(update_mutex); update.result = wide(error.what());
                }
                PostMessageW(window, finished_message, 0, 0);
            });
        } catch(...) { running = false; controls(false); KillTimer(window, 1); throw; }
    } catch(const std::exception& error) { fail(wide(error.what())); }
}
HWND control(const wchar_t* klass, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id = 0) {
    const auto handle = CreateWindowExW(0, klass, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
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
        const auto heading = control(L"STATIC", L"WA2 PAK 打包 / 解包", 0, 24, 18, 590, 38);
        SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
        control(L"STATIC", L"选择文件夹生成 PAK，或选择 PAK 解包到新文件夹。", 0, 25, 65, 600, 26);
        pack_button = control(L"BUTTON", L"选择文件夹 → 打包 PAK", BS_PUSHBUTTON | WS_TABSTOP, 25, 108, 294, 52, pack_id);
        unpack_button = control(L"BUTTON", L"选择 PAK → 解包文件夹", BS_PUSHBUTTON | WS_TABSTOP, 335, 108, 294, 52, unpack_id);
        control(L"STATIC", L"文件名编码", 0, 25, 183, 100, 27);
        codepage_box = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 130, 179, 347, 180);
        for(const auto* label : {L"自动保留（新包默认 Shift-JIS）", L"日文 Shift-JIS / CP932", L"简体中文 GBK / CP936", L"UTF-8"})
            SendMessageW(codepage_box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(codepage_box, CB_SETCURSEL, 0, 0);
        compression_box = control(L"BUTTON", L"LZSS 压缩（较慢；无收益的文件保留原样）", BS_AUTOCHECKBOX | WS_TABSTOP, 25, 222, 570, 29);
        SendMessageW(compression_box, BM_SETCHECK, BST_CHECKED, 0);
        control(L"STATIC", L"支持 KCAP / LAC；重新打包时请保留解包目录内的清单文件。\r\n解包文件名乱码时，可更改编码后重新解包。原文件不会被覆盖。", 0, 25, 265, 605, 48);
        bar = control(PROGRESS_CLASSW, L"", PBS_SMOOTH, 25, 337, 604, 20);
        SendMessageW(bar, PBM_SETRANGE32, 0, 100);
        status = control(L"STATIC", L"就绪", SS_LEFT, 25, 373, 600, 90);
        cancel_button = control(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, 529, 475, 100, 35, cancel_id);
        controls(false); return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wparam) == pack_id) start(true);
        else if(LOWORD(wparam) == unpack_id) start(false);
        else if(LOWORD(wparam) == cancel_id && running) {
            cancelled = true; EnableWindow(cancel_button, FALSE); SetWindowTextW(status, L"正在取消…");
        }
        return 0;
    case WM_TIMER: {
        std::lock_guard lock(update_mutex);
        SendMessageW(bar, PBM_SETPOS, update.percent, 0);
        SetWindowTextW(status, cancelled ? L"正在取消…" : update.text.c_str()); return 0;
    }
    case finished_message: {
        if(worker.joinable()) worker.join();
        running = false; KillTimer(handle, 1); controls(false);
        Update result; { std::lock_guard lock(update_mutex); result = update; }
        SendMessageW(bar, PBM_SETPOS, result.percent, 0); SetWindowTextW(status, result.result.c_str());
        if(!result.success && !result.cancelled) fail(result.result);
        return 0;
    }
    case WM_CLOSE:
        if(running) { cancelled = true; EnableWindow(cancel_button, FALSE); SetWindowTextW(status, L"正在取消，完成后可关闭窗口…"); }
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
    SetProcessDPIAware();
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if(FAILED(initialized)) return 1;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_PROGRESS_CLASS}; InitCommonControlsEx(&common);
    WNDCLASSW klass{}; klass.lpfnWndProc = procedure; klass.hInstance = instance;
    klass.lpszClassName = L"WA2PakTool"; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hIcon = LoadIconW(nullptr, IDI_APPLICATION); klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    RegisterClassW(&klass);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rectangle{0, 0, 654, 530}; AdjustWindowRectEx(&rectangle, style, FALSE, 0);
    window = CreateWindowExW(0, klass.lpszClassName, L"WA2 PAK 工具", style, CW_USEDEFAULT, CW_USEDEFAULT,
                           rectangle.right-rectangle.left, rectangle.bottom-rectangle.top, nullptr, nullptr, instance, nullptr);
    if(!window) { CoUninitialize(); return 1; }
    ShowWindow(window, show); MSG message{};
    while(GetMessageW(&message, nullptr, 0, 0) > 0) {
        if(!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if(worker.joinable()) { cancelled = true; worker.join(); }
    CoUninitialize(); return int(message.wParam);
}
