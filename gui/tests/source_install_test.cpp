#include "platform.hpp"
#include "process.hpp"
#include "source_install.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main()
{
    std::error_code error;
    fs::path temp = fs::temp_directory_path() /
                    ("dwc-source-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path source = temp / "archive-root";
    fs::create_directories(source / "patch", error);
    if (error || !writeFile(source / "make.sh", "#!/bin/bash\n") ||
        !writeFile(source / "gamedefs.csv", "game,definition\n") ||
        !writeFile(source / "patch" / "sample.txt", "sample\n")) {
        std::cerr << "Cannot prepare source archive\n";
        return 1;
    }
    fs::path archive = temp / "source.tar.gz";
#ifdef _WIN32
    const char* tar = "tar.exe";
#else
    const char* tar = "tar";
#endif
    CaptureResult packed = runCapture({tar, "-czf", pathToUtf8(archive), "-C", pathToUtf8(temp), "archive-root"}, temp);
    if (packed.exitCode != 0) {
        std::cerr << "Cannot create source archive: " << packed.output << packed.error << "\n";
        return 1;
    }

    std::string url = archive.generic_string();
#ifdef _WIN32
    url = "file:///" + url;
#else
    url = "file://" + url;
#endif
    fs::path target = temp / "program-folder";
    fs::create_directory(target);
    writeFile(target / "dwc-patcher-gui.exe", "existing program");
    SourceInstallResult first = installPatcherSource(target, url);
    if (!first.ok || !fs::exists(target / "make.sh") || !fs::exists(target / "patch" / "sample.txt") ||
        readFile(target / "dwc-patcher-gui.exe") != "existing program") {
        std::cerr << "Installation into the program folder failed: " << first.message << "\n";
        return 1;
    }

    fs::path fresh = temp / "new-patcher-folder";
    SourceInstallResult freshResult = installPatcherSource(fresh, url);
    if (!freshResult.ok || !fs::exists(fresh / "make.sh") || !fs::exists(fresh / "gamedefs.csv")) {
        std::cerr << "Creation of a new patcher folder failed: " << freshResult.message << "\n";
        return 1;
    }

    fs::path collision = temp / "collision-folder";
    fs::create_directories(collision / "patch");
    writeFile(collision / "patch" / "sample.txt", "keep this file");
    SourceInstallResult second = installPatcherSource(collision, url);
    if (second.ok || readFile(collision / "patch" / "sample.txt") != "keep this file" || fs::exists(collision / "make.sh")) {
        std::cerr << "An existing file was overwritten\n";
        return 1;
    }
    fs::remove_all(temp, error);
    return 0;
}
