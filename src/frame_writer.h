#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/frame.h>
#ifdef __cplusplus
}
#endif

#include <cstdint>

enum class ImageFormat {
    PNG = 0,
    JPG = 1
};

/**
 * @brief Saves AVFrame pixels to image files using stb_image_write.
 */
class FrameWriter {
public:
    /**
     * @brief Save a video frame as an image file.
     * @param frame       Decoded AVFrame (any pixel format, will be converted to RGB).
     * @param path        Output file path (must end with .png or .jpg).
     * @param format      Target image format.
     * @param quality     JPEG quality (1-100), ignored for PNG.
     * @return true on success.
     */
    static bool save_frame(AVFrame* frame,
                           const char* path,
                           ImageFormat format,
                           int quality = 95);
private:
    /// Convert frame to RGB24 and return allocated buffer (must be freed with av_free).
    static uint8_t* convert_to_rgb(AVFrame* frame, int* out_width, int* out_height);
};
