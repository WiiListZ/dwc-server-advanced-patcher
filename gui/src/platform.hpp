#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
std::wstring widen(const std::string& text);
std::string narrow(const std::wstring& text);
#endif

// pathToUtf8 converts a path for display and for writing into shell scripts.
std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path pathFromUtf8(const std::string& text);

// executableDir is the folder that contains the running GUI.
std::filesystem::path executableDir();

// openInFileManager shows a folder (or a file's folder) in Explorer, Finder
// or the desktop's file manager.
void openInFileManager(const std::filesystem::path& path);

// Opens a trusted HTTPS release URL in the user's default browser.
void openUrl(const std::string& url);

// pickFolder shows the system folder picker. Empty when cancelled or when
// the system has none.
std::optional<std::filesystem::path> pickFolder(const std::string& title, const std::filesystem::path& start);

// Shell is the bash that runs make.sh: WSL or MSYS2 on Windows, the system
// bash elsewhere.
enum class ShellKind {
    WSL,
    MSYS2,
    Native,
};

struct Shell {
    ShellKind kind = ShellKind::Native;
    std::string label;
    std::string bash;
};

// availableShells lists the bash environments found on this machine, the
// recommended one first.
std::vector<Shell> availableShells();

// toShellPath converts a local path to the path the shell sees.
std::string toShellPath(const Shell& shell, const std::filesystem::path& path);

// shellArgv runs a script file with the shell as a login shell.
std::vector<std::string> shellArgv(const Shell& shell, const std::filesystem::path& script);

// shellQuote quotes text for bash ('single quotes').
std::string shellQuote(const std::string& text);

// writeFile writes text with \n line endings. Returns false on error.
bool writeFile(const std::filesystem::path& path, const std::string& text);

// readFile returns the content of a file, or nothing when it cannot be read.
std::optional<std::string> readFile(const std::filesystem::path& path);
