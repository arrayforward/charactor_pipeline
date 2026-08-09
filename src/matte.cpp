// matte.cpp：去背（亮度+饱和度双阈值）→ 连通域去碎屑 → color bleed → alpha 羽化
#include "image.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <queue>
#include <vector>

namespace {

void matteOne(Image& img, double bgLum, double bgSat, double feather) {
    int w = img.w, h = img.h, n = w * h;

    // 1) 双阈值背景分割：亮且低饱和 => 背景
    std::vector<uint8_t> fg(n, 0);
    for (int i = 0; i < n; ++i) {
        const uint8_t* p = &img.px[size_t(i) * 4];
        fg[i] = (lumOf(p) < bgLum || satOf(p) > bgSat) ? 1 : 0;
    }

    // 2) 连通域分析，只保留最大连通域（去除碎屑噪点）
    std::vector<int> label(n, -1);
    int bestLabel = -1, bestSize = 0;
    int cur = 0;
    std::queue<int> q;
    const int dxs[4] = {1, -1, 0, 0}, dys[4] = {0, 0, 1, -1};
    for (int i = 0; i < n; ++i) {
        if (!fg[i] || label[i] >= 0) continue;
        int size = 0;
        label[i] = cur;
        q.push(i);
        while (!q.empty()) {
            int v = q.front(); q.pop();
            ++size;
            int x = v % w, y = v / w;
            for (int d = 0; d < 4; ++d) {
                int nx = x + dxs[d], ny = y + dys[d];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                int u = ny * w + nx;
                if (fg[u] && label[u] < 0) { label[u] = cur; q.push(u); }
            }
        }
        if (size > bestSize) { bestSize = size; bestLabel = cur; }
        ++cur;
    }
    std::vector<uint8_t> alpha(n, 0);
    for (int i = 0; i < n; ++i)
        if (label[i] == bestLabel) alpha[i] = 255;

    // 3) color bleed：把前景颜色向透明区逐层扩散，消除白底残留光晕
    //    （多轮 4 邻域扩张平均，轮数覆盖羽化半径即可）
    int rounds = std::max(4, int(std::ceil(feather * 3)) + 2);
    for (int r = 0; r < rounds; ++r) {
        std::vector<uint8_t> newA = alpha;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int i = y * w + x;
                if (alpha[i]) continue;
                double sr = 0, sg = 0, sb = 0; int cnt = 0;
                for (int d = 0; d < 4; ++d) {
                    int nx = x + dxs[d], ny = y + dys[d];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    int u = ny * w + nx;
                    if (alpha[u]) {
                        const uint8_t* p = &img.px[size_t(u) * 4];
                        sr += p[0]; sg += p[1]; sb += p[2]; ++cnt;
                    }
                }
                if (cnt) {
                    uint8_t* p = &img.px[size_t(i) * 4];
                    p[0] = uint8_t(sr / cnt + 0.5);
                    p[1] = uint8_t(sg / cnt + 0.5);
                    p[2] = uint8_t(sb / cnt + 0.5);
                    newA[i] = 255; // 标记已浸染，继续向外扩
                }
            }
        alpha = newA;
    }
    // 恢复硬 alpha（扩散只是借 alpha 做标记）
    for (int i = 0; i < n; ++i) alpha[i] = (label[i] == bestLabel) ? 255 : 0;

    // 4) 边缘高斯羽化：模糊 alpha 场
    if (feather > 0) {
        std::vector<float> af(n);
        for (int i = 0; i < n; ++i) af[i] = float(alpha[i]);
        gaussianBlurField(af, w, h, feather);
        for (int i = 0; i < n; ++i) alpha[i] = uint8_t(std::clamp(af[i] + 0.5f, 0.0f, 255.0f));
    }

    for (int i = 0; i < n; ++i) img.px[size_t(i) * 4 + 3] = alpha[i];
}

} // namespace

void matteDir(const std::string& inDir, const std::string& outDir,
              double bgLum, double bgSat, double feather) {
    printf("[matte] %s -> %s (bg-lum=%.0f bg-sat=%.2f feather=%.1f)\n",
           inDir.c_str(), outDir.c_str(), bgLum, bgSat, feather);
    ensureDir(outDir);
    auto files = listPngs(inDir);
    for (const auto& f : files) {
        Image img = loadPng(f);
        matteOne(img, bgLum, bgSat, feather);
        auto name = f.substr(f.find_last_of("/\\") + 1);
        savePng(img, outDir + "/" + name);
        printf("[matte] %s 完成\n", name.c_str());
    }
    printf("[matte] 完成: %zu 帧\n", files.size());
}
