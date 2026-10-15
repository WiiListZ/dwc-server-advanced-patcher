#include "platform.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  include <shobjidl.h>
#  pragma comment(lib, "ole32.lib")
#  pragma comment(lib, "shell32.lib")
#else
#  include <climits>
#  include <unistd.h>
#  ifdef __APPLE__
#    include <mach-o/dyld.h>
#  endif
#endif

namespace fs = std::filesystem;

#ifdef _WIN32

std::wstring widen(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string narrow(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::string pathToUtf8(const fs::path& path)
{
    return narrow(path.wstring());
}

fs::path pathFromUtf8(const std::string& text)
{
    return fs::path(widen(text));
}

fs::path executableDir()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return fs::path(buffer).parent_path();
}

void openInFileManager(const fs::path& path)
{
    fs::path target = fs::is_directory(path) ? path : path.parent_path();
    ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::optional<fs::path> pickFolder(const std::string& title, const fs::path& start)
{
    std::optional<fs::path> result;
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        std::wstring wideTitle = widen(title);
        dialog->SetTitle(wideTitle.c_str());
        IShellItem* folder = nullptr;
        if (!start.empty() && fs::is_directory(start) &&
            SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dialog->SetFolder(folder);
            folder->Release();
        }
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR chosen = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &chosen))) {
                    result = fs::path(chosen);
                    CoTaskMemFree(chosen);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
    return result;
}

static std::optional<std::string> msys2Bash()
{
    for (const char* candidate : {"C:\\msys64\\usr\\bin\\bash.exe", "C:\\devkitPro\\msys2\\usr\\bin\\bash.exe"}) {
        if (fs::exists(candidate)) {
            return std::string(candidate);
        }
    }
    return std::nullopt;
}

std::vector<Shell> availableShells()
{
    std::vector<Shell> shells;
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    fs::path wsl = fs::path(system) / L"wsl.exe";
    if (fs::exists(wsl)) {
        shells.push_back({ShellKind::WSL, "WSL (Windows Subsystem for Linux)", pathToUtf8(wsl)});
    }
    if (auto bash = msys2Bash()) {
        shells.push_back({ShellKind::MSYS2, "MSYS2 / devkitPro shell", *bash});
    }
    return shells;
}

std::string toShellPath(const Shell& shell, const fs::path& path)
{
    // weakly_canonical also expands 8.3 short names (C:\PROGRA~1), which
    // WSL does not understand.
    std::error_code error;
    fs::path full = fs::weakly_canonical(fs::absolute(path), error);
    std::string text = pathToUtf8(error ? fs::absolute(path) : full);
    for (char& c : text) {
        if (c == '\\') {
            c = '/';
        }
    }
    if (text.size() >= 2 && text[1] == ':') {
        char drive = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
        std::string rest = text.substr(2);
        return shell.kind == ShellKind::WSL ? "/mnt/" + std::string(1, drive) + rest : "/" + std::string(1, drive) + rest;
    }
    return text;
}

std::vector<std::string> shellArgv(const Shell& shell, const fs::path& script)
{
    if (shell.kind == ShellKind::WSL) {
        return {shell.bash, "-e", "bash", "-l", toShellPath(shell, script)};
    }
    return {shell.bash, "-l", toShellPath(shell, script)};
}

#else

std::string pathToUtf8(const fs::path& path)
{
    return path.string();
}

fs::path pathFromUtf8(const std::string& text)
{
    return fs::path(text);
}

fs::path executableDir()
{
#  ifdef __APPLE__
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        return fs::weakly_canonical(buffer).parent_path();
    }
    return fs::current_path();
#  else
    std::error_code error;
    fs::path exe = fs::read_symlink("/proc/self/exe", error);
    return error ? fs::current_path() : exe.parent_path();
#  endif
}

void openInFileManager(const fs::path& path)
{
    fs::path target = fs::is_directory(path) ? path : path.parent_path();
#  ifdef __APPLE__
    std::string command = "open " + shellQuote(target.string()) + " &";
#  else
    std::string command = "xdg-open " + shellQuote(target.string()) + " >/dev/null 2>&1 &";
#  endif
    [[maybe_unused]] int ignored = std::system(command.c_str());
}

std::optional<fs::path> pickFolder(const std::string& title, const fs::path& start)
{
#  ifdef __APPLE__
    std::string command = "osascript -e 'POSIX path of (choose folder with prompt \"" + title + "\")' 2>/dev/null";
#  else
    std::string command = "zenity --file-selection --directory --title=" + shellQuote(title) +
                          (start.empty() ? "" : " --filename=" + shellQuote(start.string() + "/")) + " 2>/dev/null";
#  endif
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return std::nullopt;
    }
    char buffer[4096];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    pclose(pipe);
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    if (output.empty()) {
        return std::nullopt;
    }
    return fs::path(output);
}

std::vector<Shell> availableShells()
{
    return {{ShellKind::Native, "System bash", "/bin/bash"}};
}

std::string toShellPath(const Shell&, const fs::path& path)
{
    return fs::absolute(path).string();
}

std::vector<std::string> shellArgv(const Shell& shell, const fs::path& script)
{
    return {shell.bash, "-l", script.string()};
}

#endif

std::string shellQuote(const std::string& text)
{
    std::string out = "'";
    for (char c : text) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

bool writeFile(const fs::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    return static_cast<bool>(file);
}

std::optional<std::string> readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}
