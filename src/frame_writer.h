#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/frame.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif

enum class ImageFormat {
    PNG = 0,
    JPG = 1
};

/**
 * @brief Saves AVFrame pixels to image files using FFmpeg's native encoders.
 *
 * Usage:
 *   FrameWriter writer;
 *   writer.init(frame, format);
 *   while (...) { writer.save_frame(frame, path); }
 */
class FrameWriter {
public:
    FrameWriter() = default;
    ~FrameWriter();

    FrameWriter(const FrameWriter&) = delete;
    FrameWriter& operator=(const FrameWriter&) = delete;

    /// Initialize encoder + scaler for the given frame format and output type.
    /// Call once before save_frame(). First frame determines dimensions & pixel fmt.
    bool init(AVFrame* template_frame, ImageFormat format, int quality = 95);

    /// Save a frame as an image. init() must be called first.
    bool save_frame(AVFrame* frame, const char* path);

    /// Get last error message.
    const char* last_error() const { return error_; }

private:
    AVCodecContext* enc_ctx_  = nullptr;
    AVFrame*        enc_frame_ = nullptr;
    SwsContext*     sws_       = nullptr;
    AVPacket*       packet_    = nullptr;
    ImageFormat     format_    = ImageFormat::JPG;
    int             enc_width_  = 0;
    int             enc_height_ = 0;
    char            error_[256] = {};

    bool encode_and_write(AVFrame* frame, const char* path);
};
