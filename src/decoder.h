#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#ifdef __cplusplus
}
#endif

struct VideoInfo {
    double duration_sec = 0.0;
    double fps = 0.0;
    int64_t total_frames = 0;
    int width = 0;
    int height = 0;
};

/**
 * @brief FFmpeg-based video decoder for frame extraction.
 *
 * Opens a video file, provides seeking and sequential frame decoding.
 * Each instance is NOT thread-safe — use one per thread.
 */
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    /// Open a video file. Optionally fills out_info with video metadata.
    bool open(const char* path, VideoInfo* out_info = nullptr);

    /// Close the video and release resources.
    void close();

    /// Seek to the given time in seconds (keyframe + forward decode).
    bool seek(double seconds);

    /// Decode the next frame. *out_frame is valid until next call.
    /// Returns false on EOF or error.
    bool decode_next_frame(AVFrame** out_frame);

    /// Get time base for converting PTS to seconds.
    AVRational time_base() const;

    /// Get video info populated after open().
    const VideoInfo& info() const { return info_; }

    /// Get the last error message.
    const char* last_error() const { return error_; }

private:
    AVFormatContext* fmt_ctx_ = nullptr;
    const AVCodec* codec_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    int video_stream_idx_ = -1;
    VideoInfo info_;
    char error_[512] = {};

    bool decode_packet(AVFrame** out_frame);
    void set_error(const char* msg);
};
