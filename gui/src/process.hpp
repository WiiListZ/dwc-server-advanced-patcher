#pragma once

#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Process runs a program in the background and collects its output (stdout
// and stderr together) line by line, so the GUI can show it while it runs.
class Process
{
public:
    Process() = default;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    ~Process();

    // start launches argv[0] with the remaining arguments in cwd. Returns
    // false (and sets error()) when the program cannot be started.
    bool start(const std::vector<std::string>& argv, const std::filesystem::path& cwd);

    bool running() const
    {
        return m_running;
    }

    bool finished() const
    {
        return m_started && !m_running;
    }

    int exitCode() const
    {
        return m_exitCode;
    }

    const std::string& error() const
    {
        return m_error;
    }

    // kill stops the program and everything it started.
    void kill();

    // takeLines returns the lines printed since the last call.
    std::vector<std::string> takeLines();

    // wait blocks until the program has exited.
    void wait();

private:
    void reader();
    void pushOutput(const char* data, size_t size);

    std::thread m_thread;
    std::mutex m_mutex;
    std::deque<std::string> m_lines;
    std::string m_partial;
    std::atomic<bool> m_running{false};
    bool m_started = false;
    int m_exitCode = -1;
    std::string m_error;

#ifdef _WIN32
    void* m_process = nullptr;
    void* m_job = nullptr;
    void* m_readPipe = nullptr;
#else
    int m_pid = -1;
    int m_readFd = -1;
#endif
};

// runCapture runs a program to completion and returns its exit code and
// output. Used for quick checks, never on the GUI thread.
struct CaptureResult {
    int exitCode = -1;
    std::string output;
    std::string error;
};

CaptureResult runCapture(const std::vector<std::string>& argv, const std::filesystem::path& cwd);
