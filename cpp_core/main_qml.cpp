#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#endif
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include "glue/FluTheme.hpp"
#include "glue/BiliController.hpp"
#include "glue/BiliTrackModel.hpp"

#include <QFile>
#include <QTextStream>
#include <QDateTime>

void customLogHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    QFile f("bili_ui.log");
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ")
           << "[" << type << "] " << msg << "\n";
    }
}

int main(int argc, char *argv[]) {
    qInstallMessageHandler(customLogHandler);
    // 启用高分屏缩放与抗锯齿渲染
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
#ifdef _WIN32
    SetCurrentProcessExplicitAppUserModelID(L"KAZUSA.BiliDown.App.2.0");
#endif
    app.setOrganizationName("KAZUSA");
    app.setApplicationName("KAZUSA BILIDOWN");
    app.setWindowIcon(QIcon(":/kazusa.ico"));

    // C++ 控制器与主题单例 (先于 engine 构造, 保证销毁顺序在其后)
    BiliController controller;
    FluTheme theme;
    if (!controller.themeColor().isEmpty()) {
        theme.setPrimaryColor(QColor(controller.themeColor()));
    }

    QQmlApplicationEngine engine;
    // QML 模块路径: 包含 Qt 官方 QML 库与 qrc 内嵌资源
    engine.addImportPath(QStringLiteral("D:/Qt/6.8.3/msvc2022_64/qml"));
    engine.addImportPath(QStringLiteral("qrc:/"));

    // 全局上下文属性 (与 Python 桥版本一致, QML 无需 import 即可引用)
    engine.rootContext()->setContextProperty("FluTheme", &theme);
    engine.rootContext()->setContextProperty("biliController", &controller);

    // 注册 QML 识别的模型类
    qmlRegisterType<VideoTrackModel>("KAZUSA", 1, 0, "VideoTrackModel");
    qmlRegisterType<VideoPageModel>("KAZUSA", 1, 0, "VideoPageModel");

    // 加载 QML 界面: qrc 内嵌资源优先, 失败则回退到本地 qml/ 目录 (开发模式)
    bool loaded = false;
    const QUrl embeddedUrl(QStringLiteral("qrc:/qml/main.qml"));
    engine.load(embeddedUrl);
    if (!engine.rootObjects().isEmpty()) {
        loaded = true;
    } else {
        qWarning() << "[QML] 内嵌资源加载失败, 回退本地 qml/ 目录...";
        const QStringList fallbackPaths = {
            QDir::currentPath() + "/qml",
            QCoreApplication::applicationDirPath() + "/qml",
        };
        for (const QString &p : fallbackPaths) {
            if (!QFileInfo::exists(p + "/main.qml")) continue;
            engine.addImportPath(p);
            engine.clearComponentCache();
            engine.load(QUrl::fromLocalFile(p + "/main.qml"));
            if (!engine.rootObjects().isEmpty()) { loaded = true; break; }
        }
    }

    if (!loaded) {
        qFatal("无法加载 QML 界面: 请确认 qml 资源已内嵌或 qml/ 目录存在");
        return -1;
    }

    return app.exec();
}
