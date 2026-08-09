// align.cpp：按前景包围盒把角色缩放到统一画布，脚底锚点对齐到固定点
#include "image.h"
#include <algorithm>
#include <cstdio>

namespace {

// 依据 alpha 求前景包围盒
bool alphaBBox(const Image& img, int& minX, int& minY, int& maxX, int& maxY) {
    minX = img.w; minY = img.h; maxX = -1; maxY = -1;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            if (img.at(x, y)[3] > 8) {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
    return maxX >= 0;
}

} // namespace

void alignDir(const std::string& inDir, const std::string& outDir,
              int canvasW, int canvasH, double anchorX, double anchorY) {
    printf("[align] %s -> %s (画布 %dx%d, 锚点 %.2f,%.2f)\n",
           inDir.c_str(), outDir.c_str(), canvasW, canvasH, anchorX, anchorY);
    ensureDir(outDir);
    auto files = listPngs(inDir);
    for (const auto& f : files) {
        Image img = loadPng(f);
        int minX, minY, maxX, maxY;
        Image canvas(canvasW, canvasH);
        if (alphaBBox(img, minX, minY, maxX, maxY)) {
            int bw = maxX - minX + 1, bh = maxY - minY + 1;
            // 角色缩放到画布高度的 80%，保持宽高比
            double scale = canvasH * 0.80 / bh;
            int nw = std::max(1, int(bw * scale + 0.5));
            int nh = std::max(1, int(bh * scale + 0.5));
            // 裁出包围盒再缩放
            Image crop(bw, bh);
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    const uint8_t* s = img.at(minX + x, minY + y);
                    uint8_t* d = crop.at(x, y);
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                }
            Image scaled = resizeBilinear(crop, nw, nh);
            // 脚底锚点：包围盒底部中心 -> (anchorX*W, anchorY*H)
            int ox = int(anchorX * canvasW - nw / 2.0 + 0.5);
            int oy = int(anchorY * canvasH - nh + 0.5);
            for (int y = 0; y < nh; ++y) {
                int dy = oy + y;
                if (dy < 0 || dy >= canvasH) continue;
                for (int x = 0; x < nw; ++x) {
                    int dx = ox + x;
                    if (dx < 0 || dx >= canvasW) continue;
                    const uint8_t* s = scaled.at(x, y);
                    uint8_t* d = canvas.at(dx, dy);
                    // alpha 覆盖合成
                    double a = s[3] / 255.0;
                    for (int c = 0; c < 3; ++c)
                        d[c] = uint8_t(s[c] * a + d[c] * (1 - a) + 0.5);
                    d[3] = std::max(d[3], s[3]);
                }
            }
        } else {
            printf("[align] 警告: %s 无前景, 输出空画布\n", f.c_str());
        }
        auto name = f.substr(f.find_last_of("/\\") + 1);
        savePng(canvas, outDir + "/" + name);
        printf("[align] %s 完成\n", name.c_str());
    }
    printf("[align] 完成: %zu 帧\n", files.size());
}
