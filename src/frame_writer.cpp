#include "frame_writer.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
}

FrameWriter::~FrameWriter() {
    if (enc_ctx_)       avcodec_free_context(&enc_ctx_);
    if (enc_frame_)     av_frame_free(&enc_frame_);
    if (sws_)           sws_freeContext(sws_);
    if (packet_)        av_packet_free(&packet_);
}

bool FrameWriter::init(AVFrame* template_frame, ImageFormat format, int quality) {
    format_ = format;
    enc_width_  = template_frame->width;
    enc_height_ = template_frame->height;

    // ── Pick encoder ──
    AVCodecID codec_id = (format == ImageFormat::JPG) ? AV_CODEC_ID_MJPEG : AV_CODEC_ID_PNG;
    const AVCodec* codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        snprintf(error_, sizeof(error_), "Encoder not found for %s",
                 format == ImageFormat::JPG ? "JPEG" : "PNG");
        return false;
    }

    enc_ctx_ = avcodec_alloc_context3(codec);
    if (!enc_ctx_) {
        snprintf(error_, sizeof(error_), "Failed to allocate encoder context");
        return false;
    }

    // ── Determine pixel format ──
    AVPixelFormat src_fmt = (AVPixelFormat)template_frame->format;
    AVPixelFormat dst_fmt;

    if (format == ImageFormat::JPG) {
        // JPEG encoder works best with YUVJ420P (full-range YUV 4:2:0)
        dst_fmt = AV_PIX_FMT_YUVJ420P;
        enc_ctx_->pix_fmt = dst_fmt;
        enc_ctx_->time_base = { 1, 1 };
        enc_ctx_->width  = enc_width_;
        enc_ctx_->height = enc_height_;
        enc_ctx_->flags |= AV_CODEC_FLAG_QSCALE;
        // Map quality 1-100 to FFmpeg qscale 31-1 (inverted)
        enc_ctx_->global_quality = (int)(31.0 * (100.0 - quality) / 99.0 + 0.5);
    } else {
        // PNG encoder expects RGB
        dst_fmt = AV_PIX_FMT_RGB24;
        enc_ctx_->pix_fmt = dst_fmt;
        enc_ctx_->time_base = { 1, 1 };
        enc_ctx_->width  = enc_width_;
        enc_ctx_->height = enc_height_;
    }

    if (avcodec_open2(enc_ctx_, codec, nullptr) < 0) {
        snprintf(error_, sizeof(error_), "Failed to open encoder");
        return false;
    }

    // ── Allocate encoder input frame ──
    enc_frame_ = av_frame_alloc();
    enc_frame_->format = dst_fmt;
    enc_frame_->width  = enc_width_;
    enc_frame_->height = enc_height_;
    if (av_frame_get_buffer(enc_frame_, 0) < 0) {
        snprintf(error_, sizeof(error_), "Failed to allocate encoder frame buffer");
        return false;
    }

    // ── Scaler: source format → encoder format ──
    if (src_fmt != dst_fmt || enc_width_ != template_frame->width || enc_height_ != template_frame->height) {
        sws_ = sws_getContext(
            enc_width_, enc_height_, src_fmt,
            enc_width_, enc_height_, dst_fmt,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
    }

    // ── Allocate output packet ──
    packet_ = av_packet_alloc();

    return true;
}

bool FrameWriter::encode_and_write(AVFrame* src_frame, const char* path) {
    AVFrame* frame_to_encode = src_frame;

    // Scale if needed
    if (sws_) {
        sws_scale(sws_,
                  (const uint8_t* const*)src_frame->data, src_frame->linesize,
                  0, src_frame->height,
                  enc_frame_->data, enc_frame_->linesize);
        frame_to_encode = enc_frame_;
    }

    // Encode
    int ret = avcodec_send_frame(enc_ctx_, frame_to_encode);
    if (ret < 0) {
        av_strerror(ret, error_, sizeof(error_));
        return false;
    }

    ret = avcodec_receive_packet(enc_ctx_, packet_);
    if (ret < 0) {
        av_strerror(ret, error_, sizeof(error_));
        return false;
    }

    // Write to file
    FILE* f = nullptr;
    errno_t err = fopen_s(&f, path, "wb");
    if (err != 0 || !f) {
        snprintf(error_, sizeof(error_), "Cannot open output file: %s", path);
        av_packet_unref(packet_);
        return false;
    }
    fwrite(packet_->data, 1, packet_->size, f);
    fclose(f);

    av_packet_unref(packet_);
    return true;
}

bool FrameWriter::save_frame(AVFrame* frame, const char* path) {
    if (!frame || !path || !enc_ctx_) return false;
    return encode_and_write(frame, path);
}
