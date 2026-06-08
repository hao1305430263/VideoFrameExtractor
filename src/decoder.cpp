#include "decoder.h"
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavutil/error.h>
}

VideoDecoder::VideoDecoder() {
    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
}

VideoDecoder::~VideoDecoder() {
    close();
    av_frame_free(&frame_);
    av_packet_free(&packet_);
}

void VideoDecoder::set_error(const char* msg) {
    strncpy(error_, msg, sizeof(error_) - 1);
    error_[sizeof(error_) - 1] = '\0';
}

void VideoDecoder::close() {
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
        fmt_ctx_ = nullptr;
    }
    codec_ = nullptr;
    video_stream_idx_ = -1;
    info_ = VideoInfo{};
}

bool VideoDecoder::open(const char* path, VideoInfo* out_info) {
    close();

    // Open input file
    int ret = avformat_open_input(&fmt_ctx_, path, nullptr, nullptr);
    if (ret < 0) {
        av_strerror(ret, error_, sizeof(error_));
        return false;
    }

    // Find stream info
    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0) {
        av_strerror(ret, error_, sizeof(error_));
        close();
        return false;
    }

    // Find the best video stream
    video_stream_idx_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec_, 0);
    if (video_stream_idx_ < 0) {
        set_error("No video stream found in file");
        close();
        return false;
    }

    // Allocate codec context
    codec_ctx_ = avcodec_alloc_context3(codec_);
    if (!codec_ctx_) {
        set_error("Failed to allocate codec context");
        close();
        return false;
    }

    // Copy codec parameters
    ret = avcodec_parameters_to_context(codec_ctx_, fmt_ctx_->streams[video_stream_idx_]->codecpar);
    if (ret < 0) {
        av_strerror(ret, error_, sizeof(error_));
        close();
        return false;
    }

    // Enable multi-threaded decoding for performance
    codec_ctx_->thread_count = 0;  // auto-detect optimal thread count

    // Open codec
    ret = avcodec_open2(codec_ctx_, codec_, nullptr);
    if (ret < 0) {
        av_strerror(ret, error_, sizeof(error_));
        close();
        return false;
    }

    // Populate video info
    AVStream* stream = fmt_ctx_->streams[video_stream_idx_];
    info_.duration_sec = (double)fmt_ctx_->duration / (double)AV_TIME_BASE;
    info_.fps = av_q2d(stream->avg_frame_rate);
    info_.total_frames = stream->nb_frames;
    info_.width = codec_ctx_->width;
    info_.height = codec_ctx_->height;

    // Estimate total frames from duration if nb_frames is 0
    if (info_.total_frames <= 0 && info_.fps > 0) {
        info_.total_frames = (int64_t)(info_.duration_sec * info_.fps);
    }

    // Estimate FPS from time base if avg_frame_rate is not reliable
    if (info_.fps <= 0.0 && stream->avg_frame_rate.den > 0) {
        info_.fps = (double)stream->avg_frame_rate.num / (double)stream->avg_frame_rate.den;
    }
    if (info_.fps <= 0.0) {
        info_.fps = 30.0;  // fallback
    }

    if (out_info) *out_info = info_;
    return true;
}

bool VideoDecoder::seek(double seconds) {
    if (!fmt_ctx_ || video_stream_idx_ < 0) return false;

    // Flush codec buffers
    avcodec_flush_buffers(codec_ctx_);

    AVStream* stream = fmt_ctx_->streams[video_stream_idx_];
    int64_t timestamp = av_rescale_q(
        (int64_t)(seconds * AV_TIME_BASE),
        AV_TIME_BASE_Q,
        stream->time_base
    );

    int ret = av_seek_frame(fmt_ctx_, video_stream_idx_, timestamp,
                            AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        av_strerror(ret, error_, sizeof(error_));
        return false;
    }

    return true;
}

AVRational VideoDecoder::time_base() const {
    if (fmt_ctx_ && video_stream_idx_ >= 0) {
        return fmt_ctx_->streams[video_stream_idx_]->time_base;
    }
    return {1, 1};
}

bool VideoDecoder::decode_packet(AVFrame** out_frame) {
    int ret = avcodec_receive_frame(codec_ctx_, frame_);
    if (ret == 0) {
        *out_frame = frame_;
        return true;
    }
    if (ret == AVERROR(EAGAIN)) {
        return false;  // need more packets
    }
    if (ret == AVERROR_EOF) {
        return false;
    }
    av_strerror(ret, error_, sizeof(error_));
    return false;
}

bool VideoDecoder::decode_next_frame(AVFrame** out_frame) {
    if (!fmt_ctx_ || !codec_ctx_) return false;

    // Try to get a frame from already-sent packets first
    if (decode_packet(out_frame)) return true;

    // Read and send packets until we get a frame
    while (true) {
        int ret = av_read_frame(fmt_ctx_, packet_);
        if (ret == AVERROR_EOF) {
            // Flush decoder
            avcodec_send_packet(codec_ctx_, nullptr);
            return decode_packet(out_frame);
        }
        if (ret < 0) {
            av_strerror(ret, error_, sizeof(error_));
            return false;
        }

        if (packet_->stream_index == video_stream_idx_) {
            ret = avcodec_send_packet(codec_ctx_, packet_);
            av_packet_unref(packet_);
            if (ret < 0) {
                av_strerror(ret, error_, sizeof(error_));
                return false;
            }
            if (decode_packet(out_frame)) return true;
        } else {
            av_packet_unref(packet_);
        }
    }
}
