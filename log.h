#pragma once
#include <QObject>
#include <QFile>
#include <QMutex>
#include <QMessageLogContext>

class Log : public QObject {
    Q_OBJECT
public:
    static Log& instance();

    // 初始化：创建文件夹并绑定 Qt 消息钩子
    void init();

    // 核心流控处理函数（必须是 static 才能注册进 qInstallMessageHandler）
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

private:
    explicit Log(QObject *parent = nullptr);
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    void write(const QString &formattedMessage);
    void checkAndCleanSpace(); // 🔍 空间检查与最早日期物理清理

    QFile m_logFile;
    QMutex m_mutex;            // 🛡️ 跨线程防崩溃互斥锁
    QString m_logDir;          // 日志存放文件夹路径
    QString m_currentDateStr;  // 记录当前的日期（按天切换）

    const qint64 MAX_TOTAL_SIZE = 100 * 1024 * 1024; // 💾 全局硬卡口：100 MB
};
