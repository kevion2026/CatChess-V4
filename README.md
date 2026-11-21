# CatChess V4

**About 10 MB of RAM. One search thread. A model you can train on a laptop.**

CatChess V4 is an open-source Xiangqi engine built around geometric features. V4 Flash puts **214,274 parameters** and **160 geometric features** into a single executable. Its measured working set during play was **9.4 MiB** on our Windows test machine, with a **4 MiB transposition table**.

| Edition | What you get |
|---|---|
| **V4 Flash** | One search thread and a compact model for low-memory play |
| **V4 Pro** | Three parallel search workers and a larger evaluation model |

## The road to 2,850 Elo

**2,850+ Elo is the FSF benchmark target.** In our verified equal-time matches, Flash scored **58.9% against V3.2** across 96 games (32 wins, 49 draws, 15 losses), equivalent to about **+62 Elo relative to V3.2 in that match**. It also won **32 of 32** games against V2 Flash. An absolute FSF rating requires a calibrated FSF result for this build. [Match conditions and full results](flash/RELEASE-NOTES.md)

## Low-cost training

The final Flash CPU training run completed **96 epochs in about 101 seconds** on our local machine, using **264,894 training positions** from an existing labeled dataset. This timing covers the fitting loop. Dataset preparation, label generation, and match testing are separate. The training setup uses a CPU, so experiments can run on a laptop without renting a GPU. [Flash architecture](flash/ARCHITECTURE.md)

**Try it:** Get the [v4.0.0 source](https://github.com/kevion2026/CatChess-V4/archive/refs/tags/v4.0.0.zip), follow the build steps below, and run the engine in a Xiangqi UCI client. Both editions ship with embedded weights. If you run your own match, share the engine builds, hardware, time control, and game records in [Issues](https://github.com/kevion2026/CatChess-V4/issues).

## 中文说明


CatChess V4 是基于几何评估的中国象棋 UCI 引擎。本仓库包含 V4 Pro 和 V4 Flash 的正式发行源码、内置权重和构建配置。

| 型号 | 搜索配置 | 评估架构 |
|---|---|---|
| V4 Pro | 3 个搜索工作进程 | 双视角评估、稀疏整数融合和 160 维几何输入 |
| V4 Flash | 1 个搜索线程、4 MiB 置换表 | 214,274 个参数，保留 160 维几何输入 |

详见 [Pro 架构](pro/ARCHITECTURE.md)、[Flash 架构](flash/ARCHITECTURE.md)、[Pro 发布报告](pro/RELEASE-NOTES.md)和 [Flash 发布报告](flash/RELEASE-NOTES.md)。发布报告中的训练记录和对局路径对应原研发项目；本仓库提供发行引擎的构建输入。两版均使用项目自有的推理代码和权重。原研发报告涉及第三方引擎对比和离线标注。

## 构建与验证

使用 Windows x64、支持 AVX2/FMA 的 CPU、Python 3.9+ 和 MinGW-w64 GCC。将 g++ 加入 PATH，或传入完整路径。

```powershell
python build.py
python build.py flash --cxx C:/w64devkit/bin/g++.exe
python smoke.py
```

程序生成在 `build/`。用不同的编译器版本构建时，程序哈希可能不同。正式 Windows 二进制哈希记录在 `pro/build-profile.json` 和 `flash/build-profile.json`。

## UCI 使用

从终端启动程序，或在支持中国象棋 UCI 的客户端中添加程序路径。终端会话示例：

```text
uci
isready
position startpos
go movetime 300
quit
```

`go` 返回 `bestmove`。权重已内置。

## 许可证

本仓库源码及自有模型权重采用 [MIT 许可证](LICENSE)。
