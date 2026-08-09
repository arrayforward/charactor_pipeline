// cycle.cpp：前景剪影自相似度扫描检测步态周期 + 周期内等间隔采样
#include "image.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

// 缩小为 32x32 的剪影（alpha 覆盖率），用于姿态相似度比较
std::vector<float> silhouette(const Image& img) {
    const int S = 32;
    std::vector<float> sil(S * S, 0.0f);
    double sx = double(img.w) / S, sy = double(img.h) / S;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            int x0 = int(x * sx), x1 = std::max(x0 + 1, int((x + 1) * sx));
            int y0 = int(y * sy), y1 = std::max(y0 + 1, int((y + 1) * sy));
            x1 = std::min(x1, img.w); y1 = std::min(y1, img.h);
            double sum = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx)
                    sum += img.at(xx, yy)[3] / 255.0;
            sil[y * S + x] = float(sum / ((x1 - x0) * (y1 - y0)));
        }
    return sil;
}

double silDist(const std::vector<float>& a, const std::vector<float>& b) {
    double d = 0;
    for (size_t i = 0; i < a.size(); ++i) d += std::fabs(a[i] - b[i]);
    return d / a.size();
}

} // namespace

void detectCycle(const std::string& framesDir) {
    printf("[cycle] 扫描 %s\n", framesDir.c_str());
    auto files = listPngs(framesDir);
    if (files.size() < 4) throw std::runtime_error("cycle: 帧数太少（<4）");
    std::vector<std::vector<float>> sils;
    for (const auto& f : files) sils.push_back(silhouette(loadPng(f)));
    int n = int(sils.size());

    // 对每个候选周期 p，计算 frame[i] 与 frame[i+p] 的平均剪影距离
    int minP = 2, maxP = n / 2;
    int bestP = -1;
    double bestScore = 1e9;
    printf("[cycle] 周期候选得分（越小越像）:\n");
    for (int p = minP; p <= maxP; ++p) {
        double sum = 0;
        int cnt = n - p;
        for (int i = 0; i < cnt; ++i) sum += silDist(sils[i], sils[i + p]);
        double score = sum / cnt;
        printf("[cycle]   p=%2d  score=%.4f\n", p, score);
        if (score < bestScore) { bestScore = score; bestP = p; }
    }
    if (bestP < 0) throw std::runtime_error("cycle: 无法判定周期");
    printf("[cycle] 检测到周期: %d 帧 (score=%.4f)\n", bestP, bestScore);
    printf("[cycle] 建议周期边界: ");
    for (int s = 0; s + bestP < n; s += bestP) printf("%d->%d  ", s, s + bestP);
    printf("\n");
    printf("[cycle] 示例: sample --frames %s --start 0 --end %d --count 12 --out sampled/\n",
           framesDir.c_str(), bestP);
}

void sampleFrames(const std::string& framesDir, int start, int end, int count,
                  const std::string& outDir) {
    printf("[sample] %s [%d,%d] 等间隔取 %d 帧 -> %s\n",
           framesDir.c_str(), start, end, count, outDir.c_str());
    auto files = listPngs(framesDir);
    if (end < 0) end = int(files.size()) - 1;
    if (start < 0 || end >= int(files.size()) || start > end)
        throw std::runtime_error("sample: start/end 越界（共 " + std::to_string(files.size()) + " 帧）");
    ensureDir(outDir);
    for (int i = 0; i < count; ++i) {
        // 等间隔取帧（含首尾，线性插值取整）
        int src = start + int(std::floor((end - start) * double(i) / std::max(1, count - 1) + 0.5));
        Image img = loadPng(files[src]);
        savePng(img, outDir + "/" + frameName(i));
        printf("[sample] out frame_%02d <= src 帧 %d\n", i, src);
    }
    printf("[sample] 完成: %d 帧\n", count);
}
