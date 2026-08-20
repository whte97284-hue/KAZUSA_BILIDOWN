# BiliCommander · 历史归档页（PageHistory）设计书 · 时光隧道

> 版本：v1.0 ｜ 日期：2026-08-20 ｜ 目标实现端：QML + C++17（Qt6）
> 设计原则：极简、文字降噪、色彩统一（中性深灰主题）、扁平、无字流派
> 核心视觉：**时光隧道** —— 横向胶片带 + 时间刻度尺，滚轮横扫、封面放大定格

---

## 1. 背景与目标

当前 `PageHistory` 是占位卡片，仅展示静态文案，无任何历史数据支撑。下载完成的任务只活在内存 `TaskModel` 里，**退出应用即丢失**。

本设计把历史页升级为「**时光隧道**」：一条按时间倒序排列的横向封面胶片带，配一条可感知"时间流逝"的刻度尺，点击封面在下方面板**放大定格**查看档案并快速定位文件。归档从"功能页"变成"有仪式感的回忆体验"。

**核心价值**：封面为王（零文字噪音）、时间可感知（刻度尺 + 光标）、动效丝滑（滚轮横扫 + 定格放大）。

---

## 2. 硬性约束（必须遵守）

1. **数据源**：历史记录完全来自本机任务完成事件，**不访问任何 B 站接口**；封面优先用本地 `cover.jpg`，本地缺失才回退远程 `coverUrl`（离线也能看）。
2. **持久化**：历史落盘 `history.json`（与应用同级目录，与 `config.json` 同侧），启动载入、完成追加、超限裁剪。
3. **主题**：全部走 `FluTheme` token（`surfaceBg/cardBorder/textSecondary/primaryColor` 等），**零硬编码色值**；深浅色模式健全。
4. **动效**：全部走 `FluTheme` 动效 token——`durationFast(120)` 状态瞬变 / `durationNormal(180)` 页面级过渡 / `durationPopup(200)` 弹窗·定格放大，统一 `easingStandard`（OutCubic）。
5. **字体**：标题 `fontTitle`、正文 `fontBody`、纯数字（统计/大小/百分比）`fontMono`。
6. **封面上限**：历史仅保留**最近 200 条**，超出丢弃最旧（防 `history.json` 无限膨胀）。
7. **删除语义**：删除/清空历史**只删记录，绝不删磁盘文件**；打开目录/播放文件才触碰真实文件。

---

## 3. 数据模型与持久化（C++ 侧）

### 3.1 新增 `HistoryEntry` 结构（新文件 `cpp_core/glue/BiliHistoryModel.hpp`）

```cpp
// 单条历史记录（值类型，供 HistoryModel 跨线程安全快照）
struct HistoryEntry {
    int64_t id = 0;            // 自增主键
    QString title;             // 视频/番剧标题
    QString coverUrl;          // 远程封面 URL（回退用）
    QString localCover;        // 本地 cover.jpg 绝对路径（优先加载）
    QString ownerName;         // UP主
    QString durationDesc;      // 时长 "24:00"
    QString qualityText;       // 画质/编码 "1080P | hvc1"
    QString sizeText;          // 展示用大小 "60 MB"
    qint64 totalBytes = 0;     // 总字节数（顶部统计求和）
    QString filePath;          // 主产物路径（output.mp4，混流后）
    QString saveDir;           // 任务存储目录
    QString bvid;              // 供未来"重新下载"扩展
    qint64 finishedAt = 0;     // 完成时间戳（ms）
};
```

### 3.2 新增 `HistoryModel`（`QAbstractListModel`）

Roles（与 QML 绑定一一对应）：

| Role | 类型 | 说明 |
|---|---|---|
| `title` | QString | 标题 |
| `coverUrl` | QString | 远程封面（回退） |
| `localCover` | QString | 本地封面路径（优先） |
| `ownerName` | QString | UP主 |
| `durationDesc` | QString | 时长 |
| `qualityText` | QString | 画质/编码 |
| `sizeText` | QString | 大小 |
| `filePath` | QString | 主产物路径 |
| `saveDir` | QString | 存储目录 |
| `finishedAt` | qint64 | 完成时间戳 |
| `finishedText` | QString | 格式化时间 "08-20 14:30"（QML 端格式化亦可，二选一） |

对外接口（仅 GUI 线程）：

```cpp
void appendEntry(const HistoryEntry &e);   // 追加 + 落盘 + 裁剪到 200 条
bool removeEntry(int row);                 // 按行号删除记录
void clearAll();                            // 清空记录
void loadFromFile(const QString &path);    // 启动载入
void saveToFile(const QString &path) const;// 落盘
int rowCount() const;
```

### 3.3 `history.json` 格式

