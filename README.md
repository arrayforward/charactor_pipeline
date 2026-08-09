# charactor_pipeline

2D sprite 角色生成管线工具（C++17，零外部依赖）。设计文档见 [docs/thinking.txt](docs/thinking.txt)：一条"AI 生成 + 确定性后处理"的暗黑like角色 sprite 管线——**AI 环节（角色设定图 → 图生视频 → 逐帧高清重绘）由外部模型完成，本工具负责全部确定性后处理与运行时渲染演示**。

## 构建（WSL / Ubuntu-22.04）

```bash
cd /mnt/d/agent/pipeline/charactor
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 可执行文件: build/charactor_pipeline
```

依赖仅 stb（已 vendor 在 `third_party/stb/`），cmake ≥ 3.16、g++ 支持 C++17 即可。

## 管线流程

```
AI 环节（工具之外）                确定性后处理（本工具）
┌──────────────┐
│ 角色设定图    │ 文生图
└──────┬───────┘
       ▼
┌──────────────┐
│ 图生视频      │ 相机锁定、原地动作
└──────┬───────┘
       ▼
┌──────────────┐     ┌──────────┐   ┌──────────┐   ┌──────────┐
│ 逐帧高清重绘  │────▶│ slice    │──▶│ matte    │──▶│ align    │
│ (得到帧表/帧) │     │ 切帧+bbox│   │ 去背/羽化 │   │ 锚点对齐  │
└──────────────┘     └──────────┘   └──────────┘   └────┬─────┘
                                                        ▼
              ┌──────────┐   ┌──────────┐   ┌──────────┐
              │ render   │◀──│ atlas    │◀──│ normal   │
              │ 光照演示  │   │ 打图集   │   │ 法线估计  │
              └──────────┘   └──────────┘   └──────────┘

视频帧序列分支: cycle（剪影自相似检测步态周期）→ sample（周期内等间隔采样）
```

## 子命令

参数风格均为 `--key value`。

```bash
# 生成 4×3 合成测试帧表（白底简笔角色，腿按正弦相位摆动），用于无 AI 素材自测
charactor_pipeline gensheet --out sheet.png [--cols 4 --rows 3 --cell 256]

# 切帧：按网格切开并逐格检测前景包围盒（带 4px padding 裁剪输出）
charactor_pipeline slice --sheet sheet.png --cols 4 --rows 3 --out sliced/

# 去背：亮度+饱和度双阈值分割 → 保留最大连通域去碎屑 → color bleed 消白边 → 高斯羽化 alpha
charactor_pipeline matte --in sliced/ --out matted/ [--bg-lum 235 --bg-sat 0.12 --feather 1.5]

# 锚点对齐：按前景包围盒缩放到统一画布（角色高=画布 80%），脚底锚点对齐 (0.5, 0.86)
charactor_pipeline align --in matted/ --out aligned/ --canvas 256x256 [--anchor 0.5,0.86]

# 法线估计：亮度高度场 → 高斯平滑 → 梯度求切线空间法线
charactor_pipeline normal --in aligned/ --out normals/ [--strength 2.0 --blur 2.0]

# 打图集：颜色图集 + 法线图集（网格排布）+ JSON 元数据
charactor_pipeline atlas --in aligned/ --normals normals/ --cols 4 \
    --out atlas_color.png --normals-out atlas_normal.png --meta atlas.json [--fps 11]

# 周期检测：对 PNG 帧序列做前景剪影自相似扫描，打印周期候选得分与边界建议
charactor_pipeline cycle --frames video_frames/

# 周期内等间隔采样 N 帧（end 缺省为最后一帧）
charactor_pipeline sample --frames video_frames/ --start 27 --end 50 --count 12 --out sampled/

# 渲染演示：环境光 + 黄昏斜阳 + 移动点光源（法线 lambert+高光），
# 角色沿对角线从右上走到左下循环，输出 PNG 帧序列 + 可循环播放的 index.html
charactor_pipeline render --atlas atlas_color.png --normals atlas_normal.png \
    --meta atlas.json --out demo/ [--frames 48]

# 一键串起 slice→matte→align→normal→atlas→render
charactor_pipeline all --sheet sheet.png --cols 4 --rows 3 --out workdir/
```

## 端到端自测（无 AI 素材）

```bash
./build/charactor_pipeline gensheet --out test_out/sheet.png --cols 4 --rows 3
./build/charactor_pipeline all --sheet test_out/sheet.png --cols 4 --rows 3 --out test_out/work
# 产物: test_out/work/{atlas_color.png, atlas_normal.png, atlas.json, demo/}
# 浏览器打开 test_out/work/demo/index.html 查看动画
```

## 代码结构

```
src/main.cpp       CLI 子命令分发
src/image.{h,cpp}  RGBA 图、PNG 读写(stb)、双线性缩放、高斯模糊
src/sheet.cpp      合成帧表生成 / 切帧 + 前景包围盒
src/matte.cpp      去背 + 连通域 + color bleed + 羽化
src/align.cpp      包围盒缩放 + 锚点对齐
src/normalmap.cpp  亮度高度场法线估计
src/atlas.cpp      图集打包 + JSON 元数据
src/cycle.cpp      步态周期检测 / 等间隔采样
src/render.cpp     2D 动态光照渲染演示
```
