#include "log.h"
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QTextStream>
#include <QFileInfoList>
#include <QFileInfo>
//#include <QtThreadNavigation>
#include <QThread>

Log& Log::instance() {
    static Log inst;
    return inst;
}

Log::Log(QObject *parent) : QObject(parent) {
    // 固化绿色路径：exe 同级目录下的 logs 文件夹
    m_logDir = QCoreApplication::applicationDirPath() + "/logs";
}

Log::~Log() {
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
}

void Log::init() {
    QDir dir(m_logDir);
    if (!dir.exists()) {
        dir.mkpath(m_logDir); // 自动物理创建文件夹
    }

    // 全局注册，一秒接管, 所有的 qdebug,qwarning 打印信息都会被存起来
    qInstallMessageHandler(Log::messageHandler);
    qInfo() << "[System]" << "======= 日志系统初始化成功 =======";
}

void Log::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    Q_UNUSED(context);

    // 1. 业务语义过滤与级别划分
    QString levelStr;
    switch (type) {
    case QtDebugMsg:    levelStr = "[DEBG]"; break;
    case QtInfoMsg:     levelStr = "[INFO]"; break;
    case QtWarningMsg:  levelStr = "[WARN]"; break;
    case QtCriticalMsg: levelStr = "[ERRO]"; break;
    case QtFatalMsg:    levelStr = "[FATL]"; break;
    }

    // 2. 毫秒级时间戳 + 跨线程ID提取
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    //quint64 threadId = reinterpret_cast<quint64>(QThread::currentThreadId());
    // 2. 【核心破锁线】：从当前的执行物理核中逆向抓取人类可读名字
    QThread* currentThread = QThread::currentThread();
    QString threadLabel = "MainThread"; // 默认主线程（主 UI 线程没设置名字时默认兜底）

    if (currentThread) {
        QString name = currentThread->objectName();
        if (!name.isEmpty()) {
            threadLabel = name; // 精准逆向转换为 "SerialThread" 或 "TcpThread"
        }
    }

    // 3. 完美工业格式装配
    QString formattedMsg = QString("[%1] [T:%2] %3 %4\n")
                               .arg(timeStr)
                               .arg(threadLabel) // 格式化线程ID对齐
                               .arg(levelStr)
                               .arg(msg);

    // 4. 送入单例对象的互斥写入流
    Log::instance().write(formattedMsg);
}

void Log::write(const QString &formattedMessage) {
    QMutexLocker locker(&m_mutex); // 🛡️ 强制多线程加锁排队，杜绝文件抢占崩溃

    QString today = QDateTime::currentDateTime().toString("yyyy-MM-dd");

    // 检测日期是否改变，或者文件未打开
    if (today != m_currentDateStr || !m_logFile.isOpen()) {
        if (m_logFile.isOpen()) m_logFile.close();

        m_currentDateStr = today;
        QString filePath = QString("%1/log_%2.log").arg(m_logDir).arg(m_currentDateStr);
        m_logFile.setFileName(filePath);

        // Append 模式打开，确保 Release 崩溃重启后日志可无缝续写
        m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }

    if (m_logFile.isOpen()) {
        QTextStream stream(&m_logFile);
        stream << formattedMessage;
        stream.flush(); // 定时硬刷盘，防断电丢失

        // 💾 每次写入后，在 C++ 层面无感执行空间检查，将 IO 损耗压制在微秒级
        checkAndCleanSpace();
    }
}

void Log::checkAndCleanSpace() {
    QDir dir(m_logDir);
    // 只扫描我们自己生成的 .log 文件
    QStringList filters;
    filters << "*.log";

    // 按照【名称】排序：因为名字带有 yyyy-MM-dd，字典序升序排列天然就是“从最早到最新”
    dir.setNameFilters(filters);
    dir.setSorting(QDir::Name | QDir::IgnoreCase);

    QFileInfoList fileList = dir.entryInfoList();

    // 1. 递归计算当前 logs 文件夹的总大小
    qint64 currentTotalSize = 0;
    for (const QFileInfo &fileInfo : fileList) {
        currentTotalSize += fileInfo.size();
    }

    // 2. 超过 100M 硬卡口，开始循环剔除最早的日期文件
    int fileIndex = 0;
    while (currentTotalSize > MAX_TOTAL_SIZE && fileIndex < fileList.size()) {
        QFileInfo targetDelete = fileList.at(fileIndex);

        // ⚠️ 防御拦截：当前正在写入的今天的日志绝对不能误删
        if (targetDelete.fileName() == m_logFile.fileName()) {
            break;
        }

        qint64 deletedSize = targetDelete.size();
        if (QFile::remove(targetDelete.absoluteFilePath())) {
            currentTotalSize -= deletedSize;
            qWarning() << "[LogSystem]" << "检测到存储超限！已物理清除最早日期的老日志:" << targetDelete.fileName();
        }
        fileIndex++;
    }
}
