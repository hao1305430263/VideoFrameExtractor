#include "frame_writer.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

FrameWriter::~FrameWriter() {
    if (enc_ctx_)   avcodec_free_context(&enc_ctx_);
    if (enc_frame_) av_frame_free(&enc_frame_);
    if (sws_)       sws_freeContext(sws_);
    if (packet_)    av_packet_free(&packet_);
}

bool FrameWriter::init(AVFrame* template_frame, ImageFormat format, int quality) {
    format_  = format;
    width_   = template_frame->width;
    height_  = template_frame->height;
    quality_ = quality;
    use_stb_ = false;

    AVPixelFormat src_fmt = (AVPixelFormat)template_frame->format;
    AVPixelFormat dst_fmt;

    if (format == ImageFormat::JPG) {
        // ── JPG: FFmpeg native encoder (YUV → YUVJ → MJPEG) ──
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        if (!codec) {
            snprintf(error_, sizeof(error_), "MJPEG encoder not found");
            return false;
        }
        enc_ctx_ = avcodec_alloc_context3(codec);
        if (!enc_ctx_) {
            snprintf(error_, sizeof(error_), "Failed to alloc encoder");
            return false;
        }
        dst_fmt = AV_PIX_FMT_YUVJ444P;   // 4:4:4, no chroma subsampling → sharp
        enc_ctx_->pix_fmt = dst_fmt;
        enc_ctx_->time_base = { 1, 1 };
        enc_ctx_->width  = width_;
        enc_ctx_->height = height_;
        enc_ctx_->flags |= AV_CODEC_FLAG_QSCALE;
        // Map 1-100 → FFmpeg qscale 1-31 → global_quality (MJPEG divides by FF_QP2LAMBDA=118)
        {
            int qp = (int)(31.0 * (100.0 - quality) / 99.0 + 0.5);
            if (qp < 1) qp = 1;  // 1 = best, 31 = worst
            enc_ctx_->global_quality = qp * 118;  // FF_QP2LAMBDA
        }
        // Optimized Huffman tables for slightly smaller file at same quality
        av_opt_set(enc_ctx_->priv_data, "huffman", "optimal", 0);
        if (avcodec_open2(enc_ctx_, codec, nullptr) < 0) {
            snprintf(error_, sizeof(error_), "Failed to open MJPEG encoder");
            return false;
        }
    } else {
        // ── PNG: stb fallback (FFmpeg PNG encoder not available in this build) ──
        use_stb_ = true;
        dst_fmt  = AV_PIX_FMT_RGB24;
    }

    // ── Scaler: source format → dst format ──
    if (src_fmt != dst_fmt) {
        sws_ = sws_getContext(
            width_, height_, src_fmt,
            width_, height_, dst_fmt,
            SWS_LANCZOS, nullptr, nullptr, nullptr
        );
    }

    // ── Allocate encoder input frame (FFmpeg path only) ──
    if (!use_stb_) {
        enc_frame_ = av_frame_alloc();
        enc_frame_->format = dst_fmt;
        enc_frame_->width  = width_;
        enc_frame_->height = height_;
        if (av_frame_get_buffer(enc_frame_, 0) < 0) {
            snprintf(error_, sizeof(error_), "Failed to alloc encoder frame");
            return false;
        }
        packet_ = av_packet_alloc();
    }

    return true;
}

bool FrameWriter::save_frame(AVFrame* src_frame, const char* path) {
    if (!src_frame || !path) return false;

    // ── Convert to destination format ──
    AVFrame* dst = src_frame;
    uint8_t* rgb_buf = nullptr;

    if (sws_) {
        if (use_stb_) {
            // Allocate RGB buffer for stb
            int stride = width_ * 3;
            rgb_buf = (uint8_t*)av_malloc((size_t)stride * height_);
            if (!rgb_buf) return false;
            uint8_t* planes[1] = { rgb_buf };
            int strides[1] = { stride };
            sws_scale(sws_,
                      (const uint8_t* const*)src_frame->data, src_frame->linesize,
                      0, src_frame->height,
                      planes, strides);
        } else {
            // Use pre-allocated encoder frame
            sws_scale(sws_,
                      (const uint8_t* const*)src_frame->data, src_frame->linesize,
                      0, src_frame->height,
                      enc_frame_->data, enc_frame_->linesize);
            dst = enc_frame_;
        }
    }

    bool ok = false;

    if (use_stb_) {
        // ── stb PNG ──
        ok = (stbi_write_png(path, width_, height_, 3, rgb_buf, width_ * 3) != 0);
    } else {
        // ── FFmpeg encode + write ──
        int ret = avcodec_send_frame(enc_ctx_, dst);
        if (ret < 0) {
            av_strerror(ret, error_, sizeof(error_));
        } else {
            ret = avcodec_receive_packet(enc_ctx_, packet_);
            if (ret < 0) {
                av_strerror(ret, error_, sizeof(error_));
            } else {
                FILE* f = nullptr;
                errno_t err = fopen_s(&f, path, "wb");
                if (err == 0 && f) {
                    fwrite(packet_->data, 1, packet_->size, f);
                    fclose(f);
                    ok = true;
                }
                av_packet_unref(packet_);
            }
        }
    }

    if (rgb_buf) av_free(rgb_buf);
    return ok;
}
