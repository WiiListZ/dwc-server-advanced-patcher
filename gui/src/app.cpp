#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "app.hpp"
#include "build_domain.hpp"
#include "game_titles.hpp"
#include "version.hpp"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif

namespace fs = std::filesystem;

namespace
{

const ImVec4 colorOk{0.30f, 0.80f, 0.45f, 1.0f};
const ImVec4 colorBad{0.95f, 0.38f, 0.38f, 1.0f};
const ImVec4 colorWarn{0.98f, 0.72f, 0.25f, 1.0f};
const ImVec4 colorMuted{0.60f, 0.63f, 0.68f, 1.0f};
const ImVec4 colorAccent{0.0f, 0.63f, 0.90f, 1.0f};

std::string trim(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    return text.substr(start);
}

std::string stripAnsi(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && !std::isalpha(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            continue;
        }
        out += text[i];
    }
    return out;
}

void statusLine(bool ok, const char* label, const std::string& detail, const char* fix = nullptr)
{
    ImGui::TextColored(ok ? colorOk : colorBad, ok ? "  OK  " : "  --  ");
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    if (!detail.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(colorMuted, "%s", detail.c_str());
    }
    if (!ok && fix) {
        ImGui::Indent(56);
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(colorWarn, "%s", fix);
        ImGui::PopTextWrapPos();
        ImGui::Unindent(56);
    }
}

bool primaryButton(const char* label, bool enabled = true)
{
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, colorAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.70f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.52f, 0.78f, 1.0f));
    bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    return clicked;
}

const char* regionName(char region)
{
    switch (region) {
    case 'E':
        return "USA";
    case 'P':
        return "Europe";
    case 'J':
        return "Japan";
    case 'K':
        return "Korea";
    case 'W':
        return "Taiwan";
    case 'C':
        return "China";
    default:
        return "";
    }
}

std::string_view gameTitle(const std::string& code)
{
    if (code.size() < 5) return {};
    const std::string_view variant(code.data(), 5);
    const auto it = std::lower_bound(std::begin(kGameTitles), std::end(kGameTitles), variant,
                                     [](const auto& item, std::string_view value) { return item.first < value; });
    return it != std::end(kGameTitles) && it->first == variant ? it->second : std::string_view{};
}

std::string asciiLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool sameFileContent(const fs::path& a, const fs::path& b)
{
    auto first = readFile(a);
    auto second = readFile(b);
    return first && second && *first == *second;
}

struct HttpProbe {
    int exitCode = -1;
    std::string status;
    std::string error;
};

bool systemResolvesIPv4(const std::string& host)
{
#ifdef _WIN32
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return false;
#endif
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* addresses = nullptr;
    const bool resolved = getaddrinfo(host.c_str(), nullptr, &hints, &addresses) == 0;
    if (addresses != nullptr) freeaddrinfo(addresses);
#ifdef _WIN32
    WSACleanup();
#endif
    return resolved;
}

std::string firstUnresolvedGameHost(const std::string& domain)
{
    constexpr const char* prefixes[] = {
        "nas.", "naswii.", "gs.", "gpcm.gs.", "gpsp.gs.",
        "gamestats.gs.", "gamestats2.gs.",
        "mariokartwii.available.gs.", "mariokartwii.master.gs.",
        "mariokartwii.ms19.gs.", "mariokartwii.gamestats.gs.",
        "mariokartwii.gamestats2.gs.", "mariokartwii.sake.gs.",
        "mariokartwii.natneg1.gs.", "mariokartwii.natneg2.gs.",
        "mariokartwii.natneg3.gs."
    };
    for (const char* prefix : prefixes) {
        const std::string host = std::string(prefix) + domain;
        if (!systemResolvesIPv4(host)) return host;
    }
    return {};
}

HttpProbe probeHttp(const std::string& url)
{
#ifdef _WIN32
    const char* curl = "curl.exe";
    const char* sink = "NUL";
#else
    const char* curl = "curl";
    const char* sink = "/dev/null";
#endif
    CaptureResult result = runCapture({curl, "--silent", "--show-error", "--noproxy", "*", "--output", sink,
                                       "--write-out", "HTTP %{http_code}", "--max-time", "8", url}, executableDir());
    HttpProbe probe;
    probe.exitCode = result.exitCode;
    size_t pos = result.output.rfind("HTTP ");
    if (pos != std::string::npos && result.output.size() >= pos + 8) {
        probe.status = result.output.substr(pos + 5, 3);
    }
    probe.error = trim(result.error.empty() ? result.output : result.error);
    return probe;
}

} // namespace

// ----- Settings -------------------------------------------------------------

void Settings::load(const fs::path& file)
{
    auto content = readFile(file);
    if (!content) {
        return;
    }
    std::istringstream lines(*content);
    std::string line;
    while (std::getline(lines, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        auto flag = [&](bool& target) { target = value == "1"; };
        if (key == "repo") repo = value;
        else if (key == "shell") shell = value;
        else if (key == "domain") domain = value;
        else if (key == "serverDir") serverDir = value;
        else if (key == "jobs") jobs = std::clamp(std::atoi(value.c_str()), 1, 64);
        else if (key == "buildPayload") flag(buildPayload);
        else if (key == "buildStage1") flag(buildStage1);
        else if (key == "buildPatch") flag(buildPatch);
        else if (key == "buildExploit") flag(buildExploit);
        else if (key == "clean") flag(clean);
        else if (key == "gameFilter") gameFilter = value;
    }
}

void Settings::save(const fs::path& file) const
{
    std::ostringstream out;
    out << "# dwc-server-advanced patcher GUI settings\n";
    out << "repo=" << repo << "\n";
    out << "shell=" << shell << "\n";
    out << "domain=" << domain << "\n";
    out << "serverDir=" << serverDir << "\n";
    out << "jobs=" << jobs << "\n";
    out << "buildPayload=" << buildPayload << "\n";
    out << "buildStage1=" << buildStage1 << "\n";
    out << "buildPatch=" << buildPatch << "\n";
    out << "buildExploit=" << buildExploit << "\n";
    out << "clean=" << clean << "\n";
    out << "gameFilter=" << gameFilter << "\n";
    writeFile(file, out.str());
}

// ----- App ------------------------------------------------------------------

App::App()
{
    m_settingsFile = executableDir() / "patcher-gui.ini";
    m_settings.load(m_settingsFile);
    m_shells = availableShells();
    if (!repoValid()) {
        findRepo();
        if (m_settings.repo.empty()) {
            m_settings.repo = pathToUtf8(executableDir() / "dwc-server-advanced-patcher");
            markDirty();
        }
    }
    if (m_settings.shell.empty() && !m_shells.empty()) {
        m_settings.shell = m_shells.front().label;
    }
    refreshGecko();
    startCheck();
    startUpdateCheck();
}

App::~App()
{
    if (m_task && m_task->running()) {
        m_task->kill();
    }
    if (m_checkThread.joinable()) {
        m_checkThread.join();
    }
    if (m_sourceThread.joinable()) {
        m_sourceThread.join();
    }
    if (m_serverTestThread.joinable()) {
        m_serverTestThread.join();
    }
    if (m_updateThread.joinable()) {
        m_updateThread.join();
    }
    m_settings.save(m_settingsFile);
}

fs::path App::repoPath() const
{
    return pathFromUtf8(m_settings.repo);
}

bool App::repoValid() const
{
    if (m_settings.repo.empty()) {
        return false;
    }
    fs::path repo = repoPath();
    return fs::exists(repo / "make.sh") && fs::exists(repo / "gamedefs.csv");
}

// findRepo looks for the patcher folder around the program and the current
// folder: the GUI is usually built inside the repository.
void App::findRepo()
{
    for (fs::path start : {executableDir(), fs::current_path()}) {
        for (fs::path dir = start; !dir.empty(); dir = dir.parent_path()) {
            if (fs::exists(dir / "make.sh") && fs::exists(dir / "gamedefs.csv")) {
                m_settings.repo = pathToUtf8(dir);
                markDirty();
                return;
            }
            if (dir == dir.parent_path()) {
                break;
            }
        }
    }
}

const Shell* App::currentShell() const
{
    for (const Shell& shell : m_shells) {
        if (shell.label == m_settings.shell) {
            return &shell;
        }
    }
    return m_shells.empty() ? nullptr : &m_shells.front();
}

bool App::domainValid() const
{
    const std::string& domain = m_settings.domain;
    if (domain.empty() || domain.size() > 200 || domain.front() == '.' || domain.back() == '.') {
        return false;
    }
    return std::all_of(domain.begin(), domain.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-';
    });
}

