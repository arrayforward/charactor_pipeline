#pragma once
// 基础图像类型与通用算法：RGBA 图、PNG 读写、双线性缩放、高斯模糊、目录扫描
#include <cstdint>
#include <string>
#include <vector>

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px; // RGBA，按行存储

    Image() = default;
    Image(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * h_ * 4, 0) {}

    uint8_t* at(int x, int y) { return &px[(size_t(y) * w + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &px[(size_t(y) * w + x) * 4]; }
    bool empty() const { return w <= 0 || h <= 0; }
};

// PNG 读写（基于 stb）。加载失败抛出 std::runtime_error。
Image loadPng(const std::string& path);
void savePng(const Image& img, const std::string& path);

// 双线性缩放（RGBA 四通道独立插值）
Image resizeBilinear(const Image& src, int nw, int nh);

// 可分离高斯模糊；premult=true 时按 alpha 预乘加权（用于羽化边缘不发白）
void gaussianBlurRGBA(Image& img, double sigma, bool premult = false);

// 仅模糊单通道浮点场（用于法线图高度场平滑）
void gaussianBlurField(std::vector<float>& f, int w, int h, double sigma);

// 像素的亮度(0-255)与饱和度(0-1)
inline double lumOf(const uint8_t* p) {
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
}
inline double satOf(const uint8_t* p) {
    int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
    int mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
    return mx == 0 ? 0.0 : double(mx - mn) / mx;
}

// 递归创建目录
void ensureDir(const std::string& dir);
// 列出目录下按文件名排序的 .png 文件
std::vector<std::string> listPngs(const std::string& dir);
// 零填充编号文件名，如 frame_03.png
std::string frameName(int idx, const char* prefix = "frame");

// ---- 各管线阶段（实现分散在对应 .cpp） ----

// sheet.cpp：生成合成测试帧表 / 切帧
// character: "default" | "luka"（巡音流歌风格）；period: 一个步态周期占多少帧（<=0 表示整张表一个周期）
void genSheet(const std::string& out, int cols, int rows, int cell,
              const std::string& character = "default", int period = 0);
void sliceSheet(const std::string& sheet, int cols, int rows, const std::string& outDir);

// matte.cpp：去背 + 连通域去碎屑 + 羽化 + color bleed
void matteDir(const std::string& inDir, const std::string& outDir,
              double bgLum, double bgSat, double feather);

// align.cpp：按前景包围盒缩放到统一画布，脚底锚点对齐
void alignDir(const std::string& inDir, const std::string& outDir,
              int canvasW, int canvasH, double anchorX, double anchorY);

// normalmap.cpp：亮度高度场 -> 法线贴图
void normalDir(const std::string& inDir, const std::string& outDir,
               double strength, double blur);

// atlas.cpp：颜色/法线图集 + JSON 元数据
void buildAtlas(const std::string& inDir, const std::string& normalsDir, int cols,
                const std::string& outColor, const std::string& outNormals,
                const std::string& metaPath, int fps);

// cycle.cpp：步态周期检测 / 周期内等间隔采样
void detectCycle(const std::string& framesDir);
void sampleFrames(const std::string& framesDir, int start, int end, int count,
                  const std::string& outDir);

// render.cpp：2D 动态光照渲染演示（输出 PNG 帧序列 + index.html）
void renderDemo(const std::string& atlasPath, const std::string& normalsPath,
                const std::string& metaPath, const std::string& outDir, int frames);
