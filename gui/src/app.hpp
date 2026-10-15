#pragma once

#include "platform.hpp"
#include "process.hpp"
#include "source_install.hpp"

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ImFont;

// Settings are kept in patcher-gui.ini next to the program.
struct Settings {
    std::string repo;
    std::string shell;
    std::string domain;
    std::string serverDir;
    int jobs = 8;
    bool buildPayload = true;
    bool buildStage1 = true;
    bool buildPatch = true;
    bool buildExploit = true;
    bool clean = false;
    std::string gameFilter;

    void load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};

// Requirements is the result of the environment check.
struct Requirements {
    bool checked = false;
    bool shellOk = false;
    std::string shellError;
    std::string devkitPPC;
    std::string gcc;
    std::string python;
    bool pythonOk = false;
    std::string cryptography;
    std::string curl;

    bool ready() const
    {
        return shellOk && !devkitPPC.empty() && pythonOk && !cryptography.empty();
    }
};

enum class Page {
    Requirements,
    Server,
    Keys,
    Build,
    Install,
    Gecko,
};

enum class TaskKind {
    None,
    Build,
    Keys,
    Clean,
};

class App
{
public:
    App();
    ~App();

    void setFonts(ImFont* regular, ImFont* bold, ImFont* mono)
    {
        m_font = regular;
        m_boldFont = bold;
        m_monoFont = mono;
    }

    // frame draws the whole window; called once per frame.
    void frame();

    // showPage opens a page by name (requirements, server, keys, build,
    // install, gecko); used by --page.
    void showPage(const std::string& name);

    // runAction starts the main action of the current page (build, server
    // test, install) once the requirements are known; used by --run.
    void runAction();

    // busy is true while the environment check or a task is running, or an
    // action requested with runAction has not started yet.
    bool busy() const
    {
        return m_checking || m_sourceInstalling || m_serverTesting || m_task != nullptr || m_pendingAction;
    }

    // lastExitCode is the exit code of the last finished task.
    int lastExitCode() const
    {
        return m_lastExitCode;
    }

private:
    // Pages.
    void drawSidebar();
    void drawRequirements();
    void drawServer();
    void drawKeys();
    void drawBuild();
    void drawInstall();
    void drawGecko();
    void drawLog(float height);

    // Helpers.
    void heading(const char* title, const char* description);
    bool repoValid() const;
    std::filesystem::path repoPath() const;
    const Shell* currentShell() const;
    bool domainValid() const;
    std::string buildCommand() const;
    std::string buildBlocker() const;
    bool makeScriptHasCRLF() const;
    void fixMakeScript();
    void findRepo();
    void markDirty()
    {
        m_settingsDirty = true;
    }

    // Background work.
    void startCheck();
    void pollCheck();
    void startSourceInstall();
    void pollSourceInstall();
    void startServerTest();
    void pollServerTest();
    bool startTask(TaskKind kind, const std::string& title, const std::string& command);
    void pollTask();
    std::string scriptPrelude() const;
    void appendLog(const std::string& line);

    // Status of the patcher folder.
    struct KeyStatus {
        bool privateKey = false;
        bool distKey = false;
        bool distMatches = false;
        bool publicKeyMatches = true;
    };
    KeyStatus keyStatus() const;

    struct BuildOutput {
        int payloads = 0;
        bool stage1 = false;
        bool key = false;
        int sbcm = 0;
        int gecko = 0;
    };
    BuildOutput buildOutput() const;
    void refreshGecko();
    bool install(std::string& message);

    Settings m_settings;
    std::filesystem::path m_settingsFile;
    bool m_settingsDirty = false;
    std::vector<Shell> m_shells;
    Page m_page = Page::Requirements;

    ImFont* m_font = nullptr;
    ImFont* m_boldFont = nullptr;
    ImFont* m_monoFont = nullptr;

    // Environment check, run on a thread.
    std::mutex m_checkMutex;
    std::thread m_checkThread;
    bool m_checking = false;
    bool m_checkDone = false;
    CaptureResult m_checkResult;
    Requirements m_requirements;

    // Source download runs away from the drawing thread.
    std::mutex m_sourceMutex;
    std::thread m_sourceThread;
    bool m_sourceInstalling = false;
    bool m_sourceDone = false;
    SourceInstallResult m_sourceResult;
    std::string m_sourceMessage;
    bool m_sourceOk = false;

    // The HTTP checks use the host OS, not WSL or MSYS2, because the server
    // may be running on Windows while the build shell has a separate network.
    std::mutex m_serverTestMutex;
    std::thread m_serverTestThread;
    bool m_serverTesting = false;
    bool m_serverTestDone = false;
    std::string m_serverTestResult;
    bool m_serverTestResultOk = false;

    // The running task (build, keys, …) and its output.
    std::unique_ptr<Process> m_task;
    TaskKind m_taskKind = TaskKind::None;
    std::string m_taskTitle;
    std::chrono::steady_clock::time_point m_taskStart;
    double m_taskSeconds = 0;
    int m_lastExitCode = 0;
    TaskKind m_lastTask = TaskKind::None;
    std::vector<std::string> m_log;
    bool m_logFollow = true;
    bool m_logScrollPending = false;

    // Results shown on the pages.
    std::string m_serverTest;
    bool m_serverTestOk = false;
    std::string m_installMessage;
    bool m_installOk = false;
    bool m_confirmKeys = false;
    bool m_pendingAction = false;

    // Widths and heights of the draggable panels.
    float m_sidebarWidth = 230.0f;
    float m_logHeight = 250.0f;
    float m_geckoWidth = 260.0f;

    // Gecko codes.
    std::vector<std::string> m_geckoFiles;
    std::string m_geckoFilter;
    std::string m_geckoSelected;
    std::string m_geckoContent;
};