// scriptPrelude prepares the environment make.sh expects: devkitPPC and a
// `python` command (Ubuntu only has python3).
std::string App::scriptPrelude() const
{
    std::string prelude = "#!/bin/bash\n";
    prelude += "if [ -z \"$DEVKITPRO\" ] && [ -d /opt/devkitpro ]; then export DEVKITPRO=/opt/devkitpro; fi\n";
    prelude += "if [ -z \"$DEVKITPPC\" ] && [ -d \"$DEVKITPRO/devkitPPC\" ]; then export DEVKITPPC=\"$DEVKITPRO/devkitPPC\"; fi\n";
    prelude += "if ! command -v python >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then\n";
    prelude += "  shim=\"${TMPDIR:-/tmp}/dwc-patcher-gui\"; mkdir -p \"$shim\"\n";
    prelude += "  ln -sf \"$(command -v python3)\" \"$shim/python\"; export PATH=\"$shim:$PATH\"\n";
    prelude += "fi\n";
    if (const Shell* shell = currentShell(); shell && repoValid()) {
        prelude += "cd " + shellQuote(toShellPath(*shell, repoPath())) + " || exit 1\n";
    }
    return prelude;
}

void App::appendLog(const std::string& line)
{
    m_log.push_back(stripAnsi(line));
    m_logScrollPending = true;
    if (m_log.size() > 20000) {
        m_log.erase(m_log.begin(), m_log.begin() + 5000);
    }
}

bool App::makeScriptHasCRLF() const
{
    auto content = readFile(repoPath() / "make.sh");
    return content && content->find("\r\n") != std::string::npos;
}

void App::fixMakeScript()
{
    fs::path file = repoPath() / "make.sh";
    auto content = readFile(file);
    if (!content) {
        return;
    }
    std::string fixed;
    fixed.reserve(content->size());
    for (size_t i = 0; i < content->size(); ++i) {
        if ((*content)[i] == '\r' && i + 1 < content->size() && (*content)[i + 1] == '\n') {
            continue;
        }
        fixed += (*content)[i];
    }
    if (!writeFile(file, fixed)) {
        appendLog("Cannot write " + pathToUtf8(file));
    }
}

// ----- Environment check ------------------------------------------------------

void App::startCheck()
{
    if (m_checking) {
        return;
    }
    const Shell* shell = currentShell();
    if (!shell) {
        m_requirements = {};
        m_requirements.checked = true;
#ifdef _WIN32
        m_requirements.shellError = "Neither WSL nor MSYS2 was found.";
#else
        m_requirements.shellError = "bash was not found.";
#endif
        return;
    }

    std::string script = scriptPrelude();
    script += "echo \"shell=ok\"\n";
    script += "gcc=\"$DEVKITPPC/bin/powerpc-eabi-gcc\"\n";
    script += "if [ -x \"$gcc\" ]; then echo \"devkitppc=$DEVKITPPC\"; echo \"gcc=$(\"$gcc\" --version | head -1)\"; fi\n";
    script += "echo \"python=$(python --version 2>&1 | head -1)\"\n";
    script += "echo \"pythonok=$(python -c 'import sys; print(int(sys.version_info >= (3, 11)))' 2>/dev/null)\"\n";
    script += "echo \"cryptography=$(python -c 'import cryptography; print(cryptography.__version__)' 2>/dev/null)\"\n";
    script += "echo \"curl=$(command -v curl)\"\n";

    fs::path scriptFile = (repoValid() ? repoPath() : executableDir()) / ".patcher-gui-check.sh";
    if (!writeFile(scriptFile, script)) {
        m_requirements = {};
        m_requirements.checked = true;
        m_requirements.shellError = "Cannot write " + pathToUtf8(scriptFile);
        return;
    }

    m_checking = true;
    m_checkDone = false;
    std::vector<std::string> argv = shellArgv(*shell, scriptFile);
    if (m_checkThread.joinable()) {
        m_checkThread.join();
    }
    m_checkThread = std::thread([this, argv, scriptFile] {
        CaptureResult result = runCapture(argv, scriptFile.parent_path());
        std::error_code ignored;
        fs::remove(scriptFile, ignored);
        std::lock_guard lock(m_checkMutex);
        m_checkResult = result;
        m_checkDone = true;
    });
}

void App::pollCheck()
{
    if (!m_checking) {
        return;
    }
    CaptureResult result;
    {
        std::lock_guard lock(m_checkMutex);
        if (!m_checkDone) {
            return;
        }
        result = m_checkResult;
    }
    m_checking = false;

    Requirements req;
    req.checked = true;
    std::map<std::string, std::string> values;
    std::istringstream lines(result.output);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(stripAnsi(line));
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            values[line.substr(0, eq)] = trim(line.substr(eq + 1));
        }
    }
    req.shellOk = values["shell"] == "ok";
    if (!req.shellOk) {
        req.shellError = !result.error.empty() ? result.error : trim(result.output);
        if (req.shellError.empty()) {
            req.shellError = "The shell did not run (exit code " + std::to_string(result.exitCode) + ").";
        }
    }
    req.devkitPPC = values["devkitppc"];
    req.gcc = values["gcc"];
    req.python = values["python"];
    req.pythonOk = values["pythonok"] == "1";
    req.cryptography = values["cryptography"];
    req.curl = values["curl"];
    m_requirements = req;
}

void App::startSourceInstall()
{
    if (m_sourceInstalling || m_checking || m_task) {
        return;
    }
    if (m_settings.repo.empty()) {
        m_sourceMessage = "Choose a patcher folder first.";
        m_sourceOk = false;
        return;
    }
    if (m_sourceThread.joinable()) {
        m_sourceThread.join();
    }
    const fs::path target = repoPath();
    m_sourceMessage.clear();
    m_sourceInstalling = true;
    m_sourceDone = false;
    m_sourceThread = std::thread([this, target] {
        SourceInstallResult result = installPatcherSource(target);
        std::lock_guard lock(m_sourceMutex);
        m_sourceResult = std::move(result);
        m_sourceDone = true;
    });
}

void App::pollSourceInstall()
{
    if (!m_sourceInstalling) {
        return;
    }
    SourceInstallResult result;
    {
        std::lock_guard lock(m_sourceMutex);
        if (!m_sourceDone) {
            return;
        }
        result = m_sourceResult;
    }
    m_sourceThread.join();
    m_sourceInstalling = false;
    m_sourceOk = result.ok;
    m_sourceMessage = result.message;
    appendLog(result.message);
    if (result.ok) {
        refreshGecko();
        startCheck();
    }
}

