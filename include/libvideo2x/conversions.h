#pragma once

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <mat.h>

namespace video2x {
namespace conversions {

// Convert AVFrame to another pixel format
AVFrame* convert_avframe_pix_fmt(AVFrame* src_frame, AVPixelFormat pix_fmt);

// Convert AVFrame to ncnn::Mat
ncnn::Mat avframe_to_ncnn_mat(AVFrame* frame);

// Convert ncnn::Mat to AVFrame. The color matrix/range describe the YUV output; when left
// unspecified they default to BT.601 limited range (swscale's default).
AVFrame* ncnn_mat_to_avframe(
    const ncnn::Mat& mat,
    AVPixelFormat pix_fmt,
    AVColorSpace colorspace = AVCOL_SPC_UNSPECIFIED,
    AVColorRange color_range = AVCOL_RANGE_UNSPECIFIED
);

}  // namespace conversions
}  // namespace video2x
