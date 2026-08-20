pragma Singleton
import QtQuick

QtObject {
    id: theme

    // ================= 1. 模式与唯一主强调色 =================
    property int darkMode: 1
    property color primaryColor: "#FF1E42" // 唯一主强调色 (默认激光红，全界面响应)
    property alias accent: theme.primaryColor

    // ================= 2. 严格语义色 (红/黄/绿，仅用于状态表达) =================
    property color success: "#00C853"      // 成功 / 已完成
    property color warning: "#FF9100"      // 警告 / 进行中 / 速度
    property color danger: "#E53935"       // 错误 / 取消 / 危险

    // 兼容别名 (统统收敛至 primaryColor / 语义色)
    property alias evaRed: theme.primaryColor
    property alias evaRedGlow: theme.primaryColor
    property alias evaOrange: theme.warning
    property alias evaGreen: theme.success
    property alias evaGold: theme.warning
    property alias accentColor: theme.primaryColor
    property alias biliPink: theme.primaryColor
    property alias biliBlue: theme.primaryColor
    property alias biliGreen: theme.success
    property alias biliGold: theme.warning

    // ================= 3. 表面色与边框 (100% 自适应明暗模式) =================
    // 明暗两套均为 RGB 等值中性灰 (无蓝调不发脏), 亮度均匀递增:
    //   暗: 15→21→27→36→46    亮: F7(底)→FF(面)→F2(悬停)→E8(激活)→E4(边框)
    property color windowBg: darkMode === 1 ? "#0F0F0F" : "#F7F7F7"
    property color navBg: darkMode === 1 ? "#151515" : "#FFFFFF"
    property color surfaceBg: darkMode === 1 ? "#1B1B1B" : "#FFFFFF"
    property color surfaceHover: darkMode === 1 ? "#242424" : "#F2F2F2"
    property color surfaceActive: darkMode === 1 ? "#2E2E2E" : "#E8E8E8"
    property color cardBorder: darkMode === 1 ? "#292929" : "#E4E4E4"

    // ================= 4. 文本层级色 =================
    property color textPrimary: darkMode === 1 ? "#FFFFFF" : "#1A1A1A"
    property color textSecondary: darkMode === 1 ? "#A1A1A1" : "#5A5A5A"
    property color textDisabled: darkMode === 1 ? "#4D4D4D" : "#9E9E9E"

    // ================= 5. 字体 Token (双主字体 + 单代码字体) =================
    // fontBody 已切换为系统已安装的思源黑体 (Noto Sans SC)；
    // fontTitle 待得意黑 (Smiley Sans) 安装后替换，当前以思源黑体加粗兜底。
    property string fontTitle: "Noto Sans SC"    // 标题 / 强调数字
    property string fontBody: "Noto Sans SC"     // 正文 / 按钮 / 标签
    property string fontMono: "Cascadia Mono"    // 仅用于 BV 号、百分比纯数字

    // ================= 6. 统一设计尺寸与动效 Token =================
    // 动效三层级: durationFast(120) 状态瞬变 / durationNormal(180) 页面级过渡
    // durationPopup(200) 弹窗·通知·模态; 全站统一 OutCubic 缓动, 自然顺滑无过冲
    property int radius: 8
    property int durationFast: 120
    property int durationNormal: 180
    property int durationPopup: 200
    property int easingStandard: Easing.OutCubic
    property real scaleHover: 1.02
    property real scalePressed: 0.96

    property int fontSizeCaption: 12 // 次级信息 / 辅助文字 / 胶囊
    property int fontSizeBody: 13    // 正文 / 按钮 / 输入框
    property int fontSizeTitle: 15   // 卡片标题 / 分组标题
    property int fontSizeHeader: 22  // 页面主标题
}
