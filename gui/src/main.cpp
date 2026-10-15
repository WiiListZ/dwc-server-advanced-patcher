// dwc-server-advanced patcher GUI: sets up and runs the patcher build
// (make.sh) and installs the result into a dwc-server-advanced server.

#include "app.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"

#include <GLFW/glfw3.h>

#ifndef GL_BGR_EXT
#  define GL_BGR_EXT 0x80E0
#endif
#ifndef GL_PACK_ALIGNMENT
#  define GL_PACK_ALIGNMENT 0x0D05
#endif

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

ImFont* loadFont(const std::initializer_list<const char*>& candidates, float size)
{
    for (const char* path : candidates) {
        if (fs::exists(path)) {
            return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size);
        }
    }
    return nullptr;
}

void applyStyle(float scale)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 0;
    style.ChildRounding = 8;
    style.FrameRounding = 6;
    style.PopupRounding = 8;
    style.GrabRounding = 6;
    style.ScrollbarRounding = 8;
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.WindowPadding = ImVec2(14, 14);
    style.ChildBorderSize = 1;
    style.SeparatorTextBorderSize = 1;
    style.SeparatorTextPadding = ImVec2(0, 6);

    ImVec4* colors = style.Colors;
    const ImVec4 background{0.055f, 0.06f, 0.07f, 1.0f};
    const ImVec4 surface{0.09f, 0.095f, 0.11f, 1.0f};
    const ImVec4 raised{0.14f, 0.15f, 0.17f, 1.0f};
    const ImVec4 accent{0.0f, 0.63f, 0.90f, 1.0f};
    colors[ImGuiCol_WindowBg] = background;
    colors[ImGuiCol_ChildBg] = surface;
    colors[ImGuiCol_PopupBg] = surface;
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.21f, 0.24f, 1.0f);
    colors[ImGuiCol_FrameBg] = raised;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.19f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.21f, 0.22f, 0.25f, 1.0f);
    colors[ImGuiCol_Button] = raised;
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.20f, 0.23f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.23f, 0.24f, 0.27f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.0f, 0.63f, 0.90f, 0.22f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.63f, 0.90f, 0.30f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.63f, 0.90f, 0.40f);
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.10f, 0.70f, 0.95f, 1.0f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.63f, 0.90f, 0.35f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.21f, 0.24f, 1.0f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
    style.ScaleAllSizes(scale);
}

void glfwError(int code, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

} // namespace

// writeScreenshot saves the window as a 24-bit BMP (used by --screenshot).
static bool writeScreenshot(const std::string& file, int width, int height)
{
    int stride = (width * 3 + 3) & ~3;
    std::vector<unsigned char> pixels(static_cast<size_t>(stride) * height);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_BGR_EXT, GL_UNSIGNED_BYTE, pixels.data());
    FILE* out = std::fopen(file.c_str(), "wb");
    if (!out) {
        return false;
    }
    unsigned int dataSize = static_cast<unsigned int>(pixels.size());
    unsigned char header[54] = {'B', 'M'};
    auto put32 = [&](int offset, unsigned int value) {
        for (int i = 0; i < 4; ++i) header[offset + i] = static_cast<unsigned char>(value >> (8 * i));
    };
    put32(2, 54 + dataSize);
    put32(10, 54);
    put32(14, 40);
    put32(18, static_cast<unsigned int>(width));
    put32(22, static_cast<unsigned int>(height));
    header[26] = 1;
    header[28] = 24;
    put32(34, dataSize);
    std::fwrite(header, 1, sizeof(header), out);
    std::fwrite(pixels.data(), 1, pixels.size(), out);
    std::fclose(out);
    return true;
}

int main(int argc, char** argv)
{
    // --screenshot <file.bmp> [--page <name>] renders one page into a file,
    // without showing the window, for the documentation.
    // --run starts the page's main action (build, server test, install).
    std::string screenshot, page;
    bool run = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else if (arg == "--page" && i + 1 < argc) page = argv[++i];
        else if (arg == "--run") run = true;
    }

    glfwSetErrorCallback(glfwError);
    if (!glfwInit()) {
        return 1;
    }

#ifdef __APPLE__
    const char* glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    if (!screenshot.empty()) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
    }
    GLFWwindow* window = glfwCreateWindow(1180, 780, "DWC Server Advanced - Client patcher", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    float scaleX = 1, scaleY = 1;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    float scale = scaleX > 0 ? scaleX : 1.0f;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    float size = 17.0f * scale;
    ImFont* regular = loadFont(
        {"C:\\Windows\\Fonts\\segoeui.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
         "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf"},
        size
    );
    if (!regular) {
        ImFontConfig config;
        config.SizePixels = 13.0f * scale;
        regular = io.Fonts->AddFontDefault(&config);
    }
    ImFont* bold = loadFont(
        {"C:\\Windows\\Fonts\\segoeuib.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
         "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"},
        size * 1.18f
    );
    ImFont* mono = loadFont(
        {"C:\\Windows\\Fonts\\consola.ttf", "/System/Library/Fonts/Supplemental/Courier New.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
         "/usr/share/fonts/TTF/DejaVuSansMono.ttf"},
        size * 0.88f
    );
    applyStyle(scale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    {
        App app;
        app.setFonts(regular, bold, mono);
        if (!page.empty()) {
            app.showPage(page);
        }
        if (run) {
            app.runAction();
        }
        int frames = 0;
        int idleFrames = 0;

        while (!glfwWindowShouldClose(window)) {
            // Wake up regularly so build output keeps flowing while idle.
            glfwWaitEventsTimeout(0.1);
            ++frames;
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            app.frame();

            ImGui::Render();
            int width = 0, height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.055f, 0.06f, 0.07f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            // Wait a few idle frames so scrolling and layout settle.
            idleFrames = app.busy() ? 0 : idleFrames + 1;
            if (!screenshot.empty() && frames > 20 && (idleFrames > 5 || (!run && frames > 600))) {
                writeScreenshot(screenshot, width, height);
                break;
            }
            glfwSwapBuffers(window);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
