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

    Q_INVOKABLE void startTask(int mode, const QString &srcPort, int srcBaud, const QString &meterPort, int meterBaud, const QVariantList &meterConfigs);

    Q_INVOKABLE void stopCalibration();
    Q_INVOKABLE void triggerCpuCrash();

signals:
    void availablePortsChanged();
    void isCalibratingChanged();
    void showTopMsg(const QString &msg, const QString &type);
    void meterStepStatusChanged(int meterIndex, int step, int status);
    void srcMessage(const QString &msg, const QString &type);
    void updateErrorMeterStatus(int meterIndex, int statusEnum, const QString &desc);
    void appendErrorRow(int meterIndex, int categoryIndex, const QString &rowName, const QVariantList &rowCells);
    void showResultPopup(QString title, QString msg, QString type);

private:
    CalibrationThread *m_calibThread;
    QStringList m_availablePorts;
};

#endif // INSTRUMENT_H