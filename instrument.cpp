#include "instrument.h"
#include <QSerialPortInfo>

Instrument::Instrument(QObject *parent) : QObject{parent}
{
    m_calibThread = new CalibrationThread(this);

    // 中转弹窗信号
    connect(m_calibThread, &CalibrationThread::showTopMessage, this, &Instrument::showTopMsg);
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

void Instrument::startCalibration(const QString &srcPort, int srcBaud, const QString &meterPort, int meterBaud)
{
    if (m_calibThread->isRunning()) return;

    m_calibThread->setConfig(srcPort, srcBaud, meterPort, meterBaud);
    m_calibThread->start();
}

void Instrument::stopCalibration()
{
    if (m_calibThread->isRunning()) m_calibThread->stopCalibration();
}

void Instrument::triggerCpuCrash() {
    int* p = nullptr; *p = 12345;
}