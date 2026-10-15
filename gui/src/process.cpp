#include "process.hpp"

#include "platform.hpp"

#include <cstring>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <csignal>
#  include <fcntl.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

Process::~Process()
{
    if (m_running) {
        kill();
    }
    wait();
}

void Process::pushOutput(const char* data, size_t size)
{
    std::lock_guard lock(m_mutex);
    m_partial.append(data, size);
    size_t start = 0;
    for (;;) {
        size_t end = m_partial.find('\n', start);
        if (end == std::string::npos) {
            break;
        }
        std::string line = m_partial.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        m_lines.push_back(std::move(line));
        start = end + 1;
    }
    m_partial.erase(0, start);
}

std::vector<std::string> Process::takeLines()
{
    std::lock_guard lock(m_mutex);
    std::vector<std::string> out(m_lines.begin(), m_lines.end());
    m_lines.clear();
    if (!m_running && !m_partial.empty()) {
        out.push_back(m_partial);
        m_partial.clear();
    }
    return out;
}

void Process::wait()
{
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

#ifdef _WIN32

static std::wstring quoteArgument(const std::wstring& arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            out += c;
            backslashes = 0;
        }
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

bool Process::start(const std::vector<std::string>& argv, const std::filesystem::path& cwd)
{
    m_error.clear();
    m_exitCode = -1;
    if (argv.empty()) {
        m_error = "nothing to run";
        return false;
    }

    std::wstring commandLine;
    for (const std::string& arg : argv) {
        if (!commandLine.empty()) {
            commandLine += L' ';
        }
        commandLine += quoteArgument(widen(arg));
    }

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        m_error = "cannot create a pipe";
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    // A job object takes the whole process tree down on kill().
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }

    PROCESS_INFORMATION info{};
    std::wstring workingDir = cwd.wstring();
    BOOL ok = CreateProcessW(
        nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
        workingDir.empty() ? nullptr : workingDir.c_str(), &startup, &info
    );
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        if (job) {
            CloseHandle(job);
        }
        m_error = "cannot start " + argv[0] + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (job) {
        AssignProcessToJobObject(job, info.hProcess);
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);

    m_process = info.hProcess;
    m_job = job;
    m_readPipe = readPipe;
    m_started = true;
    m_running = true;
    m_thread = std::thread(&Process::reader, this);
    return true;
}

void Process::reader()
{
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(static_cast<HANDLE>(m_readPipe), buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        pushOutput(buffer, read);
    }
    WaitForSingleObject(static_cast<HANDLE>(m_process), INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(m_process), &code);
    m_exitCode = static_cast<int>(code);
    CloseHandle(static_cast<HANDLE>(m_readPipe));
    CloseHandle(static_cast<HANDLE>(m_process));
    if (m_job) {
        CloseHandle(static_cast<HANDLE>(m_job));
        m_job = nullptr;
    }
    m_running = false;
}

void Process::kill()
{
    if (!m_running) {
        return;
    }
    if (m_job) {
        TerminateJobObject(static_cast<HANDLE>(m_job), 1);
    } else {
        TerminateProcess(static_cast<HANDLE>(m_process), 1);
    }
}

#else

bool Process::start(const std::vector<std::string>& argv, const std::filesystem::path& cwd)
{
    m_error.clear();
    m_exitCode = -1;
    if (argv.empty()) {
        m_error = "nothing to run";
        return false;
    }

    int fds[2];
    if (pipe(fds) != 0) {
        m_error = "cannot create a pipe";
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        m_error = "cannot start " + argv[0];
        return false;
    }
    if (pid == 0) {
        // Own process group, so kill() reaches everything make.sh starts.
        setpgid(0, 0);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
            _exit(127);
        }
        std::vector<char*> args;
        for (const std::string& arg : argv) {
            args.push_back(const_cast<char*>(arg.c_str()));
        }
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }

    close(fds[1]);
    m_pid = pid;
    m_readFd = fds[0];
    m_started = true;
    m_running = true;
    m_thread = std::thread(&Process::reader, this);
    return true;
}

void Process::reader()
{
    char buffer[4096];
    for (;;) {
        ssize_t read = ::read(m_readFd, buffer, sizeof(buffer));
        if (read <= 0) {
            break;
        }
        pushOutput(buffer, static_cast<size_t>(read));
    }
    close(m_readFd);
    int status = 0;
    waitpid(m_pid, &status, 0);
    m_exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    m_running = false;
}

void Process::kill()
{
    if (m_running && m_pid > 0) {
        ::kill(-m_pid, SIGTERM);
    }
}

#endif

CaptureResult runCapture(const std::vector<std::string>& argv, const std::filesystem::path& cwd)
{
    CaptureResult result;
    Process process;
    if (!process.start(argv, cwd)) {
        result.error = process.error();
        return result;
    }
    process.wait();
    for (const std::string& line : process.takeLines()) {
        result.output += line;
        result.output += '\n';
    }
    result.exitCode = process.exitCode();
    return result;
}
