// render.cpp：读取图集+法线图+元数据，2D 动态光照渲染行走动画，输出 PNG 帧序列和 index.html
#include "image.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Vec3 { double x, y, z; };
Vec3 norm(Vec3 v) {
    double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x / l, v.y / l, v.z / l};
}
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// 从自写的 meta JSON 里抠整数字段
int metaInt(const std::string& js, const std::string& key) {
    auto p = js.find("\"" + key + "\"");
    if (p == std::string::npos) throw std::runtime_error("meta 缺字段: " + key);
    p = js.find(':', p);
    return std::stoi(js.substr(p + 1));
}

} // namespace

void renderDemo(const std::string& atlasPath, const std::string& normalsPath,
                const std::string& metaPath, const std::string& outDir, int outFrames) {
    printf("[render] 图集 %s + 法线 %s -> %s (%d 帧)\n",
           atlasPath.c_str(), normalsPath.c_str(), outDir.c_str(), outFrames);
    Image colorAtlas = loadPng(atlasPath);
    Image normalAtlas = loadPng(normalsPath);
    std::ifstream mf(metaPath);
    if (!mf) throw std::runtime_error("render: 无法读取 " + metaPath);
    std::stringstream ss; ss << mf.rdbuf();
    std::string js = ss.str();
    int animFrames = metaInt(js, "frames");
    int cols = metaInt(js, "cols");
    int fw = metaInt(js, "frameW"), fh = metaInt(js, "frameH");

    const int W = 640, H = 360;
    ensureDir(outDir);

    // 光照配置：环境光 + 黄昏斜阳（固定方向）+ 移动点光源
    const Vec3 ambient{0.32, 0.27, 0.30};
    const Vec3 sunDir = norm({-0.55, -0.50, 0.62});
    const Vec3 sunColor{0.85, 0.55, 0.32};
    const Vec3 ptColor{1.00, 0.80, 0.50};
    const double ptRadius = 260.0, ptZ = 90.0;

    for (int t = 0; t < outFrames; ++t) {
        double ft = double(t) / outFrames;
        Image frame(W, H);
        // 黄昏渐变背景
        for (int y = 0; y < H; ++y) {
            double g = double(y) / H;
            uint8_t r = uint8_t(150 + (52 - 150) * g);
            uint8_t gg = uint8_t(96 + (40 - 96) * g);
            uint8_t b = uint8_t(120 + (64 - 120) * g);
            for (int x = 0; x < W; ++x) {
                uint8_t* p = frame.at(x, y);
                p[0] = r; p[1] = gg; p[2] = b; p[3] = 255;
            }
        }

        // 角色沿对角线从右上走到左下（脚底世界坐标）
        double footX = W * (0.80 - 0.60 * ft);
        double footY = H * (0.35 + 0.60 * ft);
        int af = int(ft * animFrames) % animFrames; // 动画帧
        int agx = af % cols, agy = af / cols;

        // 点光源沿椭圆轨迹运动
        double la = ft * 4 * M_PI;
        Vec3 light{W * 0.5 + std::cos(la) * W * 0.34,
                   H * 0.55 + std::sin(la) * H * 0.30, ptZ};

        // 脚下软阴影
        double shW = 70.0, shH = 16.0;
        for (int y = int(footY - shH); y <= int(footY + shH); ++y) {
            if (y < 0 || y >= H) continue;
            for (int x = int(footX - shW); x <= int(footX + shW); ++x) {
                if (x < 0 || x >= W) continue;
                double dx = (x - footX) / shW, dy = (y - footY) / shH;
                double d = dx * dx + dy * dy;
                if (d <= 1.0) {
                    uint8_t* p = frame.at(x, y);
                    double a = 0.35 * (1 - d);
                    for (int c = 0; c < 3; ++c) p[c] = uint8_t(p[c] * (1 - a));
                }
            }
        }

        // 逐像素绘制角色（法线 lambert + 高光）
        double scale = 0.9;
        int ox = int(footX - 0.5 * fw * scale);   // 锚点 (0.5, 0.86)
        int oy = int(footY - 0.86 * fh * scale);
        for (int y = 0; y < int(fh * scale); ++y) {
            int sy = std::min(fh - 1, int(y / scale));
            int dy = oy + y;
            if (dy < 0 || dy >= H) continue;
            for (int x = 0; x < int(fw * scale); ++x) {
                int sx = std::min(fw - 1, int(x / scale));
                const uint8_t* sp = colorAtlas.at(agx * fw + sx, agy * fh + sy);
                int a = sp[3];
                if (a == 0) continue;
                int dx = ox + x;
                if (dx < 0 || dx >= W) continue;
                const uint8_t* np = normalAtlas.at(agx * fw + sx, agy * fh + sy);
                Vec3 n = norm({np[0] / 127.5 - 1.0, np[1] / 127.5 - 1.0, np[2] / 127.5 - 1.0});

                // 方向光（黄昏斜阳）
                double sd = std::max(0.0, dot(n, sunDir));
                Vec3 hv = norm({sunDir.x, sunDir.y, sunDir.z + 1.0});
                double sspec = std::pow(std::max(0.0, dot(n, hv)), 24.0) * sd;

                // 点光源
                Vec3 pl{light.x - dx, light.y - dy, light.z};
                double dist = std::sqrt(dot(pl, pl));
                pl = norm(pl);
                double att = std::max(0.0, 1.0 - dist / ptRadius);
                att *= att;
                double pd = std::max(0.0, dot(n, pl)) * att;
                Vec3 phv = norm({pl.x, pl.y, pl.z + 1.0});
                double pspec = std::pow(std::max(0.0, dot(n, phv)), 24.0) * pd;

                double lr = ambient.x + sunColor.x * sd + ptColor.x * pd;
                double lg = ambient.y + sunColor.y * sd + ptColor.y * pd;
                double lb = ambient.z + sunColor.z * sd + ptColor.z * pd;
                double spec = 255.0 * (0.6 * sspec + 0.9 * pspec);
                double af_ = a / 255.0;
                uint8_t* dp = frame.at(dx, dy);
                double cr = std::min(255.0, sp[0] * lr + spec);
                double cg = std::min(255.0, sp[1] * lg + spec);
                double cb = std::min(255.0, sp[2] * lb + spec);
                dp[0] = uint8_t(cr * af_ + dp[0] * (1 - af_));
                dp[1] = uint8_t(cg * af_ + dp[1] * (1 - af_));
                dp[2] = uint8_t(cb * af_ + dp[2] * (1 - af_));
                dp[3] = 255;
            }
        }
        savePng(frame, outDir + "/" + frameName(t));
        if (t % 8 == 0 || t == outFrames - 1)
            printf("[render] %d/%d 帧已输出\n", t + 1, outFrames);
    }

    // 极简 index.html：JS 循环播放 PNG 序列
    std::ofstream html(outDir + "/index.html");
    html << "<!doctype html><html><head><meta charset=\"utf-8\">"
            "<title>charactor_pipeline demo</title></head>"
            "<body style=\"background:#222;text-align:center\">"
            "<img id=\"v\" style=\"image-rendering:pixelated;margin-top:20px\">"
            "<script>\n"
            "const N = " << outFrames << ";\n"
            "const names = Array.from({length:N}, (_,i) =>\n"
            "  'frame_' + String(i).padStart(2,'0') + '.png');\n"
            "const imgs = names.map(s => { const im = new Image(); im.src = s; return im; });\n"
            "let i = 0;\n"
            "setInterval(() => {\n"
            "  document.getElementById('v').src = imgs[i % N].src;\n"
            "  i++;\n"
            "}, 1000 / 12);\n"
            "</script></body></html>\n";
    printf("[render] 完成: %d 帧 + index.html\n", outFrames);
}
