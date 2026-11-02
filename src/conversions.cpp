#include "conversions.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavutil/pixdesc.h>
}

#include <spdlog/spdlog.h>

#include "logger_manager.h"

// CHANGES vs. 6.4.0 (accuracy + speed of the YUV <-> RGB conversions around the upscaler):
//  * The swscale contexts are cached per thread (sws_getCachedContext) instead of being created
//    and destroyed for every frame (2-3 times per frame before).
//  * Conversions use SWS_ACCURATE_RND and full-resolution chroma interpolation instead of plain
//    SWS_BILINEAR, which reduces rounding error and color bleeding on sharp edges.
//  * The YUV matrix (BT.601/709/2020) and range (limited/full) are taken from the frame metadata
//    instead of always assuming BT.601 limited range, so HD sources are no longer color-shifted.
//  * Frames are converted straight into / out of the ncnn::Mat buffer, removing an intermediate
//    BGR24 frame allocation and a full-frame memcpy in each direction.

namespace video2x {
namespace conversions {

namespace {

constexpr int kSwsFlags =
    SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP;

// Owns one cached SwsContext and frees it when the thread exits
struct SwsCache {
    SwsContext* ctx = nullptr;
    ~SwsCache() {
        if (ctx != nullptr) {
            sws_freeContext(ctx);
            ctx = nullptr;
        }
    }
};

thread_local SwsCache g_generic_cache;  // convert_avframe_pix_fmt
thread_local SwsCache g_to_mat_cache;   // avframe_to_ncnn_mat
thread_local SwsCache g_from_mat_cache; // ncnn_mat_to_avframe

bool is_rgb_like(AVPixelFormat fmt) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
    return desc != nullptr && (desc->flags & AV_PIX_FMT_FLAG_RGB);
}

// Returns a context configured for the given conversion, or nullptr on failure
SwsContext* get_context(
    SwsCache& cache,
    int width,
    int height,
    AVPixelFormat src_fmt,
    AVPixelFormat dst_fmt,
    AVColorSpace colorspace,
    AVColorRange color_range
) {
    cache.ctx = sws_getCachedContext(
        cache.ctx, width, height, src_fmt, width, height, dst_fmt, kSwsFlags, nullptr, nullptr,
        nullptr
    );
    if (cache.ctx == nullptr) {
        return nullptr;
    }

    // Describe the YUV side of the conversion; RGB is always full range.
    // sws_getCoefficients falls back to BT.601 for unspecified/unknown matrices.
    const int* coeffs = sws_getCoefficients(static_cast<int>(colorspace));
    int yuv_full_range = color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    int src_range = is_rgb_like(src_fmt) ? 1 : yuv_full_range;
    int dst_range = is_rgb_like(dst_fmt) ? 1 : yuv_full_range;
    sws_setColorspaceDetails(
        cache.ctx, coeffs, src_range, coeffs, dst_range, 0, 1 << 16, 1 << 16
    );
    return cache.ctx;
}

}  // namespace

// Convert AVFrame format
[[gnu::target_clones("arch=x86-64-v4", "arch=x86-64-v3", "default")]]
AVFrame* convert_avframe_pix_fmt(AVFrame* src_frame, AVPixelFormat pix_fmt) {
    AVFrame* dst_frame = av_frame_alloc();
    if (dst_frame == nullptr) {
        logger()->error("Failed to allocate destination AVFrame.");
        return nullptr;
    }

    dst_frame->format = pix_fmt;
    dst_frame->width = src_frame->width;
    dst_frame->height = src_frame->height;
    dst_frame->colorspace = src_frame->colorspace;
    dst_frame->color_range = src_frame->color_range;
    dst_frame->color_primaries = src_frame->color_primaries;
    dst_frame->color_trc = src_frame->color_trc;

    // Allocate memory for the converted frame
    if (av_frame_get_buffer(dst_frame, 32) < 0) {
        logger()->error("Failed to allocate memory for AVFrame.");
        av_frame_free(&dst_frame);
        return nullptr;
    }

    SwsContext* sws_ctx = get_context(
        g_generic_cache,
        src_frame->width,
        src_frame->height,
        static_cast<AVPixelFormat>(src_frame->format),
        pix_fmt,
        src_frame->colorspace,
        src_frame->color_range
    );
    if (sws_ctx == nullptr) {
        logger()->error("Failed to initialize swscale context.");
        av_frame_free(&dst_frame);
        return nullptr;
    }

    int ret = sws_scale(
        sws_ctx,
        src_frame->data,
        src_frame->linesize,
        0,
        src_frame->height,
        dst_frame->data,
        dst_frame->linesize
    );
    if (ret != dst_frame->height) {
        logger()->error("Failed to convert frame pixel format.");
        av_frame_free(&dst_frame);
        return nullptr;
    }

    return dst_frame;
}

// Convert AVFrame to ncnn::Mat (BGR, 8-bit, interleaved)
[[gnu::target_clones("arch=x86-64-v4", "arch=x86-64-v3", "default")]]
ncnn::Mat avframe_to_ncnn_mat(AVFrame* frame) {
    int width = frame->width;
    int height = frame->height;
    ncnn::Mat ncnn_image = ncnn::Mat(width, height, static_cast<size_t>(3), 3);
    if (ncnn_image.empty()) {
        logger()->error("Failed to allocate ncnn::Mat.");
        return ncnn::Mat();
    }

    // A 2D ncnn::Mat stores its rows contiguously (row(y) = data + y * w * elemsize)
    uint8_t* dst_data[4] = {static_cast<uint8_t*>(ncnn_image.data), nullptr, nullptr, nullptr};
    int dst_linesize[4] = {width * 3, 0, 0, 0};

    if (frame->format == AV_PIX_FMT_BGR24) {
        for (int y = 0; y < height; y++) {
            memcpy(
                dst_data[0] + static_cast<size_t>(y) * dst_linesize[0],
                frame->data[0] + static_cast<ptrdiff_t>(y) * frame->linesize[0],
                static_cast<size_t>(width) * 3
            );
        }
        return ncnn_image;
    }

    SwsContext* sws_ctx = get_context(
        g_to_mat_cache,
        width,
        height,
        static_cast<AVPixelFormat>(frame->format),
        AV_PIX_FMT_BGR24,
        frame->colorspace,
        frame->color_range
    );
    if (sws_ctx == nullptr) {
        logger()->error("Failed to initialize swscale context.");
        return ncnn::Mat();
    }

    int ret = sws_scale(sws_ctx, frame->data, frame->linesize, 0, height, dst_data, dst_linesize);
    if (ret != height) {
        logger()->error("Failed to convert AVFrame to BGR24.");
        return ncnn::Mat();
    }

    return ncnn_image;
}

// Convert ncnn::Mat (BGR, 8-bit, interleaved) to AVFrame with a specified pixel format
[[gnu::target_clones("arch=x86-64-v4", "arch=x86-64-v3", "default")]]
AVFrame* ncnn_mat_to_avframe(
    const ncnn::Mat& mat,
    AVPixelFormat pix_fmt,
    AVColorSpace colorspace,
    AVColorRange color_range
) {
    AVFrame* dst_frame = av_frame_alloc();
    if (!dst_frame) {
        logger()->error("Failed to allocate destination AVFrame.");
        return nullptr;
    }

    dst_frame->format = pix_fmt;
    dst_frame->width = mat.w;
    dst_frame->height = mat.h;
    dst_frame->colorspace = colorspace;
    dst_frame->color_range = color_range;

    if (av_frame_get_buffer(dst_frame, 32) < 0) {
        logger()->error("Failed to allocate memory for destination AVFrame.");
        av_frame_free(&dst_frame);
        return nullptr;
    }

    const uint8_t* src_data[4] = {
        static_cast<const uint8_t*>(mat.data), nullptr, nullptr, nullptr
    };
    int src_linesize[4] = {mat.w * 3, 0, 0, 0};

    if (pix_fmt == AV_PIX_FMT_BGR24) {
        for (int y = 0; y < mat.h; y++) {
            memcpy(
                dst_frame->data[0] + static_cast<ptrdiff_t>(y) * dst_frame->linesize[0],
                src_data[0] + static_cast<size_t>(y) * src_linesize[0],
                static_cast<size_t>(mat.w) * 3
            );
        }
        return dst_frame;
    }

    SwsContext* sws_ctx = get_context(
        g_from_mat_cache, mat.w, mat.h, AV_PIX_FMT_BGR24, pix_fmt, colorspace, color_range
    );
    if (sws_ctx == nullptr) {
        logger()->error("Failed to initialize swscale context.");
        av_frame_free(&dst_frame);
        return nullptr;
    }

    int ret =
        sws_scale(sws_ctx, src_data, src_linesize, 0, mat.h, dst_frame->data, dst_frame->linesize);
    if (ret != dst_frame->height) {
        logger()->error("Failed to convert BGR data to destination pixel format.");
        av_frame_free(&dst_frame);
        return nullptr;
    }

    return dst_frame;
}

}  // namespace conversions
}  // namespace video2x