void App::startServerTest()
{
    if (!domainValid() || m_serverTesting || m_task) {
        return;
    }
    if (m_serverTestThread.joinable()) {
        m_serverTestThread.join();
    }
    const std::string domain = m_settings.domain;
    m_serverTest.clear();
    m_serverTestOk = false;
    m_serverTesting = true;
    m_serverTestDone = false;
    m_serverTestThread = std::thread([this, domain] {
        std::string message;
        bool ok = false;
        if (domain == "localhost") {
            const HttpProbe server = probeHttp("http://127.0.0.1/");
            if (server.exitCode != 0) {
                message = "The server is not reachable at 127.0.0.1:80 from this computer. " + server.error;
            } else {
                message = "Server running at 127.0.0.1:80 (HTTP " + server.status + "). ";
                const HttpProbe stage = probeHttp("http://127.0.0.1/w0");
                if (stage.status == "404") {
                    message += "/w0 is missing: build and install stage1.bin. ";
                } else if (stage.status == "200") {
                    message += "Stage 1 is installed. ";
                } else {
                    message += "/w0 returned HTTP " + stage.status + ". ";
                }
                const HttpProbe stageV1 = probeHttp("http://127.0.0.1/w1");
                if (stageV1.status == "404") {
                    message += "/w1 is missing: install stage1v1.bin. ";
                }
                const std::string missingHost = firstUnresolvedGameHost(domain);
                if (!missingHost.empty()) {
                    message += "The operating system cannot resolve " + missingHost + ". Map all game hostnames to 127.0.0.1 in the hosts file; curl alone can give a false positive.";
                } else {
                    const HttpProbe name = probeHttp("http://nas.localhost/w1");
                    message += "nas.localhost resolves through Windows (HTTP " + name.status + ").";
                    ok = name.exitCode == 0 && name.status == "200" && stage.status == "200" && stageV1.status == "200";
                }
            }
        } else {
            const std::string host = "nas." + domain;
            const std::string missingHost = firstUnresolvedGameHost(domain);
            if (!missingHost.empty()) {
                message = missingHost + " does not resolve through the operating system. Point all game hostnames to the server IP.";
            } else {
                const HttpProbe response = probeHttp("http://" + host + "/w1");
                if (response.exitCode == 7) {
                    message = host + " resolves, but port 80 refuses the connection. Check the server address and firewall.";
                } else if (response.exitCode != 0) {
                    message = "Cannot reach " + host + ": " + response.error;
                } else if (response.status == "200") {
                    const HttpProbe stageV0 = probeHttp("http://" + host + "/w0");
                    message = host + " serves /w1 (HTTP 200). /w0 returned HTTP " + stageV0.status + ".";
                    ok = stageV0.status == "200";
                } else if (response.status == "404") {
                    message = host + " reaches the server, but /w1 is missing. Install stage1v1.bin.";
                } else {
                    message = host + " returned HTTP " + response.status + ".";
                }
            }
        }
        std::lock_guard lock(m_serverTestMutex);
        m_serverTestResult = std::move(message);
        m_serverTestResultOk = ok;
        m_serverTestDone = true;
    });
}

void App::pollServerTest()
{
    if (!m_serverTesting) {
        return;
    }
    {
        std::lock_guard lock(m_serverTestMutex);
        if (!m_serverTestDone) {
            return;
        }
        m_serverTest = m_serverTestResult;
        m_serverTestOk = m_serverTestResultOk;
    }
    m_serverTestThread.join();
    m_serverTesting = false;
    appendLog(m_serverTest);
}

void App::startUpdateCheck()
{
    if (m_updateChecking) return;
    if (m_updateThread.joinable()) m_updateThread.join();
    m_updateChecking = true;
    m_updateDone = false;
    m_lastUpdateCheck = std::chrono::steady_clock::now();
    m_updateThread = std::thread([this] {
        UpdateCheckResult result = checkForUpdates(kPatcherVersion);
        std::lock_guard lock(m_updateMutex);
        m_updateResult = std::move(result);
        m_updateDone = true;
    });
}

void App::pollUpdateCheck()
{
    if (!m_updateChecking) return;
    {
        std::lock_guard lock(m_updateMutex);
        if (!m_updateDone) return;
        if (m_updateResult.available && m_updateResult.release.version != m_lastNotifiedVersion) {
            m_lastNotifiedVersion = m_updateResult.release.version;
            m_updateNoticePending = true;
        }
    }
    m_updateThread.join();
    m_updateChecking = false;
}

// ----- Tasks ------------------------------------------------------------------

bool App::startTask(TaskKind kind, const std::string& title, const std::string& command)
{
    if (m_task && m_task->running()) {
        return false;
    }
    const Shell* shell = currentShell();
    if (!shell || !repoValid()) {
        appendLog("Cannot start: choose the patcher folder and a shell first.");
        return false;
    }

    fs::path scriptFile = repoPath() / ".patcher-gui-run.sh";
    std::string script = scriptPrelude() + "echo " + shellQuote("$ " + command) + "\n" + command + "\n";
    if (!writeFile(scriptFile, script)) {
        appendLog("Cannot write " + pathToUtf8(scriptFile));
        return false;
    }

    m_log.clear();
    m_logFollow = true;
    m_task = std::make_unique<Process>();
    if (!m_task->start(shellArgv(*shell, scriptFile), repoPath())) {
        appendLog(m_task->error());
        m_task.reset();
        return false;
    }
    m_taskKind = kind;
    m_taskTitle = title;
    m_taskStart = std::chrono::steady_clock::now();
    m_serverTest.clear();
    m_installOk = false;
    m_installMessage.clear();
    return true;
}

void App::pollTask()
{
    if (!m_task) {
        return;
    }
    for (const std::string& line : m_task->takeLines()) {
        appendLog(line);
    }
    if (m_task->running()) {
        m_taskSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_taskStart).count();
        return;
    }
    m_task->wait();
    for (const std::string& line : m_task->takeLines()) {
        appendLog(line);
    }
    m_lastExitCode = m_task->exitCode();
    m_lastTask = m_taskKind;
    appendLog(m_lastExitCode == 0 ? "Finished." : "Failed with exit code " + std::to_string(m_lastExitCode) + ".");

    std::error_code ignored;
    fs::remove(repoPath() / ".patcher-gui-run.sh", ignored);
    m_task.reset();
    m_taskKind = TaskKind::None;
    refreshGecko();
}

// ----- Status of the build ----------------------------------------------------

App::KeyStatus App::keyStatus() const
{
    KeyStatus status;
    if (!repoValid()) {
        return status;
    }
    fs::path repo = repoPath();
    status.privateKey = fs::exists(repo / "misc" / "private-key-OUT.pem");
    status.distKey = fs::exists(repo / "dist" / "private-key.pem");
    status.distMatches = status.privateKey && status.distKey &&
                         sameFileContent(repo / "misc" / "private-key-OUT.pem", repo / "dist" / "private-key.pem");
    fs::path generated = repo / "misc" / "wwfcPayloadPublicKey-OUT.hpp";
    std::error_code error;
    if (fs::exists(generated) && fs::file_size(generated, error) > 0) {
        status.publicKeyMatches = sameFileContent(generated, repo / "include" / "wwfcPayloadPublicKey.hpp");
    }
    return status;
}

App::BuildOutput App::buildOutput() const
{
    BuildOutput output;
    if (!repoValid()) {
        return output;
    }
    fs::path repo = repoPath();
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(repo / "dist" / "binary", error)) {
        if (entry.path().extension() == ".bin") {
            ++output.payloads;
        }
    }
    output.stage1 = fs::exists(repo / "dist" / "stage1.bin");
    output.key = fs::exists(repo / "dist" / "private-key.pem");
    for (const auto& entry : fs::directory_iterator(repo / "exploit" / "sbcm", error)) {
        if (entry.path().extension() == ".bin") {
            ++output.sbcm;
        }
    }
    output.gecko = static_cast<int>(m_geckoFiles.size());
    return output;
}

