#ifndef INSTRUMENT_H
#define INSTRUMENT_H

#include <QObject>
#include <QStringList>
#include "calibrationthread.h"

class Instrument : public QObject
{
    Q_OBJECT
    // 提供给界面的下拉框使用
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY availablePortsChanged)
    Q_PROPERTY(bool isCalibrating READ isCalibrating NOTIFY isCalibratingChanged)

public:
    explicit Instrument(QObject *parent = nullptr);

    QStringList availablePorts() const { return m_availablePorts; }

    Q_INVOKABLE void refreshPorts(); // 刷新串口列表
    // 🌟 将原有方法改为更通用的 startTask，并允许传空参数以利用缓存
    Q_INVOKABLE void startTask(int mode, const QString &srcPort = "", int srcBaud = 0, const QString &meterPort = "", int meterBaud = 0, const QVariantList &meterConfigs = QVariantList());
    Q_INVOKABLE void stopCalibration();

    bool isCalibrating() const { return m_calibThread->isRunning(); }

    Q_INVOKABLE void triggerCpuCrash(); // 保留你的测试接口

signals:
    void availablePortsChanged();
    void showTopMsg(const QString &msg, const QString &type);
    void isCalibratingChanged();
    void meterStepStatusChanged(int meterIndex, int stepIndex, int status);
    void srcMessage(const QString &msg, const QString &type);

private:
    CalibrationThread *m_calibThread;
    QStringList m_availablePorts;

    // 🌟 缓存全局端口配置（使得无需在 ErrorCalc 中再配一次）
    QString m_lastSrcPort;
    int m_lastSrcBaud = 38400;
    QString m_lastMeterPort;
    int m_lastMeterBaud = 9600;
    QVariantList m_lastMeterConfigs;
};

#endif // INSTRUMENT_H