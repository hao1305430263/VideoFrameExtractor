#include "app.h"
#include "decoder.h"
#include "frame_writer.h"

#include "imgui.h"
#include <cstdio>
#include <cmath>

// Windows COM for file dialogs
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ShObjIdl.h>
#include <comdef.h>

// ──────────────────────────────────────────────
//  AppState implementation
// ──────────────────────────────────────────────

void AppState::reset() {
    video_path[0] = '\0';
    output_dir[0] = '\0';
    start_h = start_m = start_s = 0;
    end_h = 0; end_m = 0; end_s = 5;
    interval_frames = 1;
    img_format = 1;  // JPG default
    video_duration = 0.0;
    video_fps = 0.0;
    video_width = 0;
    video_height = 0;
    has_video = false;
    progress = 0.0f;
    status_msg[0] = '\0';
}

void AppState::set_status(const char* msg) {
    std::lock_guard<std::mutex> lock(status_mutex);
    strncpy(status_msg, msg, sizeof(status_msg) - 1);
    status_msg[sizeof(status_msg) - 1] = '\0';
}

void AppState::load_video_info() {
    has_video = false;
    if (video_path[0] == '\0') return;

    VideoDecoder decoder;
    VideoInfo info;
    if (!decoder.open(video_path, &info)) {
        set_status(decoder.last_error());
        return;
    }

    video_duration = info.duration_sec;
    video_fps = info.fps;
    video_width = info.width;
    video_height = info.height;
    has_video = true;

    // Auto-set end time to video duration (clamped to reasonable range)
    int total_sec = (int)info.duration_sec;
    end_h = total_sec / 3600;
    end_m = (total_sec % 3600) / 60;
    end_s = total_sec % 60;

    // Clamp start to valid range
    int start_total = start_h * 3600 + start_m * 60 + start_s;
    if (start_total >= total_sec) {
        start_h = start_m = start_s = 0;
    }

    set_status("");
}

bool AppState::is_time_range_valid() const {
    int start_sec = start_h * 3600 + start_m * 60 + start_s;
    int end_sec = end_h * 3600 + end_m * 60 + end_s;
    return end_sec > start_sec && (start_sec < (int)video_duration || video_duration <= 0);
}

// ──────────────────────────────────────────────
//  Extraction worker
// ──────────────────────────────────────────────

static void extraction_worker(AppState* s) {
    VideoDecoder decoder;

    // Copy path so UI can be updated independently
    char path[512];
    strncpy(path, s->video_path, sizeof(path) - 1);

    if (!decoder.open(path)) {
        s->set_status(decoder.last_error());
        s->extracting = false;
        return;
    }

    double start_sec = s->start_h * 3600 + s->start_m * 60 + s->start_s;
    double end_sec   = s->end_h   * 3600 + s->end_m   * 60 + s->end_s;
    int    interval  = s->interval_frames;

    if (end_sec > decoder.info().duration_sec)
        end_sec = decoder.info().duration_sec;

    if (start_sec >= end_sec || interval <= 0) {
        s->set_status("Invalid time range or interval");
        s->extracting = false;
        return;
    }

    // Estimate total output frames
    double fps = decoder.info().fps;
    double total_range_frames = (end_sec - start_sec) * fps;
    int estimated = (int)(total_range_frames / interval) + 1;

    int out_idx = 0;           // saved frame count
    int64_t decoded_count = 0; // frames decoded since seek

    // Seek to 1 second before start to reach the first keyframe
    double seek_to = start_sec > 1.0 ? start_sec - 1.0 : 0.0;
    decoder.seek(seek_to);

    char msg_buf[256];

    // ── Init FFmpeg-based encoder (once, reuses cached contexts) ──
    FrameWriter writer;
    ImageFormat fmt = (s->img_format == 0) ? ImageFormat::PNG : ImageFormat::JPG;
    bool writer_ready = false;

    AVFrame* frame = nullptr;
    while (decoder.decode_next_frame(&frame) && !s->cancel_requested) {
        AVRational tb = decoder.time_base();
        double pts = frame->pts * av_q2d(tb);

        // Skip frames before the start time
        if (pts < start_sec) continue;
        // Stop once past end time
        if (pts >= end_sec) break;

        // Lazy-init encoder from the first valid frame
        if (!writer_ready) {
            if (!writer.init(frame, fmt, 95)) {
                snprintf(msg_buf, sizeof(msg_buf), "Encoder error: %s", writer.last_error());
                s->set_status(msg_buf);
                s->extracting = false;
                decoder.close();
                return;
            }
            writer_ready = true;
        }

        // Frame-based interval: save every Nth frame
        if (decoded_count % interval == 0) {
            char filename[512];
            snprintf(filename, sizeof(filename), "%s\\output_%05d.%s",
                     s->output_dir,
                     out_idx + 1,
                     s->img_format == 0 ? "png" : "jpg");

            if (writer.save_frame(frame, filename)) {
                out_idx++;
                s->progress = (float)out_idx / (float)estimated;
            } else {
                snprintf(msg_buf, sizeof(msg_buf), "Failed to save frame %d: %s",
                         out_idx + 1, filename);
                s->set_status(msg_buf);
            }
        }

        decoded_count++;
    }

    decoder.close();

    if (s->cancel_requested) {
        snprintf(msg_buf, sizeof(msg_buf), "Cancelled — extracted %d frames", out_idx);
    } else {
        snprintf(msg_buf, sizeof(msg_buf), "Done — %d frames saved to %s", out_idx, s->output_dir);
    }
    s->set_status(msg_buf);
    s->extracting = false;
}

