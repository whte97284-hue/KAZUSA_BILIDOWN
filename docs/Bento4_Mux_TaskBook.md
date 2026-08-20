# Bento4 混流引擎 · 开发要求书（Task Book）

> 交付给独立执行方（Gemini）的完整要求书，目标：用 Bento4 内嵌静态库实现 m4s 无损混流，彻底摆脱 FFmpeg/MP4Box 外部工具依赖。

---

## 0. 一句话目标

在 **BiliCommander_Qt**（C++17 + Qt6/QML，Windows/MSVC）中，引入 **Bento4**（Apache 2.0，~300KB 静态库）作为进程内混流引擎，把下载产出的 `video.m4s + audio.m4s` 无损合并为单文件 `mp4`。

---

## 1. 项目背景与现状

- 项目下载链路已跑通：解析 → 三态任务队列 → 实际下载 → 完整性校验。
- 单任务 Full 模式产物固定落盘在输出目录：
  - `video.m4s`（自包含标准 MP4，含 `ftyp + moov + mdat`，改名即播）
  - `audio.m4s`（同上）
  - `cover.jpg` / `danmaku.xml`
- **现状缺口**：缺混流环节，用户拿到的是音画分离的两个文件，体验不完整。
- **目标产物**：单文件 `output.mp4`，**无损 remux（容器级合并，不转码）**。

---

## 2. 为什么选 Bento4

| 对比项 | FFmpeg | MP4Box | **Bento4** |
|---|---|---|---|
| 分发体积 | 50MB+ | 几十 MB | **~300KB 静态库** |
| 许可 | LGPL/GPL | LGPL | **Apache 2.0** |
| 集成方式 | 外部 exe | 外部 exe | **C++ 静态库进程内直链** |
| 与 m4s 场景契合 | 通用但大材小用 | 专为 MP4 | **自带 mp4merge，专做"多 MP4 合并多轨"** |

- Bento4 自带 `mp4merge` 工具：把多个 MP4 合并为一个多轨 MP4，正对 B 站 m4s 场景。
- 提供完整 C++ SDK（`AP4_File` / `AP4_Movie` / `AP4_Processor`），可编程集成，实现"体内自带混流引擎"的差异化。
- 参考仓库：<https://github.com/axiomatic-systems/Bento4>（`Bento4/Source/CMakeLists.txt`，`mp4merge` 位于 `Source/Applications/mp4merge`）。

---

## 3. 硬性约束（必须遵守）

