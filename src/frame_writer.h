#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/frame.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif

enum class ImageFormat { PNG = 0, JPG = 1 };

class FrameWriter {
public:
    FrameWriter() = default;
    ~FrameWriter();

    FrameWriter(const FrameWriter&) = delete;
    FrameWriter& operator=(const FrameWriter&) = delete;

    bool init(AVFrame* template_frame, ImageFormat format, int quality = 95);
    bool save_frame(AVFrame* frame, const char* path);
    const char* last_error() const { return error_; }

private:
    // FFmpeg encoder path (JPG)
    AVCodecContext* enc_ctx_  = nullptr;
    AVFrame*        enc_frame_ = nullptr;
    AVPacket*       packet_    = nullptr;

    // Shared
    SwsContext*     sws_       = nullptr;
    ImageFormat     format_    = ImageFormat::JPG;
    int             width_     = 0;
    int             height_    = 0;
    int             quality_   = 95;
    bool            use_stb_   = false;  // true for PNG (stb fallback)
    char            error_[256] = {};
};
