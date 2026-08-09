#include "image.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;

Image loadPng(const std::string& path) {
    int w = 0, h = 0, n = 0;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) throw std::runtime_error("无法加载 PNG: " + path + " (" + stbi_failure_reason() + ")");
    Image img(w, h);
    std::copy(data, data + size_t(w) * h * 4, img.px.begin());
    stbi_image_free(data);
    return img;
}

void savePng(const Image& img, const std::string& path) {
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    if (!stbi_write_png(path.c_str(), img.w, img.h, 4, img.px.data(), img.w * 4))
        throw std::runtime_error("无法写出 PNG: " + path);
}

Image resizeBilinear(const Image& src, int nw, int nh) {
    Image dst(nw, nh);
    double sx = double(src.w) / nw, sy = double(src.h) / nh;
    for (int y = 0; y < nh; ++y) {
        double fy = (y + 0.5) * sy - 0.5;
        int y0 = int(std::floor(fy));
        double ty = fy - y0;
        int y1 = y0 + 1;
        y0 = std::clamp(y0, 0, src.h - 1);
        y1 = std::clamp(y1, 0, src.h - 1);
        for (int x = 0; x < nw; ++x) {
            double fx = (x + 0.5) * sx - 0.5;
            int x0 = int(std::floor(fx));
            double tx = fx - x0;
            int x1 = x0 + 1;
            x0 = std::clamp(x0, 0, src.w - 1);
            x1 = std::clamp(x1, 0, src.w - 1);
            const uint8_t* p00 = src.at(x0, y0);
            const uint8_t* p10 = src.at(x1, y0);
            const uint8_t* p01 = src.at(x0, y1);
            const uint8_t* p11 = src.at(x1, y1);
            uint8_t* d = dst.at(x, y);
            for (int c = 0; c < 4; ++c) {
                double v = p00[c] * (1 - tx) * (1 - ty) + p10[c] * tx * (1 - ty) +
                           p01[c] * (1 - tx) * ty + p11[c] * tx * ty;
                d[c] = uint8_t(std::clamp(v + 0.5, 0.0, 255.0));
            }
        }
    }
    return dst;
}

static std::vector<double> gaussKernel(double sigma) {
    int r = std::max(1, int(std::ceil(3.0 * sigma)));
    std::vector<double> k(2 * r + 1);
    double sum = 0;
    for (int i = -r; i <= r; ++i) {
        k[i + r] = std::exp(-double(i * i) / (2 * sigma * sigma));
        sum += k[i + r];
    }
    for (double& v : k) v /= sum;
    return k;
}

void gaussianBlurRGBA(Image& img, double sigma, bool premult) {
    if (sigma <= 0) return;
    auto k = gaussKernel(sigma);
    int r = int(k.size()) / 2;
    std::vector<double> tmp(size_t(img.w) * img.h * 4);
    auto pass = [&](bool horizontal) {
        const std::vector<uint8_t>* srcPx = nullptr;
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x) {
                double acc[4] = {0, 0, 0, 0};
                double wsum = 0;
                for (int i = -r; i <= r; ++i) {
                    int xx = horizontal ? std::clamp(x + i, 0, img.w - 1) : x;
                    int yy = horizontal ? y : std::clamp(y + i, 0, img.h - 1);
                    double wgt = k[i + r];
                    size_t o = (size_t(yy) * img.w + xx) * 4;
                    double a, cr, cg, cb;
                    if (horizontal) {
                        cr = img.px[o]; cg = img.px[o + 1]; cb = img.px[o + 2]; a = img.px[o + 3];
                    } else {
                        cr = tmp[o]; cg = tmp[o + 1]; cb = tmp[o + 2]; a = tmp[o + 3];
                    }
                    if (premult) {
                        double af = a / 255.0;
                        acc[0] += wgt * cr * af; acc[1] += wgt * cg * af; acc[2] += wgt * cb * af;
                        acc[3] += wgt * a;
                        wsum += wgt;
                    } else {
                        acc[0] += wgt * cr; acc[1] += wgt * cg; acc[2] += wgt * cb; acc[3] += wgt * a;
                    }
                }
                size_t o = (size_t(y) * img.w + x) * 4;
                if (premult && acc[3] > 1e-6) {
                    // 反预乘，颜色按权重归一
                    tmp[o] = acc[0] * 255.0 / acc[3];
                    tmp[o + 1] = acc[1] * 255.0 / acc[3];
                    tmp[o + 2] = acc[2] * 255.0 / acc[3];
                    tmp[o + 3] = acc[3] / wsum;
                } else if (premult) {
                    tmp[o] = tmp[o + 1] = tmp[o + 2] = 0; tmp[o + 3] = 0;
                } else {
                    tmp[o] = acc[0]; tmp[o + 1] = acc[1]; tmp[o + 2] = acc[2]; tmp[o + 3] = acc[3];
                }
            }
        if (!horizontal) { /* 第二趟结果已在 tmp */ }
        (void)srcPx;
    };
    pass(true);
    pass(false);
    for (size_t i = 0; i < img.px.size(); ++i)
        img.px[i] = uint8_t(std::clamp(tmp[i] + 0.5, 0.0, 255.0));
}

void gaussianBlurField(std::vector<float>& f, int w, int h, double sigma) {
    if (sigma <= 0) return;
    auto k = gaussKernel(sigma);
    int r = int(k.size()) / 2;
    std::vector<float> tmp(f.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double acc = 0;
            for (int i = -r; i <= r; ++i)
                acc += k[i + r] * f[size_t(y) * w + std::clamp(x + i, 0, w - 1)];
            tmp[size_t(y) * w + x] = float(acc);
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double acc = 0;
            for (int i = -r; i <= r; ++i)
                acc += k[i + r] * tmp[size_t(std::clamp(y + i, 0, h - 1)) * w + x];
            f[size_t(y) * w + x] = float(acc);
        }
}

void ensureDir(const std::string& dir) { fs::create_directories(dir); }

std::vector<std::string> listPngs(const std::string& dir) {
    std::vector<std::string> out;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file()) {
            auto ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".png") out.push_back(e.path().string());
        }
    std::sort(out.begin(), out.end());
    return out;
}

std::string frameName(int idx, const char* prefix) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s_%02d.png", prefix, idx);
    return buf;
}
