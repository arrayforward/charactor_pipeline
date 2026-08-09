// normalmap.cpp：亮度高度场 -> 高斯平滑 -> 梯度求切线空间法线贴图
#include "image.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

void normalDir(const std::string& inDir, const std::string& outDir,
               double strength, double blur) {
    printf("[normal] %s -> %s (strength=%.1f blur=%.1f)\n",
           inDir.c_str(), outDir.c_str(), strength, blur);
    ensureDir(outDir);
    auto files = listPngs(inDir);
    for (const auto& f : files) {
        Image img = loadPng(f);
        int w = img.w, h = img.h, n = w * h;

        // 高度场 = 亮度 * alpha（背景高度为 0）
        std::vector<float> hf(n);
        for (int i = 0; i < n; ++i) {
            const uint8_t* p = &img.px[size_t(i) * 4];
            hf[i] = float(lumOf(p) / 255.0 * (p[3] / 255.0));
        }
        gaussianBlurField(hf, w, h, blur);

        Image out(w, h);
        auto H = [&](int x, int y) {
            return hf[size_t(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)];
        };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                // 中心差分求梯度
                double dx = (H(x + 1, y) - H(x - 1, y)) * 0.5 * strength * 255.0;
                double dy = (H(x, y + 1) - H(x, y - 1)) * 0.5 * strength * 255.0;
                double nx = -dx, ny = -dy, nz = 1.0;
                double len = std::sqrt(nx * nx + ny * ny + nz * nz);
                uint8_t* p = out.at(x, y);
                p[0] = uint8_t((nx / len * 0.5 + 0.5) * 255 + 0.5);
                p[1] = uint8_t((ny / len * 0.5 + 0.5) * 255 + 0.5);
                p[2] = uint8_t((nz / len * 0.5 + 0.5) * 255 + 0.5);
                p[3] = img.at(x, y)[3];
            }
        auto name = f.substr(f.find_last_of("/\\") + 1);
        savePng(out, outDir + "/" + name);
        printf("[normal] %s 完成\n", name.c_str());
    }
    printf("[normal] 完成: %zu 帧\n", files.size());
}
