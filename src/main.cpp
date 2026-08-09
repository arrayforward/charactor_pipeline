// main.cpp：CLI 子命令分发（参数风格 --key value）
#include "image.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace {

struct Args {
    std::map<std::string, std::string> kv;
    std::string get(const char* k, const char* def = "") const {
        auto it = kv.find(k);
        return it == kv.end() ? def : it->second;
    }
    int getInt(const char* k, int def) const {
        auto it = kv.find(k);
        return it == kv.end() ? def : std::stoi(it->second);
    }
    double getDbl(const char* k, double def) const {
        auto it = kv.find(k);
        return it == kv.end() ? def : std::stod(it->second);
    }
    std::string need(const char* k) const {
        auto it = kv.find(k);
        if (it == kv.end()) { fprintf(stderr, "缺少参数 --%s\n", k); std::exit(1); }
        return it->second;
    }
};

Args parseArgs(int argc, char** argv, int from) {
    Args a;
    for (int i = from; i < argc; ++i) {
        std::string k = argv[i];
        if (k.rfind("--", 0) == 0 && i + 1 < argc) a.kv[k.substr(2)] = argv[++i];
        else { fprintf(stderr, "无法解析参数: %s\n", k.c_str()); std::exit(1); }
    }
    return a;
}

// 解析 "256x256" 或 "0.5,0.86"
void parsePair(const std::string& s, double& a, double& b, char sep) {
    auto p = s.find(sep);
    if (p == std::string::npos) { fprintf(stderr, "参数格式错误: %s\n", s.c_str()); std::exit(1); }
    a = std::stod(s.substr(0, p));
    b = std::stod(s.substr(p + 1));
}

void usage() {
    printf(
        "charactor_pipeline - 2D sprite 角色生成管线（确定性后处理部分）\n"
        "用法: charactor_pipeline <子命令> [--key value ...]\n"
        "  gensheet --out sheet.png [--cols 4 --rows 3 --cell 256]\n"
        "           [--character default|luka] [--period 0(整表一周期)]\n"
        "  slice    --sheet in.png --cols 4 --rows 3 --out dir/\n"
        "  matte    --in dir/ --out dir/ [--bg-lum 235 --bg-sat 0.12 --feather 1.5]\n"
        "  align    --in dir/ --out dir/ --canvas 256x256 [--anchor 0.5,0.86]\n"
        "  normal   --in dir/ --out dir/ [--strength 2.0 --blur 2.0]\n"
        "  atlas    --in dir/ --normals dir/ --cols 4 --out color.png\n"
        "           --normals-out normal.png --meta atlas.json [--fps 11]\n"
        "  cycle    --frames dir/\n"
        "  sample   --frames dir/ --start 27 --end 50 --count 12 --out dir/  (end 为排他边界)\n"
        "  render   --atlas color.png --normals normal.png --meta atlas.json --out demo/ [--frames 48]\n"
        "  all      --sheet in.png --cols 4 --rows 3 --out workdir/\n"
        "AI 生成环节（内置，走 MiniMax API + curl/ffmpeg）：\n"
        "  genimage --prompt \"...\" --out ref.png [--model image-01] [--aspect 2:3]\n"
        "           [--config config/pipeline.json]\n"
        "  genvideo --image ref.png --prompt \"...\" --out walk.mp4 [--duration 6]\n"
        "           [--resolution 768P] [--model MiniMax-H3] [--config config/pipeline.json]\n"
        "  extract  --video walk.mp4 --out frames/ [--ffmpeg tools/bin/ffmpeg]\n");
}

void runAll(const Args& a) {
    std::string sheet = a.need("sheet");
    int cols = a.getInt("cols", 4), rows = a.getInt("rows", 3);
    std::string out = a.need("out");
    std::string sliced = out + "/sliced", matted = out + "/matted";
    std::string aligned = out + "/aligned", normals = out + "/normals";
    sliceSheet(sheet, cols, rows, sliced);
    matteDir(sliced, matted, a.getDbl("bg-lum", 235), a.getDbl("bg-sat", 0.12), a.getDbl("feather", 1.5));
    double cw = 256, ch = 256, ax = 0.5, ay = 0.86;
    if (a.kv.count("canvas")) parsePair(a.get("canvas"), cw, ch, 'x');
    if (a.kv.count("anchor")) parsePair(a.get("anchor"), ax, ay, ',');
    alignDir(matted, aligned, int(cw), int(ch), ax, ay);
    normalDir(aligned, normals, a.getDbl("strength", 2.0), a.getDbl("blur", 2.0));
    std::string colorPng = out + "/atlas_color.png";
    std::string normalPng = out + "/atlas_normal.png";
    std::string metaJson = out + "/atlas.json";
    buildAtlas(aligned, normals, cols, colorPng, normalPng, metaJson, a.getInt("fps", 11));
    renderDemo(colorPng, normalPng, metaJson, out + "/demo", a.getInt("frames", 48));
    printf("[all] 全流程完成, 产物目录: %s\n", out.c_str());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];
    Args a = parseArgs(argc, argv, 2);
    try {
        if (cmd == "gensheet") {
            genSheet(a.need("out"), a.getInt("cols", 4), a.getInt("rows", 3), a.getInt("cell", 256),
                     a.get("character", "default"), a.getInt("period", 0));
        } else if (cmd == "slice") {
            sliceSheet(a.need("sheet"), a.getInt("cols", 4), a.getInt("rows", 3), a.need("out"));
        } else if (cmd == "matte") {
            matteDir(a.need("in"), a.need("out"), a.getDbl("bg-lum", 235),
                     a.getDbl("bg-sat", 0.12), a.getDbl("feather", 1.5));
        } else if (cmd == "align") {
            double cw = 0, ch = 0, ax = 0.5, ay = 0.86;
            parsePair(a.need("canvas"), cw, ch, 'x');
            if (a.kv.count("anchor")) parsePair(a.get("anchor"), ax, ay, ',');
            alignDir(a.need("in"), a.need("out"), int(cw), int(ch), ax, ay);
        } else if (cmd == "normal") {
            normalDir(a.need("in"), a.need("out"), a.getDbl("strength", 2.0), a.getDbl("blur", 2.0));
        } else if (cmd == "atlas") {
            buildAtlas(a.need("in"), a.get("normals"), a.getInt("cols", 4), a.need("out"),
                       a.get("normals-out", "normals_atlas.png"), a.need("meta"), a.getInt("fps", 11));
        } else if (cmd == "cycle") {
            detectCycle(a.need("frames"));
        } else if (cmd == "sample") {
            sampleFrames(a.need("frames"), a.getInt("start", 0),
                         a.getInt("end", -1), a.getInt("count", 12), a.need("out"));
        } else if (cmd == "render") {
            renderDemo(a.need("atlas"), a.need("normals"), a.need("meta"), a.need("out"),
                       a.getInt("frames", 48));
        } else if (cmd == "genimage") {
            aiGenImage(a.need("prompt"), a.need("out"), a.get("model"),
                       a.get("aspect", "2:3"), a.get("config", "config/pipeline.json"));
        } else if (cmd == "genvideo") {
            aiGenVideo(a.need("image"), a.need("prompt"), a.need("out"),
                       a.getInt("duration", 6), a.get("resolution", "768P"),
                       a.get("config", "config/pipeline.json"), a.get("model"));
        } else if (cmd == "extract") {
            aiExtractFrames(a.need("video"), a.need("out"), a.get("ffmpeg"));
        } else if (cmd == "all") {
            runAll(a);
        } else {
            fprintf(stderr, "未知子命令: %s\n", cmd.c_str());
            usage();
            return 1;
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
    return 0;
}