1. **C++17 + Qt6/MSVC**，与项目其余代码同标准；禁止引入 FFmpeg。
2. **中文/UTF-8 路径**：复用项目现有 `u8p()` 辅助（`MultiByteToWideChar(CP_UTF8)` 手工转码），**禁止 `std::filesystem::u8path`**（MSVC 会抛 `ERROR_NO_UNICODE_TRANSLATION`）。参考实现见 [BiliController.cpp L21-L29](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/glue/BiliController.cpp#L21-L29)。
3. 文件/缓存**不写 C 盘**，走 `config.downloadPath` 或用户工具目录。
4. 代码注释用**中文**。
5. 保留 Bento4 许可证声明（`Apache 2.0`，随 `third_party` 目录保留 LICENSE）。
6. **混流失败必须降级**：保留 m4s 原文件，任务仍进 Completed（状态文案提示"未混流"），**不得把整个下载判为失败**。
7. 对齐项目既有媒体校验规范：输入文件需校验非空 + 前 16 字节含 MP4 魔数（`ftyp/moov/moof/mdat`）后再混流。
8. 混流在 **worker 线程**执行（`executeTask` 内，与下载同线程，不阻塞 GUI），全程不碰 GUI 线程。

---

## 4. 任务分解

### Phase 0（必须先做）：可行性最小验证 —— 硬门

1. 编译 Bento4 的 `mp4merge` 命令行工具。
2. 取一份**真实 B 站下载的** `video.m4s + audio.m4s`，执行合并。
3. 验收：产物能被 VLC / PotPlayer / Windows 媒体播放器正常播放、**音画同步、时长与原视频一致**。
4. 输出结论：`mp4merge` 是否直接可用？B 站 m4s 是否标准（non-fragmented）？是否需要 `mp4fragment` 预处理？

> ⚠️ **Phase 0 是硬门：不通过就停下来汇报具体失败原因，禁止往后继续写代码。**

### Phase 1：源码引入与构建集成

1. Bento4 源码放入 `cpp_core/third_party/Bento4`（git submodule 或直接 vendor 均可，保留 LICENSE）。
2. 目标：产出**静态库（.lib）**，只编 Core 与必要工具。
3. 修改 [cpp_core/CMakeLists.txt](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/CMakeLists.txt)：`BiliCommander` 与 `BiliChainTest` 两个目标均链接 Bento4 静态库。
4. 若裁剪静态库困难，允许直接把 `mp4merge` 相关源码文件合入项目编译（保持 Apache 2.0 声明）。

### Phase 2：Bento4Muxer 类

- 文件：`cpp_core/Bento4Muxer.hpp` / `Bento4Muxer.cpp`
- 建议接口：

```cpp
// 无损合并音视频轨为单文件 mp4
bool muxToMp4(const std::string &videoM4s,
              const std::string &audioM4s,
              const std::string &outMp4,
              std::string &error);
```

- 合并逻辑参考 `mp4merge`：`AP4_File` 读取两个文件 → 合并轨道 → 写出新文件。
- 复用 `u8p()` 同款转码处理中文路径；输入文件先做魔数/非空校验。

### Phase 3：BiliController 接入

- **挂载点**：`downloadFullTo`（[BiliController.cpp L760 附近](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/glue/BiliController.cpp#L760-L825)）视频轨 + 音频轨均落盘成功后，调用 `muxToMp4`。
- **状态流转**：下载完成 → 混流中（`statusText = "正在合成 MP4..."`）→ 成功 / 失败降级。
- **失败降级**：m4s 保留；`statusText = "混流失败: <原因>（已保留音视频原文件）"`；任务仍进 `Completed`。
- **输出命名**：`output.mp4`，或复用 `sanitizeFileName` 规则生成 `{标题}.mp4`。
- **源文件保留策略**：成功后是否删除 m4s 由配置控制（默认保留，设置页可配）。

### Phase 4：配置项

1. `AppConfig`（[BiliAuth.hpp L37-L45](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/BiliAuth.hpp#L37-L45)）新增：

```cpp
std::string muxEngine = "bento4";  // 预留: "none" / "bento4" / "mp4box" / "ffmpeg"
bool muxKeepSources = true;        // 混流成功后是否保留 m4s 源文件
```

2. `BiliAuth::saveConfig / loadConfig` 同步序列化新字段（config.json）。
3. 设置页 [PageSettings.qml](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/qml/Pages/PageSettings.qml) 加「混流」分组：
   - 混流开关（启用/禁用）
   - 引擎只读显示「Bento4（内嵌）」
   - 「保留源文件」开关

### Phase 5：测试与验证

- 扩展 `BiliChainTest`（test_chain.cpp）跑完整链路：下载 → 自动混流 → 校验。
- 校验清单：
  1. `output.mp4` 存在且可播放、音画同步、时长与原视频一致（±1s）。
  2. 体积 ≈ `video.m4s + audio.m4s` 之和（证明无损）。
  3. 中文目录 / 中文文件名路径工作正常。
  4. 失败降级场景：人为删掉 `audio.m4s` 再触发，应降级成功（m4s 保留、任务 Completed）。
  5. 覆盖 **AV1 / HEVC / AVC** 三种视频编码 + AAC 音频。

---

## 5. 验收标准（Definition of Done）

- [ ] Phase 0 验证通过（真实 m4s 合成可播、音画同步）。
- [ ] Bento4 静态库成功编入 `BiliCommander` 与 `BiliChainTest`。
- [ ] `Bento4Muxer` 可独立调用完成混流。
- [ ] 完整下载链路自动混流，产物可播、音画同步、时长一致。
- [ ] 失败降级路径正确（任务仍 Completed，m4s 保留）。
- [ ] 设置页配置可用且持久化到 config.json。
- [ ] 中文路径正常。
- [ ] 全程无 FFmpeg / 外部工具依赖。

---

## 6. 交付物清单

1. 修改 / 新增文件列表（含 Bento4Muxer、CMake、AppConfig、PageSettings 改动）。
2. 测试报告：真实文件验证的日志输出 / 截图 / 校验结果。
3. 遗留问题与风险说明。

---

## 7. 给执行者的提醒

- **先做 Phase 0，别跳步**——它是整个方案成立的前提。
- 遇到阻塞：停下，给出具体失败信息与日志，不要闷头绕路。
- 保留 Bento4 许可证声明；不引入任何外部 exe / FFmpeg。
- 参考：<https://github.com/axiomatic-systems/Bento4>（`Bento4/Source/CMakeLists.txt`、`Source/Applications/mp4merge`）。
