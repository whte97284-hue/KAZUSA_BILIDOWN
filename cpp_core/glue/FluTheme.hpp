#pragma once

#include <QObject>
#include <QColor>
#include <QString>

// 统一极简设计主题单例 (C++ 侧全局上下文属性)
class FluTheme : public QObject {
    Q_OBJECT

    // 模式与唯一主强调色
    Q_PROPERTY(int darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(QColor primaryColor READ primaryColor WRITE setPrimaryColor NOTIFY primaryColorChanged)
    Q_PROPERTY(QColor accent READ primaryColor WRITE setPrimaryColor NOTIFY primaryColorChanged)

    // 严格语义色
    Q_PROPERTY(QColor success READ success CONSTANT)
    Q_PROPERTY(QColor warning READ warning CONSTANT)
    Q_PROPERTY(QColor danger READ danger CONSTANT)

    // 兼容别名
    Q_PROPERTY(QColor evaRed READ primaryColor CONSTANT)
    Q_PROPERTY(QColor evaRedGlow READ primaryColor CONSTANT)
    Q_PROPERTY(QColor evaOrange READ warning CONSTANT)
    Q_PROPERTY(QColor evaGold READ warning CONSTANT)
    Q_PROPERTY(QColor evaGreen READ success CONSTANT)
    Q_PROPERTY(QColor evaPurple READ primaryColor CONSTANT)
    Q_PROPERTY(QColor accentColor READ primaryColor CONSTANT)
    Q_PROPERTY(QColor biliPink READ primaryColor CONSTANT)
    Q_PROPERTY(QColor biliBlue READ primaryColor CONSTANT)
    Q_PROPERTY(QColor biliGreen READ success CONSTANT)
    Q_PROPERTY(QColor biliGold READ warning CONSTANT)

    // 表面色 (随 darkMode 联动)
    Q_PROPERTY(QColor windowBg READ windowBg NOTIFY darkModeChanged)
    Q_PROPERTY(QColor navBg READ navBg NOTIFY darkModeChanged)
    Q_PROPERTY(QColor surfaceBg READ surfaceBg NOTIFY darkModeChanged)
    Q_PROPERTY(QColor surfaceHover READ surfaceHover NOTIFY darkModeChanged)
    Q_PROPERTY(QColor surfaceActive READ surfaceActive NOTIFY darkModeChanged)
    Q_PROPERTY(QColor cardBorder READ cardBorder NOTIFY darkModeChanged)

    // 文本色 (随 darkMode 联动)
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY darkModeChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY darkModeChanged)
    Q_PROPERTY(QColor textDisabled READ textDisabled NOTIFY darkModeChanged)

    // 字体 Token
    Q_PROPERTY(QString fontTitle READ fontTitle CONSTANT)
    Q_PROPERTY(QString fontBody READ fontBody CONSTANT)
    Q_PROPERTY(QString fontMono READ fontMono CONSTANT)

    // 统一设计尺寸 Token
    Q_PROPERTY(int radius READ radius CONSTANT)
    Q_PROPERTY(int durationFast READ durationFast CONSTANT)
    Q_PROPERTY(int durationNormal READ durationNormal CONSTANT)
    Q_PROPERTY(int durationPopup READ durationPopup CONSTANT)
    Q_PROPERTY(int easingStandard READ easingStandard CONSTANT)
    Q_PROPERTY(double scaleHover READ scaleHover CONSTANT)
    Q_PROPERTY(double scalePressed READ scalePressed CONSTANT)
    Q_PROPERTY(int fontSizeCaption READ fontSizeCaption CONSTANT)
    Q_PROPERTY(int fontSizeBody READ fontSizeBody CONSTANT)
    Q_PROPERTY(int fontSizeTitle READ fontSizeTitle CONSTANT)
    Q_PROPERTY(int fontSizeHeader READ fontSizeHeader CONSTANT)

public:
    explicit FluTheme(QObject *parent = nullptr) : QObject(parent) {}

    int darkMode() const { return m_darkMode; }
    void setDarkMode(int v) {
        if (m_darkMode != v) { m_darkMode = v; emit darkModeChanged(); }
    }

    QColor primaryColor() const { return m_primaryColor; }
    void setPrimaryColor(const QColor &c) {
        if (m_primaryColor != c) { m_primaryColor = c; emit primaryColorChanged(); }
    }

    QColor success() const { return QColor("#00C853"); }
    QColor warning() const { return QColor("#FF9100"); }
    QColor danger() const { return QColor("#E53935"); }

    QColor windowBg() const { return m_darkMode == 1 ? QColor("#121212") : QColor("#F7F8FA"); }
    QColor navBg() const { return m_darkMode == 1 ? QColor("#171717") : QColor("#FFFFFF"); }
    QColor surfaceBg() const { return m_darkMode == 1 ? QColor("#1E1E1E") : QColor("#FFFFFF"); }
    QColor surfaceHover() const { return m_darkMode == 1 ? QColor("#282828") : QColor("#F3F4F6"); }
    QColor surfaceActive() const { return m_darkMode == 1 ? QColor("#333333") : QColor("#EAEBED"); }
    // 亮色模式使用极轻柔中性微暖灰，暗色模式使用绝对中性石墨描边 (0% 蓝调杂色)
    QColor cardBorder() const { return m_darkMode == 1 ? QColor("#2A2A2A") : QColor("#E6E7EB"); }

    QColor textPrimary() const { return m_darkMode == 1 ? QColor("#F5F5F5") : QColor("#1F2328"); }
    QColor textSecondary() const { return m_darkMode == 1 ? QColor("#A3A3A3") : QColor("#656D76"); }
    QColor textDisabled() const { return m_darkMode == 1 ? QColor("#525252") : QColor("#8C959F"); }

    QString fontTitle() const { return QStringLiteral("Noto Sans SC"); }
    QString fontBody() const { return QStringLiteral("Noto Sans SC"); }
    QString fontMono() const { return QStringLiteral("Cascadia Mono"); }

    int radius() const { return 8; }
    int durationFast() const { return 120; }
    int durationNormal() const { return 180; }
    int durationPopup() const { return 200; }
    int easingStandard() const { return 6; /* QEasingCurve::OutCubic */ }
    double scaleHover() const { return 1.02; }
    double scalePressed() const { return 0.96; }
    int fontSizeCaption() const { return 12; }
    int fontSizeBody() const { return 13; }
    int fontSizeTitle() const { return 15; }
    int fontSizeHeader() const { return 22; }

signals:
    void darkModeChanged();
    void primaryColorChanged();

private:
    int m_darkMode = 1;                       // 默认暗色
    QColor m_primaryColor = QColor("#FF1E42"); // 默认激光红
};
