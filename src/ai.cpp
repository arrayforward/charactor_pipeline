// ai.cpp：内置 AI 生成环节 —— MiniMax 文生图 / 图生视频（异步任务轮询）/ ffmpeg 抽帧
// HTTP 通过 system() 调 curl CLI；JSON 只处理本管线用到的扁平结构，自实现提取/转义。
#include "image.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ---- 极简 JSON 工具（仅覆盖本管线用到的结构） ----

// 把 \uXXXX 追加为 UTF-8
void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

// 解析 j[pos] 处的 '"' 起始字符串，反转义 \" \\ \/ \n \t \r \uXXXX；pos 移到闭引号之后
std::string parseJsonString(const std::string& j, size_t& pos) {
    if (pos >= j.size() || j[pos] != '"') throw std::runtime_error("JSON: 期望字符串 @ " + std::to_string(pos));
    ++pos;
    std::string out;
    while (pos < j.size()) {
        char c = j[pos++];
        if (c == '"') return out;
        if (c == '\\' && pos < j.size()) {
            char e = j[pos++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (pos + 4 > j.size()) throw std::runtime_error("JSON: \\u 转义截断");
                    unsigned cp = unsigned(std::stoul(j.substr(pos, 4), nullptr, 16));
                    pos += 4;
                    appendUtf8(out, cp);
                    break;
                }
                default: out += e; break;
            }
        } else out += c;
    }
    throw std::runtime_error("JSON: 字符串未闭合");
}

// 从 from 起找 "key" 的位置（含引号），找不到返回 npos
size_t findKey(const std::string& j, const std::string& key, size_t from = 0) {
    return j.find("\"" + key + "\"", from);
}

// 从 from 起找 "key" 后面的字符串值（key: "value"），反转义返回
std::string jsonString(const std::string& j, const std::string& key, size_t from = 0) {
    size_t k = findKey(j, key, from);
    if (k == std::string::npos) throw std::runtime_error("JSON: 找不到键 " + key);
    size_t colon = j.find(':', k);
    if (colon == std::string::npos) throw std::runtime_error("JSON: 键 " + key + " 后无冒号");
    size_t q = j.find('"', colon + 1);
    if (q == std::string::npos) throw std::runtime_error("JSON: 键 " + key + " 的值不是字符串");
    return parseJsonString(j, q);
}

// 从 from 起找 "key" 后面的数字值（key: 123）
long jsonNumber(const std::string& j, const std::string& key, size_t from = 0) {
    size_t k = findKey(j, key, from);
    if (k == std::string::npos) return -1;
    size_t colon = j.find(':', k);
    if (colon == std::string::npos) return -1;
    size_t p = j.find_first_of("-0123456789", colon + 1);
    if (p == std::string::npos) return -1;
    return std::stol(j.substr(p));
}

// JSON 字符串转义（构造请求体用）
std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += c;
        }
    }
    return out;
}

// ---- shell 引号（system() 在 WSL/Linux 走 /bin/sh，在 Windows 走 cmd.exe） ----
std::string shq(const std::string& s) {
#ifdef _WIN32
    std::string r = "\"";
    for (char c : s) { if (c == '"') r += "\\\""; else r += c; }
    r += "\"";
    return r;
#else
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    r += "'";
    return r;
#endif
}

