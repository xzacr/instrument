#include "RS232.h"
#include <QDebug>
#include <QSerialPortInfo>
#include <QDateTime>
#include <QRegularExpression>
#include <QStringConverter>

RS232::RS232(QObject *parent) : QObject(parent)
{
    m_serial = new QSerialPort(this);
    m_frameTimer = new QTimer(this);
    m_frameTimer->setSingleShot(true); // 只触发一次

    // 信号槽连接
    connect(m_serial, &QSerialPort::readyRead, this, &RS232::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &RS232::onSerialError);
    connect(m_frameTimer, &QTimer::timeout, this, &RS232::onTimeout);

    refreshPorts();
}

RS232::~RS232() {
    closePort();
}

void RS232::refreshPorts()
{
    QStringList newList;
    // 获取系统中所有可用的串口信息
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        newList << info.portName();
    }

    // 只有当列表真的发生变化时才更新，减少 UI 刷新压力
    if (m_portList != newList) {
        m_portList = newList;
        emit portListChanged();
    }
}

bool RS232::openPort(const QString &portName, int baudRate) {
    if (m_serial->isOpen()) m_serial->close();

    m_serial->setPortName(portName);
    m_serial->setBaudRate(baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);

    if (m_serial->open(QIODevice::ReadWrite)) {
        qDebug()<<"rs232串口打开成功"<<portName<<baudRate;
        emit statusChanged();
        return true;
    }
    m_lastError = m_serial->errorString();
    emit statusChanged();
    return false;
}

bool RS232::closePort() {
    if (m_serial->isOpen()) {
        m_serial->close();
        emit statusChanged();
        return true;
    }
    emit statusChanged();
    return false;
}

// 核心接收逻辑：收到字节就塞进 Buffer，并重启定时器
void RS232::onReadyRead() {
    m_buffer.append(m_serial->readAll());
    m_frameTimer->start(50); // 50ms 没后续数据则认为是一帧结束
}

// 核心断帧逻辑：定时器到时，说明这帧收齐了
void RS232::onTimeout() {
    if (!m_buffer.isEmpty()) {
        QString tiem = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        //qDebug() << "<<< 接收 hex:" <<tiem << m_buffer;
        qDebug() << "<<< 接收 str:" <<tiem << m_buffer;

        m_buffer.clear();            // 清空缓冲区准备下一帧
    }
}

void RS232::onSerialError(QSerialPort::SerialPortError error)
{
    // 1. 🚀【前置阻断】NoError 代表正常通信完成，必须直接跳过，严防误伤正常业务
    if (error == QSerialPort::NoError) return;

    // 依据工业现场常见工况进行二次中文翻译清洗
    QString errorChineseDesc = "";
    switch (error) {
    case QSerialPort::DeviceNotFoundError:
        errorChineseDesc = "未找到指定的物理串口设备";
        break;
    case QSerialPort::PermissionError:
        errorChineseDesc = "串口已被拔出或占用";
        break;
    case QSerialPort::OpenError:
        errorChineseDesc = "串口打开失败，硬件已被挂起";
        break;
    case QSerialPort::NotOpenError:
        errorChineseDesc = "串口处于未打开状态，无法通信";
        break;
    case QSerialPort::WriteError:
        errorChineseDesc = "硬件底层写入数据发生硬件死锁";
        break;
    case QSerialPort::ReadError:
        errorChineseDesc = "硬件底层读取数据发生硬件死锁";
        break;
    case QSerialPort::ResourceError:
        errorChineseDesc = "物理总线已被现场拔出";
        break;
    case QSerialPort::UnsupportedOperationError:
        errorChineseDesc = "当前操作系统不支持该串口配置";
        break;
    case QSerialPort::UnknownError:
    default:
        errorChineseDesc = "未知错误";
        break;
    }

    qWarning() << "串口错误：" << errorChineseDesc;

    if (m_serial->isOpen()) {
        m_serial->close();
    }
    emit statusChanged();
    // 发射给控制台文本框或 DeployDialog 弹窗视口进行 100% 准确同步显示
    emit errorOccurred(errorChineseDesc);
}
