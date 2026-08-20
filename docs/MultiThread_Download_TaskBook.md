# 多线程分片下载引擎 · 开发要求书（Task Book）

> 交付给独立执行方（Gemini）的完整要求书，目标：把 BiliCommander 的单连接下载升级为**多线程分片下载**，对标 BBDown `-mt` / aria2 `-x16 -s16 -k 5M` 的并发提速，同时**不破坏现有防爬校验、断点续传、取消、CDN 容灾**四大防线。

---

## 0. 一句话目标

在 **BiliCommander_Qt**（C++17 + Qt6/QML，Windows/MSVC）中，新增**多线程分片下载引擎**：把单个媒体流（video.m4s / audio.m4s）切成 N 段，用 N 个并发的 HTTP Range 请求同时拉取，显著提升大文件下载速度（单连接 3~8MB/s → 多连接 20MB/s+）。

---

## 1. 项目背景与现状

### 1.1 现有下载链路（已跑通，稳定）

```
解析 → 三态任务队列 → runDownload → downloadFullTo
  ├─ 封面 cover.jpg（尽力而为）
  ├─ 弹幕 danmaku.xml（尽力而为）
  ├─ 视频轨 downloadWithFallback(urls[], video.m4s)
  └─ 音频轨 downloadWithFallback(urls[], audio.m4s)
      └─ 混流 Bento4Muxer → 单文件 MP4
```

### 1.2 关键现状代码（必须复用，不可重写）