void App::refreshGecko()
{
    m_geckoFiles.clear();
    if (!repoValid()) {
        m_geckoSelected.clear();
        m_geckoContent.clear();
        return;
    }
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(repoPath() / "patch" / "build", error)) {
        if (entry.path().extension() == ".txt") {
            m_geckoFiles.push_back(pathToUtf8(entry.path().stem()));
        }
    }
    std::sort(m_geckoFiles.begin(), m_geckoFiles.end());
    if (!m_geckoSelected.empty()) {
        if (std::binary_search(m_geckoFiles.begin(), m_geckoFiles.end(), m_geckoSelected)) {
            m_geckoContent = readFile(repoPath() / "patch" / "build" / pathFromUtf8(m_geckoSelected + ".txt")).value_or("");
        } else {
            m_geckoSelected.clear();
            m_geckoContent.clear();
        }
    }
}

// install copies the build into the server's payload folder, as described in
// the README: dist/ → payload/, exploit/sbcm/*.bin → payload/sbcm/.
bool App::install(std::string& message)
{
    if (!repoValid()) {
        message = "Choose the patcher folder first.";
        return false;
    }
    if (!domainValid()) {
        message = "Set a valid server domain before installing.";
        return false;
    }
    fs::path server = pathFromUtf8(m_settings.serverDir);
    if (m_settings.serverDir.empty() || !fs::is_directory(server)) {
        message = "Choose the server folder (the one with config.yml).";
        return false;
    }
    fs::path repo = repoPath();
    fs::path dist = repo / "dist";
    if (!fs::exists(dist / "stage1.bin") || !fs::exists(dist / "private-key.pem")) {
        message = "dist/ is incomplete: build stage 1 and the payloads first.";
        return false;
    }
    if (const std::string problem = buildDomainProblem(); !problem.empty()) {
        message = problem + " Run Build again before installing.";
        return false;
    }
    for (const std::string& name : m_geckoFiles) {
        const auto code = readFile(repo / "patch" / "build" / pathFromUtf8(name + ".txt"));
        if (!code || asciiLower(geckoServerDomain(*code)) != asciiLower(m_settings.domain)) {
            message = "Gecko code " + name + " is missing or uses another domain. Rebuild all Gecko codes before installing.";
            return false;
        }
    }

    fs::path payload = server / "payload";
    std::error_code error;
    fs::create_directories(payload / "sbcm", error);
    if (error) {
        message = "Cannot create " + pathToUtf8(payload) + ": " + error.message();
        return false;
    }

    int copied = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dist, error)) {
        fs::path target = payload / fs::relative(entry.path(), dist);
        if (entry.is_directory()) {
            fs::create_directories(target, error);
            continue;
        }
        fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, error);
        if (error) {
            message = "Cannot copy " + pathToUtf8(entry.path()) + ": " + error.message();
            return false;
        }
        ++copied;
    }
    // Gecko patches request /w1; the build currently emits one stage 1 binary.
    // Preserve an explicitly built v1 file if a future patcher supplies one.
    if (!fs::exists(dist / "stage1v1.bin")) {
        fs::copy_file(dist / "stage1.bin", payload / "stage1v1.bin", fs::copy_options::overwrite_existing, error);
        if (error) {
            message = "Cannot install stage1v1.bin: " + error.message();
            return false;
        }
        ++copied;
    }
    int exploits = 0;
    for (const auto& entry : fs::directory_iterator(repo / "exploit" / "sbcm", error)) {
        if (entry.path().extension() != ".bin") {
            continue;
        }
        fs::copy_file(entry.path(), payload / "sbcm" / entry.path().filename(), fs::copy_options::overwrite_existing, error);
        if (error) {
            message = "Cannot copy " + pathToUtf8(entry.path()) + ": " + error.message();
            return false;
        }
        ++exploits;
    }
    message = "Installed " + std::to_string(copied) + " files and " + std::to_string(exploits) + " SBCM exploits into " +
              pathToUtf8(payload) + ". The server reads them on every request, so no restart is needed.";
    return true;
}

// ----- Drawing ----------------------------------------------------------------

void App::showPage(const std::string& name)
{
    if (name == "requirements") m_page = Page::Requirements;
    else if (name == "server") m_page = Page::Server;
    else if (name == "keys") m_page = Page::Keys;
    else if (name == "build") m_page = Page::Build;
    else if (name == "install") m_page = Page::Install;
    else if (name == "gecko") {
        m_page = Page::Gecko;
        refreshGecko();
        if (!m_geckoFiles.empty()) {
            auto it = std::find(m_geckoFiles.begin(), m_geckoFiles.end(), "RMCPD00");
            m_geckoSelected = it != m_geckoFiles.end() ? *it : m_geckoFiles.front();
            m_geckoContent = readFile(repoPath() / "patch" / "build" / pathFromUtf8(m_geckoSelected + ".txt")).value_or("");
        }
    }
}

