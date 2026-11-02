#include "validators.h"

#include <unordered_set>

void validate_anime4k_shader_name(const video2x::fsutils::StringType& shader_name) {
    static const std::unordered_set<video2x::fsutils::StringType> valid_anime4k_shaders = {
        STR("anime4k-v4-a"),
        STR("anime4k-v4-a+a"),
        STR("anime4k-v4-b"),
        STR("anime4k-v4-b+b"),
        STR("anime4k-v4-c"),
        STR("anime4k-v4-c+a"),
        STR("anime4k-v4.1-gan")
    };
    if (valid_anime4k_shaders.count(shader_name) == 0 && !std::filesystem::exists(shader_name)) {
        throw po::validation_error(
            po::validation_error::invalid_option_value,
            "libplacebo-shader",
            "libplacebo-shader must be one of: anime4k-v4-a, anime4k-v4-a+a, anime4k-v4-b, "
            "anime4k-v4-b+b, anime4k-v4-c, anime4k-v4-c+a, anime4k-v4.1-gan, or a valid file path"
        );
    }
}

// CHANGE: the built-in names are no longer a hard whitelist. Any model whose ncnn files are
// placed in models/realesrgan/<name>-x<scale>.param/.bin (or models/realcugan/<name>/...) can
// be used, e.g. converted community models. Missing files are still reported clearly by the
// filter's init() ("model param file not found").
void validate_realesrgan_model_name(const video2x::fsutils::StringType& model_name) {
    if (model_name.empty() || model_name.find(STR("..")) != video2x::fsutils::StringType::npos) {
        throw po::validation_error(
            po::validation_error::invalid_option_value,
            "realesrgan-model",
            "realesrgan-model must be a model name, e.g. realesr-animevideov3, "
            "realesr-generalv3, realesrgan-plus-anime, realesrgan-plus, or a custom model "
            "in models/realesrgan"
        );
    }
}

void validate_realcugan_model_name(const video2x::fsutils::StringType& model_name) {
    if (model_name.empty() || model_name.find(STR("..")) != video2x::fsutils::StringType::npos) {
        throw po::validation_error(
            po::validation_error::invalid_option_value,
            "realcugan-model",
            "realcugan-model must be a model folder name, e.g. models-se, models-pro, "
            "models-nose, or a custom folder in models/realcugan"
        );
    }
}

void validate_rife_model_name(const video2x::fsutils::StringType& model_name) {
    static const std::unordered_set<video2x::fsutils::StringType> valid_rife_models = {
        STR("rife"),
        STR("rife-HD"),
        STR("rife-UHD"),
        STR("rife-anime"),
        STR("rife-v2"),
        STR("rife-v2.3"),
        STR("rife-v2.4"),
        STR("rife-v3.0"),
        STR("rife-v3.1"),
        STR("rife-v4"),
        STR("rife-v4.6"),
        STR("rife-v4.25"),
        STR("rife-v4.25-lite"),
        STR("rife-v4.26"),
    };
    if (valid_rife_models.count(model_name) == 0) {
        throw po::validation_error(
            po::validation_error::invalid_option_value,
            "rife-model",
            "RIFE model must be one of: rife, rife-HD, rife-UHD, rife-anime, rife-v2, rife-v2.3, "
            "rife-v2.4, rife-v3.0, rife-v3.1, rife-v4, rife-v4.6, rife-v4.25, rife-v4.25-lite, "
            "rife-v4.26"
        );
    }
}
