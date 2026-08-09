// sheet.cpp：合成测试帧表生成 + 帧表切分（逐格检测前景包围盒）
#include "image.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// 填充椭圆
void fillEllipse(Image& img, double cx, double cy, double rx, double ry,
                 uint8_t r, uint8_t g, uint8_t b) {
    int x0 = std::max(0, int(cx - rx - 1)), x1 = std::min(img.w - 1, int(cx + rx + 1));
    int y0 = std::max(0, int(cy - ry - 1)), y1 = std::min(img.h - 1, int(cy + ry + 1));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            double dx = (x + 0.5 - cx) / rx, dy = (y + 0.5 - cy) / ry;
            if (dx * dx + dy * dy <= 1.0) {
                uint8_t* p = img.at(x, y);
                p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
            }
        }
}

void fillCircle(Image& img, double cx, double cy, double r, uint8_t cr, uint8_t cg, uint8_t cb) {
    fillEllipse(img, cx, cy, r, r, cr, cg, cb);
}

// 粗线段（用一串圆逼近，简单直接）
void thickLine(Image& img, double x0, double y0, double x1, double y1, double w,
               uint8_t r, uint8_t g, uint8_t b) {
    double len = std::hypot(x1 - x0, y1 - y0);
    int steps = std::max(1, int(len * 2));
    for (int i = 0; i <= steps; ++i) {
        double t = double(i) / steps;
        fillCircle(img, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, w / 2, r, g, b);
    }
}

// 角色风格（配色与发型特征）
struct CharStyle {
    uint8_t leg[3], shoe[3], body[3], arm[3], skin[3], hair[3], hairDark[3], trim[3];
    bool longHair = false; // 长直发（身后垂落）替代马尾
    bool headset = false;  // 耳机
};

CharStyle defaultStyle() {
    CharStyle s{};
    s.leg[0]=40; s.leg[1]=44; s.leg[2]=64;
    s.shoe[0]=30; s.shoe[1]=30; s.shoe[2]=36;
    s.body[0]=70; s.body[1]=160; s.body[2]=150;
    s.arm[0]=60; s.arm[1]=140; s.arm[2]=132;
    s.skin[0]=240; s.skin[1]=205; s.skin[2]=180;
    s.hair[0]=60; s.hair[1]=50; s.hair[2]=45;
    return s;
}

// 巡音流歌（Megurine Luka）：粉色长直发、深色金边长裙、耳机
CharStyle lukaStyle() {
    CharStyle s = defaultStyle();
    s.leg[0]=30; s.leg[1]=30; s.leg[2]=42;      // 过膝长靴（深色）
    s.shoe[0]=215; s.shoe[1]=175; s.shoe[2]=80; // 金色鞋头
    s.body[0]=38; s.body[1]=38; s.body[2]=56;   // 深色长裙
    s.arm[0]=45; s.arm[1]=45; s.arm[2]=62;      // 黑色袖套
    s.hair[0]=250; s.hair[1]=165; s.hair[2]=185;    // 粉发
    s.hairDark[0]=222; s.hairDark[1]=128; s.hairDark[2]=152;
    s.trim[0]=215; s.trim[1]=175; s.trim[2]=80;     // 金边
    s.longHair = true;
    s.headset = true;
    return s;
}

// 在指定格内画一个"行走"角色：椭圆身体、圆头、正弦摆腿、马尾/长发
void drawCharacter(Image& img, int ox, int oy, int cell, double phase, const CharStyle& st) {
    double u = cell / 256.0;          // 以 256 格为基准的比例尺
    double cx = ox + cell * 0.5;
    double hipY = oy + cell * 0.62;
    double bodyCy = oy + cell * 0.50;

    double headCx = cx, headCy = oy + cell * 0.30, headR = cell * 0.11;

    // 腿：从髋部出发的两段（大腿+小腿），膝关节随相位弯曲
    double legLen = cell * 0.30;
    for (int side = 0; side < 2; ++side) {
        double ph = phase + side * M_PI;
        double swing = std::sin(ph) * 0.55;              // 前后摆角
        double lift = std::max(0.0, std::cos(ph)) * 10 * u; // 摆动相抬脚
        double hipX = cx + (side ? 5 : -5) * u;
        double footX = hipX + std::sin(swing) * legLen;
        double footY = hipY + std::cos(swing) * legLen - lift;
        double kneeX = (hipX + footX) / 2 + 8 * u;
        double kneeY = (hipY + footY) / 2;
        thickLine(img, hipX, hipY, kneeX, kneeY, 13 * u, st.leg[0], st.leg[1], st.leg[2]);
        thickLine(img, kneeX, kneeY, footX, footY, 11 * u, st.leg[0], st.leg[1], st.leg[2]);
        fillEllipse(img, footX + 4 * u, footY, 10 * u, 5 * u, st.shoe[0], st.shoe[1], st.shoe[2]); // 鞋
    }

    // 长直发（画在身体之前 -> 垂在身后；发根部被头/身体覆盖，保持连通）
    if (st.longHair) {
        double sway = std::sin(phase - 0.9) * 9 * u; // 摆动略滞后于腿
        double hx = headCx - headR * 0.2, hy = headCy - headR * 0.5;
        double midX = headCx - headR * 1.8 - sway, midY = oy + cell * 0.45;
        double tipX = headCx - headR * 1.6 - sway * 1.9, tipY = oy + cell * 0.72; // 垂到膝部
        thickLine(img, hx, hy, midX, midY, 18 * u, st.hair[0], st.hair[1], st.hair[2]);
        thickLine(img, midX, midY, tipX, tipY, 13 * u, st.hairDark[0], st.hairDark[1], st.hairDark[2]);
    }

    // 身体
    fillEllipse(img, cx, bodyCy, cell * 0.16, cell * 0.20, st.body[0], st.body[1], st.body[2]);
    if (st.longHair) // 胸前金边装饰
        thickLine(img, cx - cell * 0.10, oy + cell * 0.40, cx + cell * 0.12, oy + cell * 0.42,
                  3 * u, st.trim[0], st.trim[1], st.trim[2]);
    // 手臂（与腿反相摆动）
    for (int side = 0; side < 2; ++side) {
        double ph = phase + (side ? 0 : M_PI);
        double shX = cx + (side ? 13 : -13) * u, shY = oy + cell * 0.40;
        double handX = shX + std::sin(ph) * 0.5 * cell * 0.16;
        double handY = shY + cell * 0.20;
        thickLine(img, shX, shY, handX, handY, 9 * u, st.arm[0], st.arm[1], st.arm[2]);
    }
    // 头
    fillCircle(img, headCx, headCy, headR, st.skin[0], st.skin[1], st.skin[2]);
    fillEllipse(img, headCx, headCy - headR * 0.55, headR * 1.02, headR * 0.62,
                st.hair[0], st.hair[1], st.hair[2]); // 头发
    if (st.headset) { // 耳机：头顶细梁 + 耳罩
        thickLine(img, headCx - headR * 0.7, headCy - headR * 0.55,
                  headCx + headR * 0.55, headCy - headR * 0.8, 3 * u, 40, 40, 48);
        fillCircle(img, headCx - headR * 0.72, headCy + headR * 0.05, headR * 0.30, 45, 45, 55);
    }
    if (!st.longHair) {
        // 马尾（摆动相位略滞后于腿；最后画保证与头部连通）
        double tailSwing = std::sin(phase - 0.9) * 14 * u;
        thickLine(img, headCx - headR * 0.5, headCy - headR * 0.2,
                  headCx - headR * 1.4 - tailSwing, headCy + headR * 1.6, 10 * u,
                  st.hair[0], st.hair[1], st.hair[2]);
        thickLine(img, headCx - headR * 1.4 - tailSwing, headCy + headR * 1.6,
                  headCx - headR * 1.9 - tailSwing * 1.6, headCy + headR * 3.0, 7 * u,
                  st.hair[0], st.hair[1], st.hair[2]);
    }
}