```json
[
  {
    "title": "【4K】某视频标题",
    "coverUrl": "https://.../cover.jpg",
    "localCover": "D:/downloads/标题/cover.jpg",
    "ownerName": "UP主名",
    "durationDesc": "24:00",
    "qualityText": "1080P | hvc1",
    "sizeText": "60 MB",
    "totalBytes": 62914560,
    "filePath": "D:/downloads/标题/output.mp4",
    "saveDir": "D:/downloads/标题",
    "bvid": "BV1xx411c7mD",
    "finishedAt": 1789123456789
  }
]
```

### 3.4 `BiliController` 暴露（Q_PROPERTY / Q_INVOKABLE）

| 成员 | 类型 | 说明 |
|---|---|---|
| `historyModel` | `QAbstractItemModel*` (CONSTANT) | 历史列表数据源 |
| `historyCount` | int (`NOTIFY historyChanged`) | 条目数，顶栏统计用 |
| `historyTotalSize` | QString (`NOTIFY historyChanged`) | 总大小 "84 GB"，顶栏统计用 |
| `Q_INVOKABLE openHistoryFile(int row)` | - | 播放主产物（复用 `openFile`） |
| `Q_INVOKABLE openHistoryFolder(int row)` | - | 打开存储目录（复用 `openFolder(saveDir)`） |
| `Q_INVOKABLE removeHistory(int row)` | - | 删除记录（不动文件） |
| `Q_INVOKABLE clearHistory()` | - | 清空记录（不动文件） |

### 3.5 接入点（`BiliController.cpp`）

- **写入**：`executeTask` 中任务进入 **Completed 且成功**时，构造 `HistoryEntry` 并 `appendEntry`（封面优先 `saveDir/cover.jpg`，存在才填 `localCover`；`filePath` 取混流后的 `output.mp4`，若混流关闭则留空由 QML 决定入口）。**失败任务不写历史**。
- **载入**：构造器末尾 `loadFromFile("./history.json")`。
- **落盘**：每次 `appendEntry / removeEntry / clearAll` 后即时 `saveToFile`。

---

## 4. 页面布局（时光隧道 · 垂直三段）

间距统一 `18px`（沿用各页 `anchors.margins`），`spacing` 用 `12`。

```
┌────────────────────────────────────────────────────────────┐
│  时光隧道                    共 128 件 · 84 GB               │  ← 顶栏：左标题 + 右统计（fontTitle / fontMono）
├────────────────────────────────────────────────────────────┤
│  ───●─────────●───────────●──────────────●─────────────── │  ← 时间刻度尺（细线 + 主题色光标 + 3 锚点标签）
│    今天        最近 7 天                    更早             │
│  ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐               │  ← 横向胶片带（ListView.Horizontal，滚轮横扫）
│  │封面│ │封面│ │封面│ │封面│ │封面│ │封面│    ...           │
│  └────┘ └────┘ └────┘ └────┘ └────┘ └────┘               │
├────────────────────────────────────────────────────────────┤
│  ┌──────────┐  【4K】某视频标题                    播放      │  ← 详情卡（点击胶片定格放大）
│  │ 封面大图  │  UP主名 · 1080P | hvc1    ·   60 MB  打开目录  │
│  │ 220×124  │  08-20 14:30                                │
│  └──────────┘                                             │
└────────────────────────────────────────────────────────────┘
```

### 4.1 顶栏

- 左：标题「时光隧道」（`fontTitle` 粗体，字号 `fontSizeHeader` 级或以下）
- 右：统计 `共 N 件 · X GB`（`fontMono`，`textSecondary`）；`N` 绑 `biliController.historyCount`，大小绑 `historyTotalSize`
- 右缘可放一个清空入口（IconOnly `clear` 图标，hover 变 `danger`，点击弹确认框 `FluContentDialog`）

### 4.2 时间刻度尺（高约 28px，不参与滚动）

- 一条 1px 基线（`cardBorder`）
- **三个静态锚点标签**：`今天`（零点起）/ `最近 7 天`（7 天前零点起）/ `更早`，均匀分布或按语义间距摆放，标签用 `textSecondary` caption 字号
- **主题色光标**（6px 圆点 + 竖向细线，`primaryColor`）：`x` 位置 = `胶片带 contentX / contentWidth` 比例映射到刻度尺宽度，随滚动平滑移动（`Behavior` 用 `durationNormal`）。光标所在区间的高亮标签加粗变 `primaryColor`
- 纯视觉导航，不拦截点击

### 4.3 横向胶片带（核心，占剩余高度约 40%）