// ---- 临时文件 ----
std::string tempFile(const std::string& tag) {
    static int counter = 0;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string name = "cp_ai_" + tag + "_" + std::to_string(now) + "_" + std::to_string(counter++) + ".tmp";
    return (fs::temp_directory_path() / name).string();
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("无法读取文件: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("无法写出文件: " + path);
    f.write(data.data(), std::streamsize(data.size()));
}

// ---- curl HTTP ----
// 发请求，响应体写入 respFile，返回 http_code；body 非空时写临时文件后 -d @file
long curlRequest(const std::string& method, const std::string& url,
                 const std::vector<std::string>& headers, const std::string* body,
                 const std::string& respFile, int timeoutSec) {
    std::string bodyFile, codeFile = tempFile("code");
    std::string cmd = "curl -s -m " + std::to_string(timeoutSec) +
                      " -o " + shq(respFile) + " -w '%{http_code}' -X " + method;
    for (const auto& h : headers) cmd += " -H " + shq(h);
    if (body) {
        bodyFile = tempFile("body");
        writeFile(bodyFile, *body);
        cmd += " -d @" + shq(bodyFile);
    }
    cmd += " " + shq(url) + " > " + shq(codeFile);
    int rc = std::system(cmd.c_str());
    if (!bodyFile.empty()) fs::remove(bodyFile);
    if (rc != 0) {
        fs::remove(codeFile);
        throw std::runtime_error("curl 调用失败（exit=" + std::to_string(rc) + "），请确认 curl 可用");
    }
    std::string code = readFile(codeFile);
    fs::remove(codeFile);
    return code.empty() ? 0 : std::stol(code);
}

// POST/GET JSON API：检查 http_code 2xx 且 base_resp.status_code==0，返回响应体
std::string apiCall(const std::string& method, const std::string& url,
                    const std::string& apiKey, const std::string* body, int timeoutSec) {
    std::string respFile = tempFile("resp");
    std::vector<std::string> headers = {
        "Authorization: Bearer " + apiKey,
        "Content-Type: application/json",
    };
    long code = curlRequest(method, url, headers, body, respFile, timeoutSec);
    std::string resp = fs::exists(respFile) ? readFile(respFile) : "";
    fs::remove(respFile);
    if (code < 200 || code >= 300)
        throw std::runtime_error("API HTTP " + std::to_string(code) + ": " + resp.substr(0, 500));
    long st = jsonNumber(resp, "status_code");
    if (st != 0)
        throw std::runtime_error("API base_resp.status_code=" + std::to_string(st) + ": " + resp.substr(0, 500));
    return resp;
}

// 下载文件（图片/视频），检查 2xx
void downloadFile(const std::string& url, const std::string& out, int timeoutSec) {
    std::string codeFile = tempFile("code");
    std::string cmd = "curl -s -L -m " + std::to_string(timeoutSec) + " -o " + shq(out) +
                      " -w '%{http_code}' " + shq(url) + " > " + shq(codeFile);
    int rc = std::system(cmd.c_str());
    if (rc != 0) { fs::remove(codeFile); throw std::runtime_error("curl 下载失败（exit=" + std::to_string(rc) + "）"); }
    std::string code = readFile(codeFile);
    fs::remove(codeFile);
    long c = code.empty() ? 0 : std::stol(code);
    if (c < 200 || c >= 300)
        throw std::runtime_error("下载 HTTP " + std::to_string(c) + ": " + url.substr(0, 200));
}

// ---- base64（自实现） ----
std::string base64Encode(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        unsigned a = uint8_t(in[i]);
        unsigned b = i + 1 < in.size() ? uint8_t(in[i + 1]) : 0;
        unsigned c = i + 2 < in.size() ? uint8_t(in[i + 2]) : 0;
        out += T[a >> 2];
        out += T[((a & 3) << 4) | (b >> 4)];
        out += i + 1 < in.size() ? T[((b & 15) << 2) | (c >> 6)] : '=';
        out += i + 2 < in.size() ? T[c & 63] : '=';
    }
    return out;
}

// base64/base64url 解码（用于从 JWT 形式的 api_key 里取 GroupID）
std::string base64Decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::string out;
    int acc = 0, bits = 0;
    for (char c : in) {
        int v = val(c);
        if (v < 0) break;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += char((acc >> bits) & 0xFF); }
    }
    return out;
}

// MiniMax 的 api_key 是 JWT，payload 里带 GroupID（files/retrieve 需要）；取不到返回空
std::string groupIdFromJwt(const std::string& apiKey) {
    size_t d1 = apiKey.find('.');
    size_t d2 = d1 == std::string::npos ? d1 : apiKey.find('.', d1 + 1);
    if (d1 == std::string::npos || d2 == std::string::npos) return "";
    std::string payload = base64Decode(apiKey.substr(d1 + 1, d2 - d1 - 1));
    try {
        if (findKey(payload, "GroupID") != std::string::npos) return jsonString(payload, "GroupID");
        if (findKey(payload, "group_id") != std::string::npos) return jsonString(payload, "group_id");
    } catch (...) {
    }
    return "";
}

