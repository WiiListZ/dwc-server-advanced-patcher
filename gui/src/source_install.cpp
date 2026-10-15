#include "source_install.hpp"

#include "platform.hpp"
#include "process.hpp"

#include <chrono>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TemporaryDirectory {
    fs::path path;
    ~TemporaryDirectory()
    {
        if (!path.empty()) {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    }
};

SourceInstallResult failed(const std::string& message)
{
    return {false, message};
}

} // namespace

SourceInstallResult installPatcherSource(const fs::path& target, const std::string& archiveUrl)
{
    if (target.empty()) {
        return failed("Choose a patcher folder first.");
    }
    std::error_code error;
    fs::path destination = fs::absolute(target, error).lexically_normal();
    if (error || destination == destination.root_path()) {
        return failed("Choose a folder other than the filesystem root.");
    }
    if (fs::exists(destination, error) && !fs::is_directory(destination, error)) {
        return failed("The selected path is a file, not a folder.");
    }
    if (error) {
        return failed("Cannot access the selected folder: " + error.message());
    }
    if (fs::exists(destination / "make.sh") || fs::exists(destination / "gamedefs.csv")) {
        return failed("This folder already contains patcher files. Choose another folder or use the existing copy.");
    }

    fs::path tempBase = fs::temp_directory_path(error);
    if (error) {
        return failed("Cannot find a temporary folder: " + error.message());
    }
    TemporaryDirectory temp;
    for (int attempt = 0; attempt < 10; ++attempt) {
        auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        temp.path = tempBase / ("dwc-patcher-source-" + std::to_string(nonce) + "-" + std::to_string(attempt));
        if (fs::create_directory(temp.path, error)) {
            break;
        }
        if (error) {
            return failed("Cannot create a temporary folder: " + error.message());
        }
        temp.path.clear();
    }
    if (temp.path.empty()) {
        return failed("Cannot allocate a temporary folder.");
    }

    fs::path archive = temp.path / "source.tar.gz";
#ifdef _WIN32
    const char* curl = "curl.exe";
    const char* tar = "tar.exe";
#else
    const char* curl = "curl";
    const char* tar = "tar";
#endif
    CaptureResult download = runCapture({curl, "--fail", "--location", "--retry", "2", "--connect-timeout", "15",
                                         "--max-time", "180", "--output", pathToUtf8(archive), archiveUrl}, temp.path);
    if (download.exitCode != 0) {
        return failed("Download failed. Check the connection and that curl is installed. " +
                      (download.error.empty() ? download.output : download.error));
    }
    fs::path unpacked = temp.path / "source";
    fs::create_directory(unpacked, error);
    if (error) {
        return failed("Cannot unpack the archive: " + error.message());
    }
    CaptureResult extract = runCapture({tar, "-xzf", pathToUtf8(archive), "-C", pathToUtf8(unpacked),
                                        "--strip-components=1"}, temp.path);
    if (extract.exitCode != 0) {
        return failed("Archive extraction failed. Check that tar is installed. " +
                      (extract.error.empty() ? extract.output : extract.error));
    }
    if (!fs::is_regular_file(unpacked / "make.sh") || !fs::is_regular_file(unpacked / "gamedefs.csv")) {
        return failed("The downloaded archive does not contain the patcher source.");
    }

    // Check all collisions before writing anything to an existing folder.
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(unpacked, error), end; !error && it != end; it.increment(error)) {
        fs::path relative = fs::relative(it->path(), unpacked, error);
        if (error) break;
        fs::path output = destination / relative;
        if (it->is_symlink()) {
            return failed("The archive contains a symbolic link; installation stopped.");
        }
        if (it->is_directory()) {
            if (fs::exists(output, error) && !fs::is_directory(output, error)) {
                return failed("A file blocks the folder " + pathToUtf8(output) + ".");
            }
        } else if (it->is_regular_file()) {
            if (fs::exists(output, error)) {
                return failed("The folder already contains " + pathToUtf8(output) + ". Choose another folder.");
            }
            files.push_back(relative);
        }
        if (error) break;
    }
    if (error) {
        return failed("Cannot inspect the destination: " + error.message());
    }

    fs::create_directories(destination, error);
    if (error) {
        return failed("Cannot create " + pathToUtf8(destination) + ": " + error.message());
    }
    std::vector<fs::path> created;
    for (const fs::path& relative : files) {
        fs::path output = destination / relative;
        fs::create_directories(output.parent_path(), error);
        if (!error) {
            fs::copy_file(unpacked / relative, output, fs::copy_options::none, error);
        }
        if (error) {
            for (const fs::path& file : created) {
                std::error_code ignored;
                fs::remove(file, ignored);
            }
            return failed("Cannot install " + pathToUtf8(output) + ": " + error.message());
        }
        created.push_back(output);
    }
    return {true, "Patcher source installed in " + pathToUtf8(destination) + "."};
}