- `ListView` `orientation: ListView.Horizontal`，`clip: true`，`spacing: 12`，数据源 `biliController.historyModel`
- 封面卡片：16:9，宽约 `200px` 高 `112px`，`radius` 用 `FluTheme.radius`（6 可接受，统一走 token），`clip: true`，`color: surfaceActive` 兜底
- 封面加载：`Image.source = localCover 存在 ? localCover : coverUrl`，`PreserveAspectCrop` + `smooth`
- **悬停**：卡片 `scale` 1 → `scaleHover(1.02)`、阴影加深（120ms），封面右上角浮现 `打开目录` 角标（半透明底胶囊，hover 才见）
- **选中**：胶片卡 `border` 变 `primaryColor` 1.5px + `scale 1.04`（`durationPopup`，OutCubic），同步更新下方详情卡
- **滚轮横扫**（关键实现）：
  - `ListView` 本身横向不响应垂直滚轮，需挂 `WheelHandler { orientation: Qt.Vertical }`（或 `MouseArea.onWheel`），将 `wheel.angleDelta.y` 累加到 `list.contentX`
  - 配 `flickableDirection: Flickable.HorizontalOnly` + `boundsBehavior: Flickable.StopAtBounds`（含回弹），实现"滚轮横扫 + 惯性拖拽"双通道
  - 滚动时时间光标联动（见 §4.2）

### 4.4 详情卡（底部固定面板，占剩余高度约 40%）

- 根：`Rectangle` `surfaceBg` + 1px `cardBorder`，`radius` token，padding 14
- 左：封面大图 16:9 宽 `220px` 高 `124px`（`localCover` 优先），与胶片卡同源
- 右（`ColumnLayout`）：
  - 标题：`fontTitle` 粗体单行 elide
  - 元信息行：`UP主 · 画质/编码 · 大小`（`textSecondary` caption）
  - 时间行：`MM-dd HH:mm`（`textSecondary` caption，`fontMono` 可）
  - 操作：右对齐两个 `FluPillButton` —— `播放`（`isFilled: true`）/ `打开目录`（ghost），均走 `openHistoryFile/Folder(row)`
- **无选中态**：显示极简空态文案「点击上方封面，定格这段记忆」；**历史为空**：整个页面显示空态「时光隧道还是空的 · 去下载一部视频吧」（复用页面居中空态样式）

---

## 5. 关键交互流

```
启动 → 载入 history.json → 胶片带按 finishedAt 倒序展示
滚动（滚轮/拖拽）→ 封面横扫 + 时间光标联动 + 锚点高亮切换
点击封面 → 定格放大（选中态）+ 详情卡更新（fade+slide）
打开目录 / 播放 → 复用现有 openFolder / openFile
删除 / 清空 → 确认框 → 仅删 history.json 记录，磁盘文件不动
```

---

## 6. 动效衔接（呼应动效体系）

| 场景 | 动效 |
|---|---|
| 页面进入 | fade + 轻微 slide ≤ `durationNormal` |
| 封面 hover | `scale` + 阴影 ≤ `durationFast`(120) |
| 封面选中定格 | `scale` + `border` 过渡 ≤ `durationPopup`(200) OutCubic |
| 时间光标联动 | `x` 平滑跟随 ≤ `durationNormal`(180) |
| 详情卡内容切换 | fade + 上移 ≤ `durationNormal`(180) |
| 删除/清空确认框 | `FluContentDialog` 默认（200ms OutCubic） |

---

## 7. 文件与命名

- 新增：`cpp_core/glue/BiliHistoryModel.hpp`（`HistoryEntry` + `HistoryModel`）
- 新增：`qml/Pages/PageHistory.qml`（重写占位页）
- 修改：`cpp_core/glue/BiliController.hpp/.cpp`（historyModel 暴露 + 完成事件写入 + 载入/落盘）
- 修改：`main.qml` 无需改导航（PageHistory 已在 index 2）
- 持久化文件：`history.json`（应用根目录，与 `config.json` 同级）
- 主题：全部 `FluTheme` token，不新增硬编码色值

---

## 8. 验收标准

1. 下载完成（成功）→ 历史页出现该条记录；**失败任务不出现**
2. 重启应用 → 历史仍在（`history.json` 载入）；连续新增至 201 条 → 自动裁剪保留最近 200 条
3. 胶片带可**滚轮横扫**（含惯性），时间刻度尺光标随滚动平滑联动，锚点高亮正确切换
4. 点击封面 → 定格放大 + 详情卡正确展示标题/UP主/画质/大小/时间
5. `打开目录` / `播放` 正确打开对应文件；`删除`/`清空` 弹确认框，确认后记录消失**但磁盘文件仍在**
6. 封面：本地 `cover.jpg` 存在则加载本地（断网也能看）；不存在回退远程 `coverUrl`
7. 深浅色模式表现正常，无硬编码色值；qmllint 无语法错误；`build_qml.bat` 全量构建通过
8. 空历史态、无选中态文案正确，页面无死角落（间距/对齐/截断均检查）
