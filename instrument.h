#ifndef INSTRUMENT_H
#define INSTRUMENT_H

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include "calibrationthread.h"

class Instrument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY availablePortsChanged)
    Q_PROPERTY(bool isCalibrating READ isCalibrating NOTIFY isCalibratingChanged)

public:
    explicit Instrument(QObject *parent = nullptr);

    QStringList availablePorts() const { return m_availablePorts; }
    bool isCalibrating() const { return m_calibThread->isRunning(); }

    Q_INVOKABLE void refreshPorts();

    // 统一的启动接口：mode=0(全自动校准), mode=1(单测误差)
    Q_INVOKABLE void startTask(int mode, const QString &srcPort = "", int srcBaud = 0, const QString &meterPort = "", int meterBaud = 0, const QVariantList &meterConfigs = QVariantList());

    Q_INVOKABLE void stopCalibration();
    Q_INVOKABLE void triggerCpuCrash();

signals:
    void availablePortsChanged();
    void isCalibratingChanged();
    void showTopMsg(const QString &msg, const QString &type);
    void meterStepStatusChanged(int meterIndex, int step, int status);
    void srcMessage(const QString &msg, const QString &type);

private:
    CalibrationThread *m_calibThread;
    QStringList m_availablePorts;

    // 缓存全局端口配置，方便误差页面直接一键启动
    QString m_lastSrcPort;
    int m_lastSrcBaud = 38400;
    QString m_lastMeterPort;
    int m_lastMeterBaud = 9600;
    QVariantList m_lastMeterConfigs;
};

#endif // INSTRUMENT_H