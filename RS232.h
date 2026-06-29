#ifndef RS232_H
#define RS232_H

#include <QObject>
#include <QSerialPort>
#include <QByteArray>
#include <QTimer>

class RS232 : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY statusChanged)
    Q_PROPERTY(QStringList portList READ portList NOTIFY portListChanged)
    Q_PROPERTY(QString lastError READ lastError CONSTANT)
public:
    explicit RS232(QObject *parent = nullptr);
    ~RS232();

    QString lastError(){return m_lastError;}

    // 获取当前系统可用串口列表
    QStringList portList() const { return m_portList; }

    // 刷新串口函数：手动触发检测并更新模型
    Q_INVOKABLE void refreshPorts();

    // 基础控制
    Q_INVOKABLE bool openPort(const QString &portName, int baudRate);
    Q_INVOKABLE bool closePort();
    bool isOpen() const { return m_serial->isOpen(); }

    // --- 多线程交接需要的核心接口 ---
    QSerialPort* getSerialPort() { return m_serial; }
    void suspendAsyncMode();
    void resumeAsyncMode();

signals:
    void statusChanged();                      // 串口开关状态改变
    void portListChanged();
    void errorOccurred(const QString &err);

private slots:
    void onReadyRead(); // 处理接收
    void onTimeout();   // 处理断帧（Modbus 常用逻辑）
    void onSerialError(QSerialPort::SerialPortError error);
private:
    QSerialPort *m_serial;
    QByteArray m_buffer;    // 接收缓冲区
    QTimer *m_frameTimer;   // 断帧定时器
    QStringList m_portList;
    QString m_lastError;
};

#endif