// 判定像素是否接近白色背景（用于切帧时的包围盒检测）
bool isBg(const uint8_t* p) {
    int mx = std::max({p[0], p[1], p[2]});
    int mn = std::min({p[0], p[1], p[2]});
    return mx >= 240 && (mx - mn) <= 12;
}

} // namespace

void genSheet(const std::string& out, int cols, int rows, int cell,
              const std::string& character, int period) {
    CharStyle st = (character == "luka") ? lukaStyle() : defaultStyle();
    int n = cols * rows;
    if (period <= 0) period = n; // 默认整张表一个完整步态周期
    printf("[gensheet] %dx%d 格, 单元 %dpx, 角色=%s, 步态周期=%d帧 -> %s\n",
           cols, rows, cell, character.c_str(), period, out.c_str());
    Image img(cols * cell, rows * cell);
    std::fill(img.px.begin(), img.px.end(), 255); // 白底
    for (int i = 0; i < n; ++i) {
        int gx = i % cols, gy = i / cols;
        double phase = 2.0 * M_PI * (i % period) / period; // 每帧腿相位递进
        drawCharacter(img, gx * cell, gy * cell, cell, phase, st);
    }
    savePng(img, out);
    printf("[gensheet] 完成: %d 帧, 图尺寸 %dx%d\n", n, img.w, img.h);
}

void sliceSheet(const std::string& sheet, int cols, int rows, const std::string& outDir) {
    printf("[slice] %s -> %s (%dx%d)\n", sheet.c_str(), outDir.c_str(), cols, rows);
    Image img = loadPng(sheet);
    if (img.w % cols || img.h % rows)
        printf("[slice] 警告: 图尺寸 %dx%d 不能被 %dx%d 整除, 边缘像素将被忽略\n",
               img.w, img.h, cols, rows);
    int cw = img.w / cols, ch = img.h / rows;
    ensureDir(outDir);
    int idx = 0;
    for (int gy = 0; gy < rows; ++gy)
        for (int gx = 0; gx < cols; ++gx, ++idx) {
            // 前景包围盒检测
            int minX = cw, minY = ch, maxX = -1, maxY = -1;
            for (int y = 0; y < ch; ++y)
                for (int x = 0; x < cw; ++x) {
                    if (!isBg(img.at(gx * cw + x, gy * ch + y))) {
                        minX = std::min(minX, x); maxX = std::max(maxX, x);
                        minY = std::min(minY, y); maxY = std::max(maxY, y);
                    }
                }
            if (maxX < 0) { // 空格：原样输出
                printf("[slice] frame_%02d: 未检测到前景, 整格输出\n", idx);
                minX = minY = 0; maxX = cw - 1; maxY = ch - 1;
            }
            const int pad = 4;
            minX = std::max(0, minX - pad); minY = std::max(0, minY - pad);
            maxX = std::min(cw - 1, maxX + pad); maxY = std::min(ch - 1, maxY + pad);
            int bw = maxX - minX + 1, bh = maxY - minY + 1;
            Image frame(bw, bh);
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    const uint8_t* s = img.at(gx * cw + minX + x, gy * ch + minY + y);
                    uint8_t* d = frame.at(x, y);
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
                }
            savePng(frame, outDir + "/" + frameName(idx));
            printf("[slice] frame_%02d: bbox=(%d,%d %dx%d)\n", idx, minX, minY, bw, bh);
        }
    printf("[slice] 完成: %d 帧\n", idx);
}
