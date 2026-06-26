#ifndef INSTRUMENT_H
#define INSTRUMENT_H

#include <QObject>
#include <QDebug>
#include "RS232.h"
#include "RS485.h"

class Instrument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(RS232* rs232 READ rs232 CONSTANT)
    Q_PROPERTY(RS485* rs485 READ rs485 CONSTANT)
public:
    explicit Instrument(QObject *parent = nullptr);

    RS232* rs232() const { return m_rs232; }
    RS485* rs485() const { return m_rs485; }

    // 手动创造崩溃
    Q_INVOKABLE void triggerCpuCrash();

signals:

private:
    RS232 *m_rs232;
    RS485 *m_rs485;
};

#endif // INSTRUMENT_H