void AppState::start_extraction() {
    if (extracting) return;
    if (output_dir[0] == '\0') {
        set_status("Please select an output directory first");
        return;
    }
    cancel_requested = false;
    progress = 0.0f;
    extracting = true;
    set_status("Extracting...");
    worker_thread = std::thread(extraction_worker, this);
    worker_thread.detach();
}

void AppState::cancel_extraction() {
    cancel_requested = true;
}

// ──────────────────────────────────────────────
//  Windows File Dialogs
// ──────────────────────────────────────────────

bool win32_open_file_dialog(char* out_path, size_t out_size) {
    IFileOpenDialog* pDialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                   IID_IFileOpenDialog, (void**)&pDialog);
    if (FAILED(hr)) return false;

    // Filter: video files
    COMDLG_FILTERSPEC filters[] = {
        { L"Video Files", L"*.mp4;*.avi;*.mkv;*.mov;*.wmv;*.webm;*.flv;*.m4v;*.mpg;*.mpeg;*.ts;*.m2ts;*.3gp;*.ogv" },
        { L"All Files (*.*)", L"*.*" }
    };
    pDialog->SetFileTypes(2, filters);
    pDialog->SetFileTypeIndex(1);
    pDialog->SetDefaultExtension(L"mp4");

    hr = pDialog->Show(nullptr);
    if (FAILED(hr)) { pDialog->Release(); return false; }

    IShellItem* pItem = nullptr;
    hr = pDialog->GetResult(&pItem);
    if (FAILED(hr)) { pDialog->Release(); return false; }

    PWSTR pszPath = nullptr;
    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
    if (SUCCEEDED(hr)) {
        WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, out_path, (int)out_size, nullptr, nullptr);
        CoTaskMemFree(pszPath);
    }

    pItem->Release();
    pDialog->Release();
    return SUCCEEDED(hr);
}

bool win32_open_folder_dialog(char* out_path, size_t out_size) {
    IFileOpenDialog* pDialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                   IID_IFileOpenDialog, (void**)&pDialog);
    if (FAILED(hr)) return false;

    // Set options for folder picker
    DWORD flags = 0;
    pDialog->GetOptions(&flags);
    pDialog->SetOptions(flags | FOS_PICKFOLDERS);

    hr = pDialog->Show(nullptr);
    if (FAILED(hr)) { pDialog->Release(); return false; }

    IShellItem* pItem = nullptr;
    hr = pDialog->GetResult(&pItem);
    if (FAILED(hr)) { pDialog->Release(); return false; }

    PWSTR pszPath = nullptr;
    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
    if (SUCCEEDED(hr)) {
        WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, out_path, (int)out_size, nullptr, nullptr);
        CoTaskMemFree(pszPath);
    }

    pItem->Release();
    pDialog->Release();
    return SUCCEEDED(hr);
}

// ──────────────────────────────────────────────
//  UI Rendering
// ──────────────────────────────────────────────

