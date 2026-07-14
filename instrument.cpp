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
    connect(m_calibThread, &CalibrationThread::updateErrorMeterStatus,this, &Instrument::updateErrorMeterStatus);
    connect(m_calibThread, &CalibrationThread::appendErrorRow,this, &Instrument::appendErrorRow);
    connect(m_calibThread, &CalibrationThread::showResultPopup,this, &Instrument::showResultPopup);
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

void Instrument::startTask(int mode, const QString &srcPort, int srcBaud, const QString &meterPort, int meterBaud, const QVariantList &meterConfigs)
{
    // 1. 防重复启动保护
    if (m_calibThread->isRunning()) return;

    // 2. 统一接管启动成功提示
    if (mode == 0) {
        qDebug("全自动校准序列已启动");
    } else {
        qDebug("单次误差测试已启动");
    }

    // 3. 直接把前端传进来的参数，原封不动地透传给底层工作线程！
    m_calibThread->setConfig(mode, srcPort, srcBaud, meterPort, meterBaud, meterConfigs);

    // 4. 发车！
    m_calibThread->start();
}

void Instrument::stopCalibration()
{
    if (m_calibThread->isRunning()) m_calibThread->stopCalibration();
}

void Instrument::triggerCpuCrash() {
    int* p = nullptr; *p = 12345;
}