void App::heading(const char* title, const char* description)
{
    if (m_boldFont) {
        ImGui::PushFont(m_boldFont);
    }
    ImGui::TextUnformatted(title);
    if (m_boldFont) {
        ImGui::PopFont();
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(colorMuted, "%s", description);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void App::frame()
{
    pollCheck();
    pollSourceInstall();
    pollServerTest();
    pollUpdateCheck();
    pollTask();
    if (!m_updateChecking && std::chrono::steady_clock::now() - m_lastUpdateCheck >= std::chrono::hours(6)) {
        startUpdateCheck();
    }
    if (m_pendingAction && !m_checking && !m_task && !m_serverTesting) {
        m_pendingAction = false;
        if (m_page == Page::Build && buildBlocker().empty()) {
            startTask(TaskKind::Build, "Building", buildCommand());
        } else if (m_page == Page::Server && domainValid()) {
            startServerTest();
        } else if (m_page == Page::Install) {
            m_installOk = install(m_installMessage);
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Patcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (m_updateNoticePending) {
        ImGui::OpenPopup("Patcher update available");
        m_updateNoticePending = false;
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float divider = 7.0f;
    const bool showLog = m_task || !m_log.empty();
    m_sidebarWidth = std::clamp(m_sidebarWidth, 165.0f, std::max(165.0f, available.x - 320.0f));
    if (showLog) {
        m_logHeight = std::clamp(m_logHeight, 120.0f, std::max(120.0f, available.y - 180.0f));
    }
    const float verticalSpacing = ImGui::GetStyle().ItemSpacing.y;
    const float pageHeight = showLog
                                 ? std::max(120.0f, available.y - m_logHeight - divider - 2.0f * verticalSpacing)
                                 : available.y;

    ImGui::BeginChild("sidebar", ImVec2(m_sidebarWidth, pageHeight), ImGuiChildFlags_Borders);
    drawSidebar();
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("##sidebar-divider", ImVec2(divider, pageHeight));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) m_sidebarWidth += ImGui::GetIO().MouseDelta.x;
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("page", ImVec2(0, pageHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (m_page) {
    case Page::Requirements:
        drawRequirements();
        break;
    case Page::Server:
        drawServer();
        break;
    case Page::Keys:
        drawKeys();
        break;
    case Page::Build:
        drawBuild();
        break;
    case Page::Install:
        drawInstall();
        break;
    case Page::Gecko:
        drawGecko();
        break;
    }
    ImGui::EndChild();

    if (showLog) {
        ImGui::InvisibleButton("##log-divider", ImVec2(-1, divider));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive()) m_logHeight -= ImGui::GetIO().MouseDelta.y;
        // The remaining region accounts for both ImGui item gaps, so the
        // log's lower border stays inside the application window.
        drawLog(std::max(1.0f, ImGui::GetContentRegionAvail().y));
    }
    ImGui::SetNextWindowSize(ImVec2(430, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Patcher update available", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Version %s is available", m_updateResult.release.version.c_str());
        ImGui::TextColored(colorMuted, "Installed: v%s", kPatcherVersion);
        ImGui::TextWrapped("Open the release page to download the new patcher for your platform. Your patcher folder and settings stay where they are.");
        if (primaryButton("Open release page")) {
            openUrl(m_updateResult.release.url);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Later")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();

    if (m_settingsDirty) {
        m_settings.save(m_settingsFile);
        m_settingsDirty = false;
    }
}

void App::drawSidebar()
{
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float unit = ImGui::GetFontSize() / 17.0f;
    auto point = [&](float x, float y) { return ImVec2(top.x + x * unit, top.y + y * unit); };
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddCircleFilled(point(16, 16), 16 * unit, IM_COL32(166, 224, 248, 255));
    draw->AddCircleFilled(point(16, 16), 13 * unit, IM_COL32(0, 160, 230, 255));
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    for (float radius : {4.0f, 7.2f, 10.4f}) {
        draw->PathClear();
        draw->PathArcTo(point(11, 16), radius * unit, -3.14159265f / 4, 3.14159265f / 4);
        draw->PathStroke(white, 0, 2.2f * unit);
        const float diagonal = radius * 0.70710678f;
        draw->AddCircleFilled(point(11 + diagonal, 16 - diagonal), 1.1f * unit, white);
        draw->AddCircleFilled(point(11 + diagonal, 16 + diagonal), 1.1f * unit, white);
    }
    draw->AddCircleFilled(point(11, 16), 1.9f * unit, white);
    ImGui::Dummy(ImVec2(32 * unit, 32 * unit));
    ImGui::Spacing();
    if (m_boldFont) {
        ImGui::PushFont(m_boldFont);
    }
    ImGui::TextColored(colorAccent, "DWC Server Advanced");
    if (m_boldFont) {
        ImGui::PopFont();
    }
    ImGui::TextColored(colorMuted, "Client patcher");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    KeyStatus keys = keyStatus();
    BuildOutput output = buildOutput();
    struct Item {
        Page page;
        const char* label;
        bool done;
    };
    const Item items[] = {
        {Page::Requirements, "1  Requirements", m_requirements.ready() && repoValid() && !makeScriptHasCRLF()},
        {Page::Server, "2  Server", domainValid()},
        {Page::Keys, "3  Signing key", keys.privateKey},
        {Page::Build, "4  Build", output.stage1 && output.payloads > 0},
        {Page::Install, "5  Install", m_installOk},
        {Page::Gecko, "6  Gecko codes", output.gecko > 0},
    };
    for (const Item& item : items) {
        ImGui::PushID(static_cast<int>(item.page));
        if (ImGui::Selectable(item.label, m_page == item.page, 0, ImVec2(0, 30))) {
            m_page = item.page;
            if (item.page == Page::Gecko) {
                refreshGecko();
            }
        }
        if (item.done) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 6);
            ImGui::TextColored(colorOk, "*");
        }
        ImGui::PopID();
    }

    const float footerTop = ImGui::GetWindowHeight() - 145.0f;
    if (ImGui::GetCursorPosY() < footerTop) ImGui::SetCursorPosY(footerTop);
    ImGui::Separator();
    ImGui::TextColored(colorMuted, "Patcher v%s", kPatcherVersion);
    ImGui::BeginDisabled(m_updateChecking);
    if (ImGui::SmallButton(m_updateChecking ? "Checking updates..." : "Check for updates")) startUpdateCheck();
    ImGui::EndDisabled();
    if (m_updateResult.available) {
        ImGui::TextColored(colorWarn, "New: %s", m_updateResult.release.version.c_str());
        if (ImGui::SmallButton("Download update")) openUrl(m_updateResult.release.url);
    } else if (m_updateResult.ok) {
        ImGui::TextColored(colorOk, "Up to date");
    } else if (!m_updateChecking && !m_updateResult.error.empty()) {
        ImGui::TextColored(colorMuted, "Update check unavailable");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_updateResult.error.c_str());
    }
    ImGui::Separator();
    if (m_task) {
        ImGui::TextColored(colorAccent, "%s...", m_taskTitle.c_str());
        ImGui::TextColored(colorMuted, "%.0f s", m_taskSeconds);
    } else if (m_lastTask != TaskKind::None) {
        ImGui::TextColored(m_lastExitCode == 0 ? colorOk : colorBad, m_lastExitCode == 0 ? "Last task finished" : "Last task failed");
    } else {
        ImGui::TextColored(colorMuted, "Idle");
    }
}

void App::drawRequirements()
{
    heading("Requirements", "The patcher is built with its own make.sh, which needs bash, devkitPPC and Python 3.11+ with the cryptography module. This page checks them and the patcher folder.");

    ImGui::SeparatorText("Patcher folder");
    std::string repo = m_settings.repo;
    ImGui::BeginDisabled(m_sourceInstalling);
    ImGui::SetNextItemWidth(-110);
    if (ImGui::InputText("##repo", &repo)) {
        m_settings.repo = repo;
        markDirty();
        refreshGecko();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...##repo")) {
        if (auto chosen = pickFolder("Choose the dwc-server-advanced-patcher folder", repoPath())) {
            m_settings.repo = pathToUtf8(*chosen);
            markDirty();
            refreshGecko();
            startCheck();
        }
    }
    ImGui::EndDisabled();
    statusLine(repoValid(), "Patcher folder", repoValid() ? "make.sh and gamedefs.csv found" : "",
               "Choose a folder, then download this patcher's source into it. You can use the program's folder.");
    ImGui::BeginDisabled(m_sourceInstalling || m_checking || m_task != nullptr);
    if (ImGui::Button("Use program folder")) {
        m_settings.repo = pathToUtf8(executableDir());
        markDirty();
        refreshGecko();
        startCheck();
    }
    if (!repoValid()) {
        ImGui::SameLine();
        if (primaryButton("Download patcher here", !m_settings.repo.empty() && !m_checking)) {
            startSourceInstall();
        }
    }
    ImGui::EndDisabled();
    if (m_sourceInstalling) {
        ImGui::TextColored(colorAccent, "Downloading and installing the patcher source...");
    } else if (!m_sourceMessage.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(m_sourceOk ? colorOk : colorBad, "%s", m_sourceMessage.c_str());
        ImGui::PopTextWrapPos();
    }
    if (repoValid() && makeScriptHasCRLF()) {
        statusLine(false, "make.sh line endings", "Windows (CRLF)",
                   "Git checked make.sh out with Windows line endings, which bash cannot run. Fix converts it to Unix line endings; the repository's .gitattributes keeps it that way.");
        ImGui::Indent(56);
        if (primaryButton("Fix line endings")) {
            fixMakeScript();
        }
        ImGui::Unindent(56);
    }

    ImGui::SeparatorText("Shell");
    if (m_shells.empty()) {
#ifdef _WIN32
        statusLine(false, "bash", "", "Install WSL (wsl --install in an administrator terminal, then install devkitPPC inside it) or the devkitPro installer, which comes with MSYS2.");
#else
        statusLine(false, "bash", "", "Install bash.");
#endif
    } else {
        ImGui::SetNextItemWidth(320);
        if (ImGui::BeginCombo("##shell", currentShell() ? currentShell()->label.c_str() : "")) {
            for (const Shell& shell : m_shells) {
                if (ImGui::Selectable(shell.label.c_str(), currentShell() == &shell)) {
                    m_settings.shell = shell.label;
                    markDirty();
                    startCheck();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextColored(colorMuted, "runs make.sh");
    }

    ImGui::SeparatorText("Tools");
    if (m_checking) {
        ImGui::TextColored(colorAccent, "Checking...");
    } else if (m_requirements.checked) {
        const Requirements& r = m_requirements;
        statusLine(r.shellOk, "bash", r.shellOk ? (currentShell() ? currentShell()->label : "") : r.shellError,
                   "The shell could not be started. With WSL, open a terminal and run wsl once to finish its setup.");
        if (r.shellOk) {
            statusLine(!r.devkitPPC.empty(), "devkitPPC", r.devkitPPC.empty() ? "" : r.gcc,
                       "Install devkitPPC from devkitPro (https://devkitpro.org/wiki/Getting_Started). In WSL: install devkitpro-pacman, then sudo dkp-pacman -S devkitPPC.");
            statusLine(r.pythonOk, "Python 3.11+", r.python,
                       "Install Python 3.11 or newer (sudo apt install python3 in WSL or Ubuntu).");
            statusLine(!r.cryptography.empty(), "cryptography", r.cryptography,
                       "Install the module: python3 -m pip install cryptography (or sudo apt install python3-cryptography).");
            statusLine(!r.curl.empty(), "curl", r.curl.empty() ? "optional for builds" : r.curl,
                       "The shell uses curl to test the server. Source download also needs system curl and tar.");
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("Check again")) {
        startCheck();
    }
    if (m_requirements.ready() && repoValid()) {
        ImGui::SameLine();
        if (primaryButton("Next: server")) {
            m_page = Page::Server;
        }
    }
}

void App::drawServer()
{
    heading("Server", "The host name is built into the patch. It needs DNS records that point to your server. Do not enter a bare IP address or localhost for other devices.");

    ImGui::SeparatorText("Domain");
    ImGui::BeginDisabled(m_serverTesting);
    ImGui::SetNextItemWidth(360);
    if (ImGui::InputTextWithHint("##domain", "wfc.example.com", &m_settings.domain, ImGuiInputTextFlags_CharsNoBlank)) {
        markDirty();
        m_serverTest.clear();
        m_installOk = false;
        m_installMessage.clear();
    }
    ImGui::EndDisabled();
    if (!m_settings.domain.empty() && !domainValid()) {
        ImGui::TextColored(colorBad, "Use only letters, digits, dots and hyphens.");
    } else if (domainValid()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(colorMuted, "Games will connect to nas.%s, gpcm.gs.%s and other hostnames.",
                           m_settings.domain.c_str(), m_settings.domain.c_str());
        if (m_settings.domain == "localhost") {
            ImGui::TextColored(colorWarn, "localhost only refers to this computer. The game also needs nas.localhost and gs.localhost to resolve.");
            if (ImGui::Button("Copy local hosts entries")) {
                ImGui::SetClipboardText(
                    "127.0.0.1 nas.localhost naswii.localhost gs.localhost\n"
                    "127.0.0.1 gpcm.gs.localhost gpsp.gs.localhost gamestats.gs.localhost gamestats2.gs.localhost\n"
                    "127.0.0.1 mariokartwii.available.gs.localhost mariokartwii.master.gs.localhost mariokartwii.ms19.gs.localhost\n"
                    "127.0.0.1 mariokartwii.gamestats.gs.localhost mariokartwii.gamestats2.gs.localhost mariokartwii.sake.gs.localhost\n"
                    "127.0.0.1 mariokartwii.natneg1.gs.localhost mariokartwii.natneg2.gs.localhost mariokartwii.natneg3.gs.localhost\n");
            }
            ImGui::TextColored(colorMuted, "Paste into the system hosts file with administrator rights, then test the server.");
        } else if (std::all_of(m_settings.domain.begin(), m_settings.domain.end(), [](unsigned char c) {
                       return std::isdigit(c) || c == '.';
                   })) {
            ImGui::TextColored(colorWarn, "A bare IP becomes nas.<IP> in the patch. Use a DNS name pointing to the IP instead.");
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(!domainValid() || m_task != nullptr || m_serverTesting);
    if (ImGui::Button(m_serverTesting ? "Testing..." : "Test the server")) {
        startServerTest();
    }
    ImGui::EndDisabled();
    if (!m_serverTest.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(m_serverTestOk ? colorOk : colorWarn, "%s", m_serverTest.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::SeparatorText("Server folder");
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(colorMuted, "The folder of the server with config.yml (Panel -> Server -> Files shows it). The Install step copies the build into its payload/ folder. Leave it empty if the server runs on another machine: then upload dist/ in the panel's Files tab.");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(-110);
    if (ImGui::InputText("##serverdir", &m_settings.serverDir)) {
        markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...##server")) {
        if (auto chosen = pickFolder("Choose the dwc-server-advanced server folder", pathFromUtf8(m_settings.serverDir))) {
            m_settings.serverDir = pathToUtf8(*chosen);
            markDirty();
        }
    }
    if (!m_settings.serverDir.empty()) {
        fs::path server = pathFromUtf8(m_settings.serverDir);
        bool hasConfig = fs::exists(server / "config.yml") || fs::exists(server / "config.yaml");
        statusLine(hasConfig, "Server folder", hasConfig ? "config.yml found" : "", "No config.yml in this folder: is it the server folder?");
    }
    ImGui::TextColored(colorMuted, "HTTPS certificates are generated by the server. Open Panel > Server > Settings > HTTPS to create missing files.");
    ImGui::Spacing();
    if (primaryButton("Next: signing key", domainValid())) {
        m_page = Page::Keys;
    }
}

void App::drawKeys()
{
    heading("Signing key", "The server signs every game payload with a private key, and stage 1 only accepts payloads signed with the matching public key. The first build creates the key pair; keep using the same one afterwards.");

    KeyStatus keys = keyStatus();
    statusLine(keys.privateKey, "Private key", keys.privateKey ? "misc/private-key-OUT.pem" : "",
               "No key yet. The first build creates one automatically.");
    if (keys.privateKey) {
        statusLine(keys.publicKeyMatches, "Public key in the build", keys.publicKeyMatches ? "include/wwfcPayloadPublicKey.hpp" : "",
                   "include/wwfcPayloadPublicKey.hpp does not match your key: clients would reject the payloads. Generate the keys again, or copy misc/wwfcPayloadPublicKey-OUT.hpp there.");
        if (keys.distKey) {
            statusLine(keys.distMatches, "Key in dist/", keys.distMatches ? "dist/private-key.pem matches" : "",
                       "dist/private-key.pem is from another key. Build again before installing.");
        }
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, colorWarn);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("Keep misc/private-key-OUT.pem and dist/private-key.pem private: anyone who has them can run code on your players' consoles. They are excluded from git.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::BeginDisabled(m_task != nullptr || !m_requirements.ready());
    if (ImGui::Button(keys.privateKey ? "Generate a new key pair..." : "Generate the key pair")) {
        if (keys.privateKey) {
            m_confirmKeys = true;
            ImGui::OpenPopup("Replace the key?");
        } else {
            startTask(TaskKind::Keys, "Generating keys", "bash ./make.sh --keys");
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (primaryButton("Next: build")) {
        m_page = Page::Build;
    }

    ImGui::SetNextWindowSize(ImVec2(470, 210), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Replace the key?", &m_confirmKeys)) {
        ImGui::TextWrapped("A new key makes every installed stage 1 and payload invalid.");
        ImGui::TextWrapped("You then have to build everything again and install it on the server.");
        ImGui::Spacing();
        if (ImGui::Button("Generate a new key")) {
            startTask(TaskKind::Keys, "Generating keys", "bash ./make.sh --keys");
            m_confirmKeys = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_confirmKeys = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// buildCommand is the make.sh call for the chosen options, the same command
// the patcher README describes.
std::string App::buildCommand() const
{
    std::string command = "bash ./make.sh";
    if (m_settings.clean || buildNeedsClean()) command += " --clean";
    if (!keyStatus().privateKey) command += " --keys";
    if (m_settings.buildPayload) command += " --payload";
    if (m_settings.buildStage1) command += " --stage1";
    if (m_settings.buildPatch) command += " --patch";
    if (m_settings.buildExploit) command += " --exploit";
    command += " -- -j" + std::to_string(m_settings.jobs) + " " + shellQuote("-DWWFC_DOMAIN=\"" + m_settings.domain + "\"");
    if (!m_settings.gameFilter.empty()) {
        command += " -g" + m_settings.gameFilter;
    }
    return command;
}

std::string App::buildDomainProblem() const
{
    if (!repoValid() || !domainValid()) return {};
    const fs::path stage1 = repoPath() / "dist" / "stage1.bin";
    if (fs::exists(stage1)) {
        const auto binary = readFile(stage1);
        const std::string built = binary ? stage1ServerDomain(*binary) : "";
        if (asciiLower(built) != asciiLower(m_settings.domain)) {
            return "Stage 1 uses " + (built.empty() ? std::string("an unknown domain") : built) +
                   ", but Server is set to " + m_settings.domain + ".";
        }
    }
    if (!m_geckoFiles.empty()) {
        const auto code = readFile(repoPath() / "patch" / "build" / pathFromUtf8(m_geckoFiles.front() + ".txt"));
        const std::string built = code ? geckoServerDomain(*code) : "";
        if (asciiLower(built) != asciiLower(m_settings.domain)) {
            return "Gecko codes use " + (built.empty() ? std::string("an unknown domain") : built) +
                   ", but Server is set to " + m_settings.domain + ".";
        }
    }
    return {};
}

bool App::buildNeedsClean() const
{
    return !buildDomainProblem().empty();
}

std::string App::buildBlocker() const
{
    if (!repoValid()) return "Choose the patcher folder first.";
    if (makeScriptHasCRLF()) return "Fix the line endings of make.sh first (Requirements).";
    if (!m_requirements.ready()) return m_checking ? "Checking the requirements..." : "Fix the requirements first.";
    if (!domainValid()) return "Set the server domain first.";
    if (buildNeedsClean() && !(m_settings.buildPayload && m_settings.buildStage1 && m_settings.buildPatch)) {
        return "The domain changed: select payloads, stage 1 and Gecko codes for a complete rebuild.";
    }
    if (!(m_settings.buildPayload || m_settings.buildStage1 || m_settings.buildPatch || m_settings.buildExploit)) {
        return "Choose something to build.";
    }
    return "";
}

void App::runAction()
{
    m_pendingAction = true;
}

void App::drawBuild()
{
    heading("Build", "Runs make.sh with the options below. Everything ends up in dist/ (stage 1, payloads, key), exploit/sbcm/ and patch/build/ (Gecko codes).");

    KeyStatus keys = keyStatus();
    std::string command = buildCommand();
    std::string blocker = buildBlocker();
    if (buildNeedsClean()) {
        ImGui::TextColored(colorWarn, "The saved build uses another domain. Build will clean old output and rebuild for %s.", m_settings.domain.c_str());
    }
    if (m_task) {
        if (ImGui::Button("Cancel")) {
            m_task->kill();
        }
        ImGui::SameLine();
        ImGui::TextColored(colorAccent, "%s... %.0f s", m_taskTitle.c_str(), m_taskSeconds);
    } else {
        if (primaryButton("Build", blocker.empty())) {
            startTask(TaskKind::Build, "Building", command);
        }
        if (!blocker.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(colorWarn, "%s", blocker.c_str());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_requirements.ready());
        if (ImGui::Button("Clean only")) {
            startTask(TaskKind::Clean, "Cleaning", "bash ./make.sh --clean");
        }
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    bool changed = false;
    ImGui::SeparatorText("What to build");
    changed |= ImGui::Checkbox("Game payloads (stage 2)", &m_settings.buildPayload);
    changed |= ImGui::Checkbox("Stage 1 loader", &m_settings.buildStage1);
    changed |= ImGui::Checkbox("Gecko codes (stage 0)", &m_settings.buildPatch);
    changed |= ImGui::Checkbox("Mario Kart Wii SBCM exploit", &m_settings.buildExploit);
    changed |= ImGui::Checkbox("Clean old build output first", &m_settings.clean);

    ImGui::SeparatorText("Options");
    ImGui::SetNextItemWidth(220);
    changed |= ImGui::SliderInt("Parallel jobs", &m_settings.jobs, 1, 32);
    ImGui::SetNextItemWidth(220);
    changed |= ImGui::InputTextWithHint("Only games starting with", "e.g. RMC (empty: all games)", &m_settings.gameFilter, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    if (changed) {
        markDirty();
    }

    ImGui::SeparatorText("Command");
    if (m_monoFont) {
        ImGui::PushFont(m_monoFont);
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(colorMuted, "%s", command.c_str());
    ImGui::PopTextWrapPos();
    if (m_monoFont) {
        ImGui::PopFont();
    }
    if (!keys.privateKey) {
        ImGui::TextColored(colorWarn, "No signing key yet: this build creates one.");
    }

    if (m_lastTask == TaskKind::Build && !m_task) {
        ImGui::Spacing();
        if (m_lastExitCode == 0) {
            ImGui::TextColored(colorOk, "Build finished in %.0f s.", m_taskSeconds);
            ImGui::SameLine();
            if (primaryButton("Next: install")) {
                m_page = Page::Install;
            }
        } else {
            ImGui::TextColored(colorBad, "The build failed. The log below shows the error.");
        }
    }
}

void App::drawInstall()
{
    heading("Install", "Copies the build into the server's payload/ folder: dist/ (stage1.bin, binary/, private-key.pem) and exploit/sbcm/*.bin into payload/sbcm/.");

    BuildOutput output = buildOutput();
    KeyStatus keys = keyStatus();
    statusLine(output.stage1, "Stage 1", output.stage1 ? "dist/stage1.bin" : "", "Build stage 1 first.");
    statusLine(output.payloads > 0, "Game payloads", output.payloads > 0 ? std::to_string(output.payloads) + " in dist/binary" : "", "Build the payloads first.");
    statusLine(output.key && keys.distMatches, "Signing key", output.key ? (keys.distMatches ? "dist/private-key.pem" : "does not match misc/private-key-OUT.pem") : "",
               "dist/private-key.pem is missing or from another key: build again.");
    statusLine(output.sbcm > 0, "SBCM exploits", output.sbcm > 0 ? std::to_string(output.sbcm) + " in exploit/sbcm" : "optional", "Optional: only for patching Mario Kart Wii during matchmaking.");
    const std::string domainProblem = buildDomainProblem();
    const std::string domainHint = domainProblem.empty() ? "" : domainProblem + " Rebuild before installing.";
    statusLine(domainProblem.empty(), "Build domain", domainProblem.empty() ? m_settings.domain : "",
               domainHint.c_str());

    ImGui::SeparatorText("Destination");
    if (m_settings.serverDir.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(colorWarn, "No server folder set (step 2). If the server runs on another machine, open its panel, go to Server -> Files and upload the content of dist/ into payload/ and exploit/sbcm into payload/sbcm.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Open dist/")) {
            openInFileManager(repoPath() / "dist");
        }
    } else {
        fs::path payload = pathFromUtf8(m_settings.serverDir) / "payload";
        if (m_monoFont) {
            ImGui::PushFont(m_monoFont);
        }
        ImGui::TextUnformatted(pathToUtf8(payload).c_str());
        if (m_monoFont) {
            ImGui::PopFont();
        }
        ImGui::Spacing();
        if (primaryButton("Install into the server", domainValid() && output.stage1 && output.payloads > 0 && keys.distMatches && domainProblem.empty())) {
            m_installOk = install(m_installMessage);
        }
        ImGui::SameLine();
        if (ImGui::Button("Open payload/")) {
            std::error_code ignored;
            fs::create_directories(payload, ignored);
            openInFileManager(payload);
        }
    }
    if (!m_installMessage.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(m_installOk ? colorOk : colorBad, "%s", m_installMessage.c_str());
        ImGui::PopTextWrapPos();
        if (m_installOk && ImGui::Button("Next: Gecko codes")) {
            m_page = Page::Gecko;
            refreshGecko();
        }
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(colorMuted, "On the server, leave payloadServerAddress empty (Panel -> Server -> Settings -> Network) so it serves and signs the payloads itself.");
    ImGui::PopTextWrapPos();
}

void App::drawGecko()
{
    heading("Gecko codes", "Choose the game and region that match your copy. In Dolphin, open the game's Properties > Gecko Codes > Add New Code, then paste the code below.");
    ImGui::TextColored(colorWarn, "After a rebuild, replace the old Gecko code in Dolphin. Do not enable both versions.");

    if (m_geckoFiles.empty()) {
        ImGui::TextColored(colorWarn, "No Gecko codes yet: build with \"Gecko codes (stage 0)\" enabled.");
        return;
    }

    ImGui::SetNextItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x * 0.55f));
    ImGui::InputTextWithHint("##geckofilter", "Search game title, ID or region...", &m_geckoFilter);
    ImGui::SameLine();
    if (ImGui::Button("Open folder")) {
        openInFileManager(repoPath() / "patch" / "build");
    }
    ImGui::SameLine();
    ImGui::TextColored(colorMuted, "%d codes", static_cast<int>(m_geckoFiles.size()));

    const ImVec2 geckoAvailable = ImGui::GetContentRegionAvail();
    const float divider = 7.0f;
    m_geckoWidth = std::clamp(m_geckoWidth, 165.0f, std::max(165.0f, geckoAvailable.x - 240.0f));
    ImGui::BeginChild("geckolist", ImVec2(m_geckoWidth, geckoAvailable.y), ImGuiChildFlags_Borders);
    int shown = 0;
    const std::string filter = asciiLower(m_geckoFilter);
    for (const std::string& name : m_geckoFiles) {
        const std::string title(gameTitle(name));
        const std::string region = name.size() >= 4 ? regionName(name[3]) : "";
        if (!filter.empty() && asciiLower(title + " " + name + " " + region).find(filter) == std::string::npos) {
            continue;
        }
        ++shown;
        const std::string label = (title.empty() ? "Unknown game" : title) + "\n" + region + "  |  " + name + "##" + name;
        if (ImGui::Selectable(label.c_str(), m_geckoSelected == name)) {
            m_geckoSelected = name;
            m_geckoContent = readFile(repoPath() / "patch" / "build" / pathFromUtf8(name + ".txt")).value_or("");
        }
    }
    if (shown == 0) ImGui::TextColored(colorMuted, "No matching games.");
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("##gecko-divider", ImVec2(divider, geckoAvailable.y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) m_geckoWidth += ImGui::GetIO().MouseDelta.x;
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("geckocode", ImVec2(0, geckoAvailable.y), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    if (m_geckoSelected.empty()) {
        ImGui::TextColored(colorMuted, "Choose a game on the left.");
    } else {
        if (m_boldFont) {
            ImGui::PushFont(m_boldFont);
        }
        const std::string title(gameTitle(m_geckoSelected));
        ImGui::TextUnformatted(title.empty() ? "Unknown game" : title.c_str());
        if (m_boldFont) {
            ImGui::PopFont();
        }
        ImGui::TextColored(colorMuted, "%s  |  %s", m_geckoSelected.c_str(),
                           m_geckoSelected.size() >= 4 ? regionName(m_geckoSelected[3]) : "");
        const std::string builtDomain = geckoServerDomain(m_geckoContent);
        if (!builtDomain.empty()) {
            ImGui::TextColored(colorMuted, "This code connects to nas.%s/w1", builtDomain.c_str());
            if (!m_settings.domain.empty() && asciiLower(builtDomain) != asciiLower(m_settings.domain)) {
                ImGui::PushTextWrapPos(0);
                ImGui::TextColored(colorWarn, "Server page uses %s. Rebuild the Gecko codes for that domain, reinstall the build, and replace the old code in your game.", m_settings.domain.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        const bool codeMatchesServer = !builtDomain.empty() && asciiLower(builtDomain) == asciiLower(m_settings.domain);
        if (primaryButton("Copy Gecko code", codeMatchesServer)) {
            ImGui::SetClipboardText(m_geckoContent.c_str());
            m_geckoCopiedAt = std::chrono::steady_clock::now();
        }
        ImGui::SameLine();
        if (std::chrono::steady_clock::now() - m_geckoCopiedAt < std::chrono::seconds(3)) {
            ImGui::TextColored(colorOk, "Copied to clipboard");
        } else {
            ImGui::TextColored(colorMuted, "Paste into your cheat manager's code field");
        }
        if (m_monoFont) {
            ImGui::PushFont(m_monoFont);
        }
        ImGui::InputTextMultiline("##code", &m_geckoContent, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
        if (m_monoFont) {
            ImGui::PopFont();
        }
    }
    ImGui::EndChild();
}

void App::drawLog(float height)
{
    ImGui::BeginChild("log", ImVec2(0, height), ImGuiChildFlags_Borders);
    ImGui::TextColored(m_task ? colorAccent : colorMuted, "%s", m_task ? (m_taskTitle + "...").c_str() : "Output of the last task");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 180);
    if (ImGui::SmallButton("Copy log")) {
        std::string all;
        for (const std::string& line : m_log) {
            all += line + "\n";
        }
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_task != nullptr);
    if (ImGui::SmallButton("Clear")) {
        m_log.clear();
        m_lastTask = TaskKind::None;
    }
    ImGui::EndDisabled();

    ImGui::BeginChild("loglines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    if (m_monoFont) {
        ImGui::PushFont(m_monoFont);
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_log.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const std::string& line = m_log[i];
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            bool error = lower.find("error") != std::string::npos || lower.find("failed") != std::string::npos ||
                         lower.find("traceback") != std::string::npos;
            bool command = line.rfind("$ ", 0) == 0;
            if (error) {
                ImGui::TextColored(colorBad, "%s", line.c_str());
            } else if (command) {
                ImGui::TextColored(colorAccent, "%s", line.c_str());
            } else {
                ImGui::TextUnformatted(line.c_str());
            }
        }
    }
    if (m_monoFont) {
        ImGui::PopFont();
    }
    if (m_logFollow && m_logScrollPending) {
        ImGui::SetScrollHereY(1.0f);
        m_logScrollPending = false;
    }
    if (ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 4 && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0) {
        m_logFollow = false;
    } else if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) {
        m_logFollow = true;
    }
    ImGui::EndChild();
    ImGui::EndChild();
}
