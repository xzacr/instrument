#include "calibrationthread.h"
#include <QDebug>

CalibrationThread::CalibrationThread(QObject *parent)
    : QThread(parent), m_232Port(nullptr), m_485Port(nullptr), m_isRunning(false)
{
}

void CalibrationThread::setPorts(QSerialPort *_232Port, QSerialPort *_485Port)
{
    m_232Port = _232Port;
    m_485Port = _485Port;
}

void CalibrationThread::stopCalibration()
{
    m_isRunning = false;
}

void CalibrationThread::run()
{
    m_isRunning = true;

    qInfo() << "[Thread] 后台校准线程启动！";
    emit logMessage("全自动校准流程已启动...");

    // ========== 这里将写最核心的通信轮询逻辑 ==========

    // 模拟一段耗时工作 (死等 3 秒，界面绝对不会卡)
    for(int i=1; i<=3 && m_isRunning; ++i) {
        emit logMessage(QString("正在执行第 %1 步...").arg(i));
        QThread::sleep(1);
    }

    if (m_isRunning) {
        emit logMessage("校准完成！");
    } else {
        emit logMessage("校准被手动中止！");
    }

    // 线程退出前，自动触发 finished 信号
}