// ---- 配置：从 config/pipeline.json 提取 minimax 段字段 ----
struct MinimaxConfig {
    std::string baseUrl, apiKey, imageModel, videoModel;
};

MinimaxConfig loadMinimaxConfig(const std::string& configPath) {
    std::string j = readFile(configPath);
    size_t mm = findKey(j, "minimax");
    if (mm == std::string::npos)
        throw std::runtime_error("配置 " + configPath + " 中找不到 minimax 段");
    MinimaxConfig c;
    c.baseUrl = jsonString(j, "base_url", mm);
    c.apiKey = jsonString(j, "api_key", mm);
    c.imageModel = jsonString(j, "image_model", mm);
    c.videoModel = jsonString(j, "video_model", mm);
    return c;
}

// 从 pos 起找下一个以 http 开头的 JSON 字符串
std::string firstUrlAfter(const std::string& j, size_t pos) {
    while (true) {
        size_t q = j.find("\"http", pos);
        if (q == std::string::npos) throw std::runtime_error("JSON: 找不到 http URL");
        return parseJsonString(j, q);
    }
}

} // namespace

void aiGenImage(const std::string& prompt, const std::string& out,
                const std::string& model, const std::string& aspect,
                const std::string& configPath) {
    MinimaxConfig cfg = loadMinimaxConfig(configPath);
    std::string useModel = model.empty() ? cfg.imageModel : model;
    printf("[genimage] model=%s aspect=%s -> %s\n", useModel.c_str(), aspect.c_str(), out.c_str());
    std::string body = "{\"model\":\"" + jsonEscape(useModel) + "\","
                       "\"prompt\":\"" + jsonEscape(prompt) + "\","
                       "\"aspect_ratio\":\"" + jsonEscape(aspect) + "\","
                       "\"response_format\":\"url\",\"n\":1,\"prompt_optimizer\":false}";
    std::string resp = apiCall("POST", cfg.baseUrl + "/v1/image_generation", cfg.apiKey, &body, 120);
    size_t arr = findKey(resp, "image_urls");
    if (arr == std::string::npos)
        throw std::runtime_error("genimage: 响应无 image_urls: " + resp.substr(0, 500));
    std::string url = firstUrlAfter(resp, arr);
    printf("[genimage] 下载图片 ...\n");
    fs::path p(out);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    downloadFile(url, out, 300);
    printf("[genimage] 完成: %s\n", out.c_str());
}

