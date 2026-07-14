#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include "log.h"
#include "dump_handler.h"
#include "instrument.h"

int main(int argc, char *argv[])
{
    initCrashHandler();     //崩溃dmp文件

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("YZY");         // 可以改成您公司或团队的名字（如：InstrumentTech）
    QCoreApplication::setOrganizationDomain("YZY.com");   // 您的域名或组织标识（如：instrument.local）
    QCoreApplication::setApplicationName("仪表校准"); // 软件的名称
    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("appDirPath", QCoreApplication::applicationDirPath());

#ifndef QT_DEBUG
    Log::instance().init(); // 🚀 只有 Release 才会物理运行，Debug 下直接在源码层面被人间蒸发！
#endif

    Instrument* ins = new Instrument(&app);
    // 网关类设置成上下文属性
    engine.rootContext()->setContextProperty("ins", ins);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("instrument", "Main");

    return QCoreApplication::exec();
}
