#ifndef METER_H
#define METER_H

#include <QObject>
#include <QDebug>

class Meter : public QObject
{
    Q_OBJECT
public:
    explicit Meter(QObject *parent = nullptr);

    // 手动创造崩溃
    Q_INVOKABLE void triggerCpuCrash();

signals:
};

#endif // METER_H