| 位置 | 说明 |
|---|---|
| [BiliDownloader.cpp L150-L448](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/BiliDownloader.cpp#L150-L448) | `downloadStream()`：单连接流式下载，含 Range 续传、手动重定向循环、伪 200 拦截、魔数校验、进度回调 |
| [BiliDownloader.cpp L29-L45](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/BiliDownloader.cpp#L29-L45) | `isValidMediaChunk()`：首个 Chunk 魔数校验（防反爬拦截体落盘） |
| [BiliController.cpp L977-L1017](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/glue/BiliController.cpp#L977-L1017) | `downloadWithFallback()`：主 CDN + 备份 CDN 逐条尝试，单 CDN 内 3 次重试 + 指数退避，`.part` 断点续传 |
| [BiliController.cpp L818-L925](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/glue/BiliController.cpp#L818-L925) | `downloadFullTo()`：视频/音频轨各调一次 `downloadWithFallback`，混流挂载点 |
| [BiliDownloader.hpp L42-L50](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/BiliDownloader.hpp#L42-L50) | `downloadStream` 签名：`(url, destPath, progressCb, maxBytes, errorMsg, resumeOffset, cancel)` |
| [BiliAuth.hpp L37-L49](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/BiliAuth.hpp#L37-L49) | `AppConfig` 结构（config.json 持久化） |
| [BiliController.cpp L606-L625](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/glue/BiliController.cpp#L606-L625) | `queueLoop()`：FIFO 单 worker 顺序消费 |

### 1.3 现状缺口

- **单连接下载**：B 站 CDN 单连接吞吐有限，大文件（4K/8K 番剧）下载慢。
- **无并发加速**：对比 BBDown（`-mt` 默认开）与 aria2（`-x16 -s16`），速度差距明显。
- **无全局限速**：后台下载会打满带宽（限速由另一执行方负责，见 §7 分工）。

---

## 2. 技术方案（多线程分片）

### 2.1 核心模型

```
                ┌───────────────────────────┐
                │  MultiThreadDownloader     │
                │  1. HEAD/探测获取 totalSize│
                │  2. 切分 [0..N) 段         │
                │  3. 每段一个线程            │
                └────────────┬──────────────┘
          ┌──────────────────┼──────────────────┐
          │ Range: bytes=0-   │ Range: bytes=... │ ...
      Thread0             Thread1            Thread2
     (写 .part.0)        (写 .part.1)       (写 .part.2)
          └──────────────────┴──────────────────┘
                      完成后按 offset 合并 → 单文件
```

### 2.2 分片策略

- **获取总大小**：对主 CDN 发 `GET` + `Range: bytes=0-0`，从响应头 `Content-Range: bytes 0-0/<total>` 或 `Content-Length` 拿到 `totalSize`（复用现有 WinHTTP 请求基建）。
- **分片数**：`threads = min(maxThreads, 文件大小 / 最小分片)`，默认 `maxThreads = 4`，最小分片 5MB（防止小文件被切碎）。
- **分片边界**：按字节均分 `[start, end)`，最后一个分片到文件末尾。
- **文件过小**（< 5MB）：退化为**单线程**，直接走现有 `downloadStream` 路径，避免线程开销。

### 2.3 写入策略（二选一，推荐 B）

- **方案 A（推荐）**：每个分片线程写独立临时文件 `.part.0 / .part.1 / ...`，全部成功后按 offset 顺序拼接/流式合并成最终 `.part`，再原子改名收尾。好处：天然支持分片级断点续传（某段已下载完则跳过该段）。
- **方案 B**：所有线程用 `SetFilePointerEx` 直接写同一个文件的不同 offset（预分配大小）。好处：省去合并；坏处：并发写同一文件句柄需同步，且分片级续传复杂。**除非实现极顺，否则选 A。**

### 2.4 断点续传（对齐现有 `.part` 机制）

- 任务取消/中断后，**已完成的完整分片保留**（`.part.N` 存在且大小 == 该分片大小）。
- 重新开始时，逐个分片检查：已完成则跳过，未完成则从该分片 offset 续传。
- 全部完成后合并 → `xxx.part` → 原子改名 `xxx`（沿用现有 `.part` → 最终文件的语义，保证上层混流/魔数校验不感知改动）。

### 2.5 容灾（对齐现有 downloadWithFallback）

- 每个分片失败：同 CDN 重试 3 次（指数退避 300ms → 1s，复用现有逻辑）。
- 主 CDN 整体不可用时：整个多线程下载切换到**备份 CDN** 重跑。
- 某个分片持续失败：整任务失败，`errorMsg` 说明失败分片，`.part` 保留供下次续传。

### 2.6 进度合并

- 每线程按实际下载字节更新一个原子累计值，合并计算全局 `percent`。
- 上层 `ProgressCallback` 契约**保持不变**：`(downloadedBytes, totalBytes, speedBps, percent)`。
- 回调节流 100ms（复用现有逻辑），避免刷屏。

### 2.7 取消

- 复用现有 `std::atomic<bool> *cancel`：置位后各线程在每次 `WinHttpReadData` 前检查并退出，已写分片保留。

---

## 3. 硬性约束（必须遵守）

1. **C++17 + Qt6/MSVC**，禁止引入 FFmpeg / aria2 / 任何外部可执行文件。
2. **严格复用现有防线**：
   - 每个分片的首次响应必须做**伪 200 拦截 + 魔数校验**（复用 `isValidMediaChunk`，分片头同样可能是拦截体）。
   - 手动重定向循环（`WINHTTP_OPTION_REDIRECT_POLICY_NEVER` + 逐跳重发 Referer/Cookie）必须保留，**禁止打开 WinHTTP 自动跳转**。
   - `Accept-Encoding: identity`、`Referer: https://www.bilibili.com/`（带尾斜杠）、SESSDATA Cookie 头必须逐分片携带。
3. **中文/UTF-8 路径**：复用 `u8p()`（`MultiByteToWideChar(CP_UTF8)`），禁止 `std::filesystem::u8path`。
4. 代码注释用**中文**。
5. **配置化**：线程数走 `AppConfig.maxDownloadThreads`（int，默认 4，范围 1~16，`1`=关闭多线程走旧路径）；`loadConfig/saveConfig` 双向序列化。
6. **性能底线**：多线程不得拖慢小文件（< 5MB 直接单线程）；分片线程数不超过配置值。
7. **线程安全**：所有跨线程状态用 `std::atomic` / 互斥锁；进度合并用原子累加，禁止数据竞争。
8. **不破坏主生产链路**：改造以"新增引擎 + 可选切换"方式进行，默认开启多线程但随时可关；现有单线程路径保留作为兜底。
9. **限速**：限速由另一执行方（当前助手）负责，见 §7 分工。多线程引擎需预留全局速率控制钩子（见 §4.4），**不要自己实现限速**，避免与限速实现冲突。

---

## 4. 任务分解

### Phase 0（必须先做）：可行性最小验证 —— 硬门

1. 用**真实 B 站下载地址**（任意视频的 video.m4s 直链）手工验证：发 `Range: bytes=0-0` 是否能拿到 `Content-Range: bytes 0-0/<total>` 且状态 206？
2. 验证多个 Range 并发（3 线程各取一段）返回的数据能拼接回完整文件，且与单线程下载的 MD5 一致。
3. 输出结论：B 站 CDN 是否支持多段 Range 并发？有无连接数限制？是否需要固定每线程 Range 大小？

> ⚠️ **Phase 0 是硬门：不通过就停下汇报具体失败原因，禁止往后写代码。**

### Phase 1：分片下载核心引擎

- 文件：新增 `cpp_core/MultiThreadDownloader.hpp` / `MultiThreadDownloader.cpp`（或并入 BiliDownloader，二选一，命名自定但需注释清晰）。
- 接口建议：

```cpp
// 多线程分片下载单个媒体流 (等价 downloadStream 的多线程版)
bool downloadStreamParallel(
    const std::string &streamUrl,
    const std::vector<std::string> &backupUrls,  // 备用 CDN (主 CDN 失败时整体切换)
    const std::string &destFilePath,             // 最终文件路径 (内部走 .part + 分片)
    int threadCount,                             // 线程数 (<=0 或 1 → 走单线程)
    ProgressCallback progressCb,
    std::string *errorMsg,
    std::atomic<bool> *cancel
);
```

- 复用 `BiliHttpClient` / 现有 WinHTTP 基建（系统代理探测、超时设置、Header 构造）。
- 分片完成 → 合并 → `.part` → 原子改名。合并用流式拷贝（`ifstream/ofstream` + `u8p`），禁止一次性读入内存。

### Phase 2：BiliController 接入

- **挂载点**：`downloadWithFallback`（[BiliController.cpp L977](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/glue/BiliController.cpp#L977)）内部，在现有单线程 `downloadStream` 调用处增加分支：
  - `m_config.maxDownloadThreads > 1` 且文件足够大 → 调用 `downloadStreamParallel`；
  - 否则 → 走原有 `downloadStream`（保留兜底）。
- 保持 `downloadFullTo` / `downloadAudioTo` / 进度权重逻辑不变（进度回调契约不变）。

### Phase 3：配置项

1. `AppConfig`（[BiliAuth.hpp L37-L49](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/BiliAuth.hpp#L37-L49)）新增：

```cpp
int maxDownloadThreads = 4;   // 多线程分片并发数 (1=关闭, 默认4)
```

2. `BiliAuth::loadConfig / saveConfig` 同步序列化新字段。
3. 设置页限速分组由另一执行方负责（§7）；本任务只需保证 `maxDownloadThreads` 在 config.json 可读写。

### Phase 4：全局速率控制钩子（仅预留接口）

- 在 `MultiThreadDownloader` 内预留一个**可选**的全局节流器（`RateLimiter` 引用或函数指针），**实现由限速执行方负责**。钩子形式建议：

```cpp
// 预留: 限速执行方注入的全局节流器 (可为空指针 = 不限速)
class RateLimiter; // 由限速执行方定义 (BiliController 层持有)
void setRateLimiter(RateLimiter *limiter);
```

- 引擎内每个分片在写盘前调用 `limiter->acquire(bytes)`（若非空）。**本任务不实现 RateLimiter 本身。**

### Phase 5：测试与验证

- 扩展 `BiliChainTest`（test_chain.cpp）或 `BiliProbe` 增加多线程下载用例。
- 校验清单：
  1. 同一直链，多线程（4 线程）与单线程下载产物 **MD5 一致**（无损）。
  2. 大文件（≥200MB）多线程耗时显著低于单线程（速度提升 ≥1.5x 为通过）。
  3. 小文件（<5MB）自动走单线程，行为与旧版完全一致。
  4. 断点续传：下载到 60% 取消 → 重启 → 已完成的完整分片跳过，未完成续传，最终产物完整。
  5. 取消：下载中置位 cancel → 各线程立即退出，`.part` 保留。
  6. 容灾：主 CDN 全断 → 整体切备份 CDN 成功；单分片失败 → 同 CDN 重试后成功。
  7. 伪 200 拦截：分片首次响应为 HTML 拦截体 → 判定失败并切换 CDN。
  8. 中文路径：含中文/特殊字符的目录下载正常。
  9. `maxDownloadThreads=1` 时行为与旧版一致（回归）。

---

## 5. 验收标准（Definition of Done）

- [ ] Phase 0 验证通过（Range 并发可行，产物 MD5 一致）。
- [ ] `downloadStreamParallel` 实现完成，接入 `downloadWithFallback`，默认 4 线程可用。
- [ ] 大文件多线程提速 ≥1.5x；小文件自动单线程，行为不变。
- [ ] 断点续传、取消、CDN 容灾、伪 200 拦截全部保留且通过测试。
- [ ] 多线程产物与单线程产物 MD5 一致（无损）。
- [ ] `AppConfig.maxDownloadThreads` 持久化正常（config.json）。
- [ ] 中文路径正常；qmllint 无新语法错误（若改到 QML）；`build_qml.bat` 全量构建通过。
- [ ] 限速钩子已预留（不实现限速逻辑本身）。

---

## 6. 交付物清单

1. 修改 / 新增文件列表（MultiThreadDownloader、BiliController 接入、BiliAuth 配置）。
2. 测试报告：多线程 vs 单线程速度对比表、MD5 校验结果、断点/取消/容灾用例日志。
3. 遗留问题与风险说明。

---

## 7. 分工边界（重要）

- **本任务（Gemini 负责）**：多线程分片下载引擎 + 接入 + 配置 + 测试。**不要实现限速。**
- **另一执行方（当前会话助手负责）**：
  - **限速设置**：全局 `RateLimiter` 实现（令牌桶/节流）、`AppConfig.maxDownloadSpeedKB` 配置项、设置页限速 UI。
  - **字幕落盘**：把已解析的 `detail.subtitles`（[BiliParser.cpp L225-237](file:///c:/Users/33011/Downloads/Python_Project/博客/工具/BiliCommander_Qt/cpp_core/BiliParser.cpp#L225-L237) 已解析未落盘）下载为 `.srt` 并混入/伴随输出。
- 两执行方在 **`RateLimiter` 钩子** 处对接：多线程引擎只调用 `limiter->acquire(bytes)`，限速实现完全由限速执行方注入。

---

## 8. 给执行者的提醒

- **先做 Phase 0，别跳步**——Range 并发可行性是整个方案成立的前提。
- 遇到阻塞：停下，给出具体失败信息与日志，不要闷头绕路。
- 全程零外部工具依赖；防线（魔数/重定向/Header）一条都不能丢。
- 参考：aria2 分片模型（`-s N` 每文件 N 段、`-k 5M` 段大小）、BBDown `-mt` 多线程。
