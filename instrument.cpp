#include "instrument.h"
#include <QSerialPortInfo>

Instrument::Instrument(QObject *parent) : QObject{parent}
{
    m_calibThread = new CalibrationThread(this);

    // 中转弹窗信号
    connect(m_calibThread, &CalibrationThread::showTopMessage, this, &Instrument::showTopMsg);
    connect(m_calibThread, &CalibrationThread::meterStepStatusChanged, this, &Instrument::meterStepStatusChanged);
    connect(m_calibThread, &CalibrationThread::srcMessage, this, &Instrument::srcMessage);
    connect(m_calibThread, &QThread::started, this, &Instrument::isCalibratingChanged);
    connect(m_calibThread, &QThread::finished, this, &Instrument::isCalibratingChanged);

    refreshPorts(); // 启动时获取一次串口列表
}

void Instrument::refreshPorts()
{
    QStringList list;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const auto &info : infos) {
        list << info.portName();
    }
    if (m_availablePorts != list) {
        m_availablePorts = list;
        emit availablePortsChanged();
    }
}

// 🌟 新的启动统一分发器
void Instrument::startTask(int mode, const QString &srcPort, int srcBaud, const QString &meterPort, int meterBaud, const QVariantList &meterConfigs)
{
    if (m_calibThread->isRunning()) return;

    // 如果 QML 传了实际配置（比如在校准页面），我们就更新缓存
    if (!srcPort.isEmpty()) {
        m_lastSrcPort = srcPort;
        m_lastSrcBaud = srcBaud;
        m_lastMeterPort = meterPort;
        m_lastMeterBaud = meterBaud;
        m_lastMeterConfigs = meterConfigs;
    }

    if (m_lastMeterConfigs.isEmpty()) {
        emit showTopMsg("未发现有效的仪表配置，请先在校准页面勾选！", "error");
        return;
    }

    // 透传给底层线程去解析并运行
    m_calibThread->setConfig(mode, m_lastSrcPort, m_lastSrcBaud, m_lastMeterPort, m_lastMeterBaud, m_lastMeterConfigs);
    m_calibThread->start();
}

void Instrument::stopCalibration()
{
    if (m_calibThread->isRunning()) m_calibThread->stopCalibration();
}

void Instrument::triggerCpuCrash() {
    int* p = nullptr; *p = 12345;
}