void aiGenVideo(const std::string& image, const std::string& prompt,
                const std::string& out, int duration, const std::string& resolution,
                const std::string& configPath, const std::string& model) {
    MinimaxConfig cfg = loadMinimaxConfig(configPath);
    std::string useModel = model.empty() ? cfg.videoModel : model;
    // MiniMax-H3 用 /v2 多模态 content[] 结构；Hailuo/I2V 等旧模型用 /v1 的
    // prompt + first_frame_image 结构，且成功后要走 file_id -> files/retrieve 取下载地址
    bool isH3 = useModel.find("H3") != std::string::npos;
    printf("[genvideo] model=%s duration=%d resolution=%s -> %s\n",
           useModel.c_str(), duration, resolution.c_str(), out.c_str());
    std::string ext = fs::path(image).extension().string();
    for (auto& c : ext) c = char(::tolower(c));
    std::string mime = (ext == ".jpg" || ext == ".jpeg") ? "image/jpeg" : "image/png";
    std::string dataUrl = "data:" + mime + ";base64," + base64Encode(readFile(image));
    printf("[genvideo] 首帧 %s (%s, base64 %zu 字节)\n", image.c_str(), mime.c_str(), dataUrl.size());

    std::string body, createUrl, queryUrl;
    if (isH3) {
        body = "{\"model\":\"" + jsonEscape(useModel) + "\",\"content\":["
               "{\"type\":\"text\",\"text\":\"" + jsonEscape(prompt) + "\"},"
               "{\"type\":\"image_url\",\"image_url\":{\"url\":\"" + dataUrl + "\"},\"role\":\"first_frame\"}],"
               "\"duration\":" + std::to_string(duration) + ","
               "\"resolution\":\"" + jsonEscape(resolution) + "\"}";
        createUrl = cfg.baseUrl + "/v2/video_generation";
    } else {
        body = "{\"model\":\"" + jsonEscape(useModel) + "\","
               "\"prompt\":\"" + jsonEscape(prompt) + "\","
               "\"first_frame_image\":\"" + dataUrl + "\","
               "\"duration\":" + std::to_string(duration) + ","
               "\"resolution\":\"" + jsonEscape(resolution) + "\"}";
        createUrl = cfg.baseUrl + "/v1/video_generation";
    }
    std::string resp = apiCall("POST", createUrl, cfg.apiKey, &body, 180);
    std::string taskId = jsonString(resp, "task_id");
    printf("[genvideo] task_id=%s，开始轮询（每 10s，总超时 15min）\n", taskId.c_str());
    queryUrl = isH3 ? cfg.baseUrl + "/v2/query/video_generation/" + taskId
                    : cfg.baseUrl + "/v1/query/video_generation?task_id=" + taskId;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);
    std::string videoUrl;
    while (true) {
        std::string qresp = apiCall("GET", queryUrl, cfg.apiKey, nullptr, 60);
        size_t taskPos = findKey(qresp, "task");
        std::string status = jsonString(qresp, "status", taskPos == std::string::npos ? 0 : taskPos);
        printf("[genvideo] status=%s\n", status.c_str());
        bool ok = status == "succeeded" || status == "Success";
        bool bad = status == "failed" || status == "cancelled" || status == "Failed" || status == "Fail";
        if (ok) {
            // H3：task.content.url 直接给成片地址；旧模型：拿 file_id 换下载地址
            size_t contentPos = taskPos == std::string::npos ? 0 : taskPos;
            size_t c2 = findKey(qresp, "content", contentPos);
            if (c2 != std::string::npos) contentPos = c2;
            try {
                videoUrl = firstUrlAfter(qresp, contentPos);
            } catch (...) {
                std::string fileId = jsonString(qresp, "file_id", contentPos);
                std::string groupId = groupIdFromJwt(cfg.apiKey);
                std::string retr = cfg.baseUrl + "/v1/files/retrieve?";
                if (!groupId.empty()) retr += "GroupId=" + groupId + "&";
                retr += "file_id=" + fileId;
                std::string rresp = apiCall("GET", retr, cfg.apiKey, nullptr, 60);
                videoUrl = jsonString(rresp, "download_url");
            }
            break;
        }
        if (bad)
            throw std::runtime_error("genvideo: 任务 " + status + ": " + qresp.substr(0, 500));
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("genvideo: 轮询超时（15 分钟），task_id=" + taskId);
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    printf("[genvideo] 下载视频 ...\n");
    fs::path p(out);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    downloadFile(videoUrl, out, 600);
    printf("[genvideo] 完成: %s\n", out.c_str());
}

void aiExtractFrames(const std::string& video, const std::string& outDir,
                     const std::string& ffmpeg) {
    // ffmpeg 路径优先级：--ffmpeg 参数 > tools/bin/ffmpeg > PATH
    std::string bin = ffmpeg;
    if (bin.empty()) {
#ifdef _WIN32
        if (fs::exists("tools/bin/ffmpeg.exe")) bin = "tools/bin/ffmpeg.exe";
#else
        if (fs::exists("tools/bin/ffmpeg")) bin = "tools/bin/ffmpeg";
#endif
        if (bin.empty()) bin = "ffmpeg";
    }
    ensureDir(outDir);
    std::string pattern = outDir + "/frame_%04d.png";
    std::string cmd = shq(bin) + " -y -hide_banner -loglevel error -i " + shq(video) + " " + shq(pattern);
    printf("[extract] %s\n", cmd.c_str());
    int rc = std::system(cmd.c_str());
    if (rc != 0)
        throw std::runtime_error("ffmpeg 抽帧失败（exit=" + std::to_string(rc) +
                                 "），可用 --ffmpeg 指定路径或放 tools/bin/ffmpeg");
    auto frames = listPngs(outDir);
    printf("[extract] 完成: %zu 帧 -> %s\n", frames.size(), outDir.c_str());
    if (frames.empty()) throw std::runtime_error("extract: 未抽出任何帧");
}
