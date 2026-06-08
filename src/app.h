#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <mutex>

/// Application UI state (all fields accessible from both UI and worker thread).
struct AppState {
    // --- Input file ---
    char video_path[512] = {};
    char output_dir[512] = {};

    // --- Time range (HH:MM:SS) ---
    int start_h = 0, start_m = 0, start_s = 0;
    int end_h = 0, end_m = 0, end_s = 5;

    // --- Extraction settings ---
    int interval_frames = 1; // extract one frame every N frames
    int img_format = 0;      // 0 = PNG, 1 = JPG

    // --- Video metadata (filled after loading) ---
    double video_duration = 0.0;
    double video_fps = 0.0;
    int video_width = 0;
    int video_height = 0;
    bool has_video = false;

    // --- Extraction worker state ---
    std::atomic<bool> extracting{false};
    std::atomic<bool> cancel_requested{false};
    std::atomic<float> progress{0.0f};
    std::thread worker_thread;
    std::mutex status_mutex;
    char status_msg[256] = {};

    /// Reset all fields to defaults.
    void reset();

    /// Load video info from the current path.
    void load_video_info();

    /// Set a status message (thread-safe).
    void set_status(const char* msg);

    /// Start the extraction in a background thread.
    void start_extraction();

    /// Request cancellation and join the worker thread.
    void cancel_extraction();

    /// Whether the start/end times are valid.
    bool is_time_range_valid() const;
};

/// Render the entire ImGui application window.
void render_ui(AppState* state);

/// Windows file open dialog. Fills out_path (UTF-8) with the selected file.
bool win32_open_file_dialog(char* out_path, size_t out_size);

/// Windows folder selection dialog. Fills out_path (UTF-8) with the selected folder.
bool win32_open_folder_dialog(char* out_path, size_t out_size);
