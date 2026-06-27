#ifndef CALIBRATIONTHREAD_H
#define CALIBRATIONTHREAD_H

#include <QThread>
#include <QSerialPort>

class CalibrationThread : public QThread
{
    Q_OBJECT
public:
    explicit CalibrationThread(QObject *parent = nullptr);

    // 接收 Instrument 移交过来的串口指针
    void setPorts(QSerialPort *_232Port, QSerialPort *_485Port);

    // 提供给外部强制停止的方法
    void stopCalibration();

signals:
    // 跨线程发给界面的信号
    void logMessage(const QString &msg);

protected:
    // 只有 run() 里面的代码才是运行在子线程里的！
    void run() override;

private:
    QSerialPort *m_232Port;
    QSerialPort *m_485Port;
    bool m_isRunning;
};

#endif // CALIBRATIONTHREAD_H