// atlas.cpp：把 N 帧打进颜色图集 + 法线图集（网格排布），输出 JSON 元数据
#include "image.h"
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace {

Image packGrid(const std::vector<std::string>& files, int cols, int fw, int fh,
               int rows, const char* kind) {
    Image atlas(cols * fw, rows * fh);
    for (size_t i = 0; i < files.size(); ++i) {
        Image f = loadPng(files[i]);
        if (f.w != fw || f.h != fh)
            throw std::runtime_error(std::string(kind) + "帧尺寸不一致: " + files[i]);
        int gx = int(i) % cols, gy = int(i) / cols;
        for (int y = 0; y < fh; ++y)
            for (int x = 0; x < fw; ++x) {
                const uint8_t* s = f.at(x, y);
                uint8_t* d = atlas.at(gx * fw + x, gy * fh + y);
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            }
    }
    return atlas;
}

} // namespace

void buildAtlas(const std::string& inDir, const std::string& normalsDir, int cols,
                const std::string& outColor, const std::string& outNormals,
                const std::string& metaPath, int fps) {
    printf("[atlas] 颜色帧: %s, 法线帧: %s, 列数 %d\n", inDir.c_str(), normalsDir.c_str(), cols);
    auto colorFiles = listPngs(inDir);
    if (colorFiles.empty()) throw std::runtime_error("atlas: 输入目录无 PNG: " + inDir);
    Image first = loadPng(colorFiles[0]);
    int fw = first.w, fh = first.h;
    int n = int(colorFiles.size());
    int rows = (n + cols - 1) / cols;

    Image colorAtlas = packGrid(colorFiles, cols, fw, fh, rows, "颜色");
    savePng(colorAtlas, outColor);
    printf("[atlas] 颜色图集: %s (%dx%d, %d 帧)\n", outColor.c_str(), colorAtlas.w, colorAtlas.h, n);

    bool hasNormals = !normalsDir.empty();
    if (hasNormals) {
        auto normalFiles = listPngs(normalsDir);
        if (normalFiles.size() != colorFiles.size())
            throw std::runtime_error("atlas: 法线帧数量与颜色帧不一致");
        Image normalAtlas = packGrid(normalFiles, cols, fw, fh, rows, "法线");
        savePng(normalAtlas, outNormals);
        printf("[atlas] 法线图集: %s (%dx%d)\n", outNormals.c_str(), normalAtlas.w, normalAtlas.h);
    }

    std::ofstream meta(metaPath);
    if (!meta) throw std::runtime_error("atlas: 无法写出 " + metaPath);
    meta << "{\n"
         << "  \"frames\": " << n << ",\n"
         << "  \"cols\": " << cols << ",\n"
         << "  \"rows\": " << rows << ",\n"
         << "  \"frameW\": " << fw << ",\n"
         << "  \"frameH\": " << fh << ",\n"
         << "  \"fps\": " << fps << "\n"
         << "}\n";
    printf("[atlas] 元数据: %s\n", metaPath.c_str());
}
