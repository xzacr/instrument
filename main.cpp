#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include "log.h"
#include "dump_handler.h"
#include "meter.h"

int main(int argc, char *argv[])
{
    initCrashHandler();     //崩溃dmp文件

    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

#ifndef QT_DEBUG
    Log::instance().init(); // 🚀 只有 Release 才会物理运行，Debug 下直接在源码层面被人间蒸发！
#endif

    Meter* meter = new Meter(&app);
    // 网关类设置成上下文属性
    engine.rootContext()->setContextProperty("meter", meter);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("instrument", "Main");

    return QCoreApplication::exec();
}
