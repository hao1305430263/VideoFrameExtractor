// Video Frame Extractor — main entry point
// GLFW window + ImGui + FFmpeg backend

#include "app.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <cstring>

// GLFW
#include <GLFW/glfw3.h>

// ──────────────────────────────────────────────
//  Globals
// ──────────────────────────────────────────────

static AppState g_state;
static GLFWwindow* g_window = nullptr;

// ──────────────────────────────────────────────
//  GLFW Callbacks
// ──────────────────────────────────────────────

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void glfw_drop_callback(GLFWwindow* /*window*/, int count, const char** paths) {
    if (count > 0 && paths[0]) {
        strncpy(g_state.video_path, paths[0], sizeof(g_state.video_path) - 1);
        g_state.video_path[sizeof(g_state.video_path) - 1] = '\0';
        g_state.load_video_info();
    }
}

static void glfw_key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
    // ESC to cancel extraction
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (g_state.extracting) {
            g_state.cancel_extraction();
        }
    }
}

// ──────────────────────────────────────────────
//  Main
// ──────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // --- COM init (needed for file dialogs) ---
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // --- GLFW init ---
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        CoUninitialize();
        return 1;
    }

    // OpenGL 3.3
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);            // fixed size
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE);

    g_window = glfwCreateWindow(500, 800, "Video Frame Extractor", nullptr, nullptr);
    if (!g_window) {
        glfwTerminate();
        CoUninitialize();
        return 1;
    }
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);  // vsync

    // Callbacks
    glfwSetDropCallback(g_window, glfw_drop_callback);
    glfwSetKeyCallback(g_window, glfw_key_callback);

    // --- ImGui init ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;   // no ini file

    // Style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;

    // Load system Chinese font (Microsoft YaHei)
    io.Fonts->AddFontFromFileTTF("c:/Windows/Fonts/msyh.ttc", 16.0f, nullptr,
        io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // --- Main loop ---
    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();

        // Handle window resize
        int display_w = 0, display_h = 0;
        glfwGetFramebufferSize(g_window, &display_w, &display_h);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Disable padding/margin for full-window UI
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);

        render_ui(&g_state);

        ImGui::PopStyleVar(2);

        // Render
        ImGui::Render();
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(g_window);
    }

    // --- Cleanup ---
    if (g_state.extracting) {
        g_state.cancel_extraction();
        // Give worker a moment to exit
        g_state.extracting = false;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(g_window);
    glfwTerminate();
    CoUninitialize();

    return 0;
}