void render_ui(AppState* s) {
    // --- Main window (fills entire GLFW window, cannot be moved) ---
    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::SetNextWindowSize(ImVec2(500, 800), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);

    ImGui::Begin("Video Frame Extractor", nullptr, wflags);

    // ── 1. File selection (drag area + click to browse) ──
    {
        ImGui::Text("Video File");
        ImGui::Spacing();

        // Make a big button area for drag & drop / click
        ImVec2 drop_size(ImGui::GetContentRegionAvail().x, 60);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.25f, 0.30f, 1.0f));

        bool has_file = (s->video_path[0] != '\0');
        const char* drop_label = has_file ? s->video_path : "Drop video file here, or click to browse...";

        if (ImGui::Button(drop_label, drop_size)) {
            char buf[512] = {};
            if (win32_open_file_dialog(buf, sizeof(buf))) {
                strncpy(s->video_path, buf, sizeof(s->video_path) - 1);
                s->load_video_info();
            }
        }

        // Accept drag & drop payloads
        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH");
            if (payload && payload->DataSize > 0) {
                const char* dropped = (const char*)payload->Data;
                strncpy(s->video_path, dropped, sizeof(s->video_path) - 1);
                s->video_path[sizeof(s->video_path) - 1] = '\0';
                s->load_video_info();
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopStyleColor(3);

        // Show video info if loaded
        if (s->has_video) {
            int total_sec = (int)s->video_duration;
            int h = total_sec / 3600;
            int m = (total_sec % 3600) / 60;
            int sec = total_sec % 60;
            ImGui::TextDisabled("  Resolution: %dx%d  |  FPS: %.2f  |  Duration: %02d:%02d:%02d",
                               s->video_width, s->video_height,
                               s->video_fps, h, m, sec);
        } else if (s->video_path[0] != '\0') {
            ImGui::TextDisabled("  (no video loaded)");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── 2. Time range (vertical layout) ──
    {
        ImGui::Text("Time Range");
        ImGui::Spacing();

        auto time_input = [](const char* label, int* h, int* m, int* sec) {
            ImGui::Text("%s", label);
            ImGui::PushItemWidth(60);
            ImGui::Text(" H"); ImGui::SameLine();
            ImGui::InputInt("##h", h, 0, 0); ImGui::SameLine();
            ImGui::Text(" M"); ImGui::SameLine();
            ImGui::InputInt("##m", m, 0, 0); ImGui::SameLine();
            ImGui::Text(" S"); ImGui::SameLine();
            ImGui::InputInt("##s", sec, 0, 0);
            ImGui::PopItemWidth();
            if (*h < 0) *h = 0;
            if (*m < 0) *m = 0; else if (*m > 59) *m = 59;
            if (*sec < 0) *sec = 0; else if (*sec > 59) *sec = 59;
        };

        // Start time
        ImGui::PushID("start");
        time_input("Start Time", &s->start_h, &s->start_m, &s->start_s);
        if (ImGui::SmallButton("Jump to start")) {
            s->start_h = s->start_m = s->start_s = 0;
        }
        ImGui::PopID();

        ImGui::Spacing();

        // End time
        ImGui::PushID("end");
        time_input("End Time", &s->end_h, &s->end_m, &s->end_s);
        if (ImGui::SmallButton("Jump to end")) {
            if (s->has_video) {
                int total = (int)s->video_duration;
                s->end_h = total / 3600;
                s->end_m = (total % 3600) / 60;
                s->end_s = total % 60;
            }
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── 3. Extraction settings ──
    {
        ImGui::Text("Settings");
        ImGui::Spacing();

        ImGui::PushItemWidth(100);
        ImGui::InputInt("Extract one frame every (frames)", &s->interval_frames, 1, 5);
        if (s->interval_frames < 1) s->interval_frames = 1;
        if (s->interval_frames > 100000) s->interval_frames = 100000;
        ImGui::PopItemWidth();

        ImGui::Spacing();

        const char* formats[] = { "PNG", "JPG" };
        ImGui::PushItemWidth(120);
        ImGui::Combo("Output Format", &s->img_format, formats, 2);
        ImGui::PopItemWidth();
    }

    ImGui::Spacing();

    // ── 4. Output directory ──
    {
        ImGui::Text("Output Directory");
        ImGui::Spacing();

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 100);
        ImGui::InputText("##outdir", s->output_dir, sizeof(s->output_dir));
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(90, 0))) {
            char buf[512] = {};
            if (win32_open_folder_dialog(buf, sizeof(buf))) {
                strncpy(s->output_dir, buf, sizeof(s->output_dir) - 1);
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── 5. Info & estimate ──
    {
        int start_sec = s->start_h * 3600 + s->start_m * 60 + s->start_s;
        int end_sec   = s->end_h   * 3600 + s->end_m   * 60 + s->end_s;
        if (end_sec > start_sec && s->interval_frames > 0 && s->video_fps > 0) {
            int estimated = (int)((end_sec - start_sec) * s->video_fps / s->interval_frames) + 1;
            ImGui::TextDisabled("Estimated: %d frame(s)  |  Range: %02d:%02d:%02d → %02d:%02d:%02d",
                               estimated,
                               s->start_h, s->start_m, s->start_s,
                               s->end_h, s->end_m, s->end_s);
        } else {
            ImGui::TextDisabled("Set a valid time range to see estimated frame count");
        }
    }

    ImGui::Spacing();

    // ── 6. Progress bar ──
    if (s->extracting) {
        float p = s->progress;
        ImGui::ProgressBar(p, ImVec2(-1, 20));
        ImGui::Spacing();
    }

    // ── 7. Action buttons ──
    {
        float btn_width = 140;
        float avail = ImGui::GetContentRegionAvail().x;
        float offset = (avail - (s->extracting ? btn_width * 2 : btn_width)) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

        if (!s->extracting) {
            // Disable button if no video or no output dir
            bool disabled = (!s->has_video || s->output_dir[0] == '\0' || !s->is_time_range_valid());
            if (disabled) ImGui::BeginDisabled();

            if (ImGui::Button("Start Extraction", ImVec2(btn_width, 35))) {
                s->start_extraction();
            }

            if (disabled) ImGui::EndDisabled();

        } else {
            if (ImGui::Button("Cancel", ImVec2(btn_width, 35))) {
                s->cancel_extraction();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Working... %.0f%%", s->progress * 100.0f);
        }
    }

    // ── 8. Status message ──
    {
        std::lock_guard<std::mutex> lock(s->status_mutex);
        if (s->status_msg[0]) {
            ImGui::Spacing();
            ImGui::Separator();
            ImVec4 color = s->extracting ? ImVec4(1, 1, 0, 1) : ImVec4(0.4f, 1, 0.4f, 1);
            ImGui::TextColored(color, "%s", s->status_msg);
        }
    }

    ImGui::End();
}
