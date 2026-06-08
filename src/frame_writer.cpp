#include "frame_writer.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
}

// stb_image_write implementation
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

uint8_t* FrameWriter::convert_to_rgb(AVFrame* frame, int* out_width, int* out_height) {
    int width = frame->width;
    int height = frame->height;

    // Cache SwsContext — creation is expensive, reuse for all frames
    static SwsContext* cached_sws = nullptr;
    static int cached_width = 0, cached_height = 0;
    static AVPixelFormat cached_fmt = AV_PIX_FMT_NONE;

    if (!cached_sws || width != cached_width || height != cached_height ||
        frame->format != cached_fmt) {
        if (cached_sws) sws_freeContext(cached_sws);
        cached_sws = sws_getContext(
            width, height, (AVPixelFormat)frame->format,
            width, height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        cached_width = width;
        cached_height = height;
        cached_fmt = (AVPixelFormat)frame->format;
    }

    if (!cached_sws) return nullptr;

    // Allocate RGB buffer
    int rgb_stride = width * 3;
    uint8_t* rgb_data = (uint8_t*)av_malloc((size_t)rgb_stride * height);
    if (!rgb_data) return nullptr;

    // Convert
    uint8_t* dst_planes[1] = { rgb_data };
    int dst_strides[1] = { rgb_stride };

    sws_scale(cached_sws,
              (const uint8_t* const*)frame->data, frame->linesize,
              0, height,
              dst_planes, dst_strides);

    *out_width = width;
    *out_height = height;
    return rgb_data;
}

bool FrameWriter::save_frame(AVFrame* frame,
                              const char* path,
                              ImageFormat format,
                              int quality) {
    if (!frame || !path) return false;

    int width = 0, height = 0;
    uint8_t* rgb_data = convert_to_rgb(frame, &width, &height);
    if (!rgb_data) return false;

    bool ok = false;
    if (format == ImageFormat::PNG) {
        int stride = width * 3;
        ok = (stbi_write_png(path, width, height, 3, rgb_data, stride) != 0);
    } else {
        ok = (stbi_write_jpg(path, width, height, 3, rgb_data, quality) != 0);
    }

    av_free(rgb_data);
    return ok;
}
