#include "calibrationthread.h"
#include <QDebug>
#include <QThread>

CalibrationThread::CalibrationThread(QObject *parent)
    : QThread(parent), m_isRunning(false) {}

CalibrationThread::~CalibrationThread()
{
    if (isRunning()) {
        qWarning() << "析构强停：正在强行关闭标准源...";
        stopCalibration();
        wait(); // 阻塞等待子线程彻底死透，确保安全收尾指令发出
    }
}

void CalibrationThread::setConfig(const QString &srcPort, int srcBaud, const QString &meterPort, int meterBaud) {
    m_srcPortName = srcPort; m_srcBaud = srcBaud;
    m_meterPortName = meterPort; m_meterBaud = meterBaud;
}

void CalibrationThread::stopCalibration() {
    m_isRunning = false;
}

// =========================================================================
// 核心流水线主引擎（经典的线性阻塞结构 + 统一安全出口）
// =========================================================================
void CalibrationThread::run()
{
    m_isRunning = true;

    QSerialPort srcPort;
    srcPort.setPortName(m_srcPortName);
    srcPort.setBaudRate(m_srcBaud);

    QSerialPort meterPort;
    meterPort.setPortName(m_meterPortName);
    meterPort.setBaudRate(m_meterBaud);

    if (!srcPort.open(QIODevice::ReadWrite)) {
        emit showTopMessage("标准源串口打开失败: " + translateSerialError(srcPort.error()), "error");
        return;
    }
    if (!meterPort.open(QIODevice::ReadWrite)) {
        emit showTopMessage("仪表串口打开失败: " + translateSerialError(meterPort.error()), "error");
        return;
    }

    // 绑定总线硬件中断监控
    connect(&srcPort, &QSerialPort::errorOccurred, this, &CalibrationThread::onSourcePortError);
    connect(&meterPort, &QSerialPort::errorOccurred, this, &CalibrationThread::onMeterPortError);

    qDebug()<<"串口打开成功!";

    if (!handshakeSource(srcPort)) goto INIT_ERROR;

    // =========================================================
    // 阶段一：输出 220V 5A 1.0PF
    // =========================================================
    qDebug("1. 下发 220V 5A 1.0PF 配置...");
    if (!sendSourceCmd(srcPort, m_cfgCmd1)) goto ABORT_PROCESS;

    QThread::msleep(1500);

    qDebug("2. 启动标准源输出...");
    if (!sendSourceCmd(srcPort, m_startCmd)) goto ABORT_PROCESS;

    QThread::sleep(3);

    qInfo() << "正在全通道监测物理输出，等待三相全量稳定...";
    // 自动闭环等三相全部达到 220V 5A
    if (!waitSourceStable(srcPort, 1.0f, 2000)) goto ABORT_PROCESS;

    if (!m_isRunning) goto ABORT_PROCESS;

    // =========================================================
    // 阶段二：仪表 1.0PF 交互校验
    // =========================================================
    qDebug("3. 解除写保护 (寄存器 1FFF 写入 1)...");
    writeMeterReg(meterPort, m_mAddr, m_regWriteProtect, 1);
    if (!waitMeterState(meterPort, m_mAddr, m_regWriteProtect, 1, 2000)) goto ABORT_PROCESS;

    qDebug("4. 寄存器 2000 写入 0 (置为空闲)...");
    writeMeterReg(meterPort, m_mAddr, m_regState, 0);
    if (!waitMeterState(meterPort, m_mAddr, m_regState, 0, 2000)) goto ABORT_PROCESS;

    qDebug("5. 寄存器 2000 写入 1 (校准准备中) -> 等待变 2 (准备完毕))...");
    writeMeterReg(meterPort, m_mAddr, m_regState, 1);
    if (!waitMeterState(meterPort, m_mAddr, m_regState, 2, 2000)) goto ABORT_PROCESS;

    qDebug("6. 寄存器 2000 写入 3 (计算PF=1.0下的参数) -> 变 4 (计算完毕))...");
    writeMeterReg(meterPort, m_mAddr, m_regState, 3);
    if (!waitMeterState(meterPort, m_mAddr, m_regState, 4, 2000)) goto ABORT_PROCESS;

    if (!m_isRunning) goto ABORT_PROCESS;

    // =========================================================
    // 阶段三：输出 220V 5A 0.5PF
    // =========================================================
    qDebug("7. 切换源至 0.5PF...");
    if (!sendSourceCmd(srcPort, m_cfgCmd2)) goto ABORT_PROCESS;

    qDebug("8. 启动标准源输出...");
    if (!sendSourceCmd(srcPort, m_startCmd)) goto ABORT_PROCESS;

    QThread::sleep(5);

    qInfo() << "正在全通道监测物理输出，等待三相全量稳定(0.5PF)...";
    // 自动闭环等三相全部达到 220V 5A
    if (!waitSourceStable(srcPort, 0.5f, 2000)) goto ABORT_PROCESS;

    if (!m_isRunning) goto ABORT_PROCESS;

    // =========================================================
    // 阶段四：仪表 0.5PF 交互校验
    // =========================================================

    qDebug("9. 寄存器 2000 写入 5 (等待相位校准完成 -> 变6)...");
    writeMeterReg(meterPort, m_mAddr, m_regState, 5);
    if (!waitMeterState(meterPort, m_mAddr, m_regState, 6, 2000)) goto ABORT_PROCESS;

    qDebug("10. 寄存器 2000 写入 7 (等待复位 -> 变0)...");
    writeMeterReg(meterPort, m_mAddr, m_regState, 7, false); //false 只管发不管接收
    QThread::msleep(1500); // 避开单片机复位
    if (!waitMeterState(meterPort, m_mAddr, m_regState, 0, 2000)) goto ABORT_PROCESS;

    if (m_isRunning) {
        qDebug("====== 校准流程圆满结束 ======");
        return;
    }

ABORT_PROCESS:
    qWarning() << ">>> [拉闸报警] 流程异常中断！正在向物理总线追发强停命令...";
    srcPort.write(m_stopCmd);
    srcPort.waitForBytesWritten(500);
    // 捕获强停后标准源抛出的最后应答
    if (srcPort.waitForReadyRead(500)) {
        qInfo().noquote() << "[Rx 源强停确认]" << srcPort.readAll().toHex(' ').toUpper();
    }

INIT_ERROR:
    srcPort.close();
    meterPort.close();
    qInfo() << "====== INIT_ERROR 串口已安全释放 ======";
}

void CalibrationThread::onSourcePortError(QSerialPort::SerialPortError error)
{
    if (error != QSerialPort::NoError && error != QSerialPort::TimeoutError) {
        m_isRunning = false;
        QString errMsg = translateSerialError(error);
        emit showTopMessage("标准源总线断开: " + errMsg, "error");
        qCritical() << "💥 [硬件中断] 标准源触发物理错误:" << errMsg;
    }
}

void CalibrationThread::onMeterPortError(QSerialPort::SerialPortError error)
{
    if (error != QSerialPort::NoError && error != QSerialPort::TimeoutError) {
        m_isRunning = false;
        QString errMsg = translateSerialError(error);
        emit showTopMessage("仪表 485 总线断开: " + errMsg, "error");
        qCritical() << "💥 [硬件中断] 仪表 485 触发物理错误:" << errMsg;
    }
}

// ---------------------------------------------------------
// 底层控制协议与轮询引擎 (直接追加至 cpp 尾部)
// ---------------------------------------------------------

bool CalibrationThread::sendSourceCmd(QSerialPort &port, const QByteArray &cmdHex)
{
    // 🌟 1. 打印发给标准源的报文
    qInfo().noquote() << "[Tx 标准源]" << cmdHex.toHex(' ').toUpper();
    port.write(cmdHex);

    if (!checkSourceResponse(port)) return false;

    QByteArray rx = port.readAll();

    // 🌟 2. 打印标准源返回的报文
    qInfo().noquote() << "[Rx 标准源]" << rx.toHex(' ').toUpper();

    if (rx == m_srcNack) {
        qCritical() << "❌ [拒绝执行] 标准源返回否定全帧(NACK)！指令被拒收。";
        emit showTopMessage("标准源配置被拒(否定应答)", "error");
        m_isRunning = false;
        return false;
    }
    if (rx == m_srcAck) {
        return true; // 完美的 8 字节全帧匹配，放行
    }

    emit showTopMessage("标准源应答特征不匹配", "error");
    m_isRunning = false;
    return false;
}

quint16 CalibrationThread::calculateCRC(const QByteArray &data) {
    quint16 crc = 0xFFFF;
    for (int pos = 0; pos < data.length(); pos++) {
        crc ^= (quint8)data.at(pos);
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1; crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

bool CalibrationThread::writeMeterReg(QSerialPort &port, quint8 addr, quint16 reg, quint16 value, bool expectAck)
{
    QByteArray frame;
    frame.append(addr).append(0x06);
    frame.append(reg >> 8).append(reg & 0xFF);
    frame.append(value >> 8).append(value & 0xFF);
    quint16 crc = calculateCRC(frame);
    frame.append(crc & 0xFF).append(crc >> 8);

    qInfo().noquote() << QString("[Tx 仪表%1]").arg(addr) << frame.toHex(' ').toUpper();

    port.write(frame);

    // 🌟【核心防呆】：如果这是复位指令，不指望它回包，写完直接算成功！
    if (!expectAck) {
        port.waitForBytesWritten(100); // 确保数据从 USB 线塞出去了就行
        return true;
    }

    // 正常的写指令，严格检查应答
    if (!checkMeterResponse(port, addr)) return false;

    QByteArray rx = port.readAll();
    qInfo().noquote() << QString("[Rx 仪表%1]").arg(addr) << rx.toHex(' ').toUpper();

    return true;
}

bool CalibrationThread::readMeterReg(QSerialPort &port, quint8 addr, quint16 reg, quint16 &outValue)
{
    QByteArray frame;
    frame.append(addr).append(0x03);
    frame.append(reg >> 8).append(reg & 0xFF);
    frame.append((char)0x00).append(0x01);
    quint16 crc = calculateCRC(frame);
    frame.append(crc & 0xFF).append(crc >> 8);

    // 🌟 1. 打印发给仪表的 03 读命令
    qInfo().noquote() << QString("[Tx 仪表%1]").arg(addr) << frame.toHex(' ').toUpper();

    port.write(frame);
    if (!checkMeterResponse(port, addr)) return false;

    QByteArray rx = port.readAll();

    // 🌟 2. 打印仪表返回的报文
    qInfo().noquote() << QString("[Rx 仪表%1]").arg(addr) << rx.toHex(' ').toUpper();

    if (rx.size() >= 7 && rx[0] == addr && rx[1] == 0x03) {
        outValue = ((quint8)rx[3] << 8) | (quint8)rx[4];
        return true;
    }
    return false;
}

bool CalibrationThread::waitMeterState(QSerialPort &port, quint8 addr, quint16 reg, quint16 targetState, int timeoutMs)
{
    int elapsed = 0;
    while(elapsed < timeoutMs && m_isRunning) {
        quint16 val = 0;
        if (readMeterReg(port, addr, reg, val) && val == targetState) {
            return true;
        }
        QThread::msleep(300);
        elapsed += 300;
    }

    if (m_isRunning) {
        emit showTopMessage(QString("仪表 %1 状态轮询超时").arg(addr), "error");
        qDebug()<<QString("等待状态机寄存器变更为 %2 失败").arg(targetState);
        m_isRunning = false;
    }
    return false;
}

bool CalibrationThread::handshakeSource(QSerialPort &port)
{
    qInfo() << ">>> 正在与标准源握手";
    port.write(m_handshakeCmd);

    if (!checkSourceResponse(port)) return false;

    QByteArray rx = port.readAll();
    qInfo().noquote() << "[Rx 握手响应]" << rx.toHex(' ').toUpper();

    // 全帧精准对比
    if (rx == m_srcNack) {
        qCritical() << "❌ [拒绝服务] 标准源返回否定全帧！";
        emit showTopMessage("标准源返回否定应答，握手失败", "error");
        m_isRunning = false;
        return false;
    }
    if (rx == m_srcAck) {
        qInfo() << ">>> 标准源握手成功！设备在线。";
        return true;
    }
    return false;
}

bool CalibrationThread::checkSourceResponse(QSerialPort &port) {
    if (!port.waitForBytesWritten(200) || !port.waitForReadyRead(1000)) {
        QSerialPort::SerialPortError err = port.error();
        if (err == QSerialPort::NoError) err = QSerialPort::TimeoutError;

        QString errStr = translateSerialError(err);
        emit showTopMessage("标准源中断: " + errStr, "error");
        m_isRunning = false;
        return false;
    }
    while (port.waitForReadyRead(20)) {
    }
    return true;
}

bool CalibrationThread::checkMeterResponse(QSerialPort &port, int meterIndex) {
    if (!port.waitForBytesWritten(200) || !port.waitForReadyRead(1000)) {
        QSerialPort::SerialPortError err = port.error();
        if (err == QSerialPort::NoError) err = QSerialPort::TimeoutError;

        if (err != QSerialPort::TimeoutError) {
            emit showTopMessage("总线物理故障: " + translateSerialError(err), "error");
            m_isRunning = false;
        } else {
            qWarning() << "[Thread] 仪表" << meterIndex << "请求超时跳过";
        }
        return false;
    }
    while (port.waitForReadyRead(20)) {
    }
    return true;
}

bool CalibrationThread::waitSourceStable(QSerialPort &port, float targetPF, int timeoutMs)
{
    // 1. 定义物理基准线
    float tgtV = 220.0f;
    float tgtI = 5.0f;
    float tgtFreq = 50.0f;

    // 2. 自动计算三相相位基准 (A、B、C 相差 120 度)
    float tgtVpA = 0.0f, tgtVpB = 240.0f, tgtVpC = 120.0f;
    float tgtIpA = (targetPF == 1.0f) ? 0.0f   : 300.0f;
    float tgtIpB = (targetPF == 1.0f) ? 240.0f : 180.0f;
    float tgtIpC = (targetPF == 1.0f) ? 120.0f : 60.0f;

    // 3. Lambda 检测器
    auto okVal = [](float c, float t, float tol) { return qAbs(c - t) < tol; };
    auto okPhs = [](float c, float t) {
        float d = qAbs(c - t);
        if (d > 180.0f) d = qAbs(d - 360.0f); // 解决 0度和360度的边界问题
        return d < 1.0f; // 相位允许 1度 偏差
    };

    int elapsed = 0;
    while(elapsed < timeoutMs) {
        if (!m_isRunning) return false;

        qInfo().noquote() << "[Tx 全量查询]" << m_queryAllCmd.toHex(' ').toUpper();
        port.write(m_queryAllCmd);

        if (checkSourceResponse(port)) {
            QByteArray rx = port.readAll();

            if (rx == m_srcNack) {
                qCritical() << "❌ [拒绝读取] 标准源返回否定全帧，输出未就绪！";
                m_isRunning = false; return false;
            }

            // 🌟 108 字节全量帧长校验！
            if (rx.size() >= 108 && rx[0] == (char)0x68 && rx[5] == (char)0x91) {
                float ua, uap, ub, ubp, uc, ucp;
                float ia, iap, ib, ibp, ic, icp;
                float fAB, fC;

                // 暴力内存强拆：对照 20 项配置的死板偏移量提取 14 个物理浮点数
                memcpy(&ua,  rx.constData() + 7,  4); memcpy(&uap, rx.constData() + 12, 4);
                memcpy(&ub,  rx.constData() + 22, 4); memcpy(&ubp, rx.constData() + 27, 4);
                memcpy(&uc,  rx.constData() + 37, 4); memcpy(&ucp, rx.constData() + 42, 4);

                memcpy(&ia,  rx.constData() + 52, 4); memcpy(&iap, rx.constData() + 57, 4);
                memcpy(&ib,  rx.constData() + 67, 4); memcpy(&ibp, rx.constData() + 72, 4);
                memcpy(&ic,  rx.constData() + 82, 4); memcpy(&icp, rx.constData() + 87, 4);

                memcpy(&fAB, rx.constData() + 97, 4); memcpy(&fC,  rx.constData() + 102, 4);

                // 打印极其壮观的 3D 物理日志
                qInfo() << QString(">>> [全量监测] V: %1/%2/%3V | I: %4/%5/%6A | F: %7Hz")
                               .arg(ua,0,'f',1).arg(ub,0,'f',1).arg(uc,0,'f',1)
                               .arg(ia,0,'f',2).arg(ib,0,'f',2).arg(ic,0,'f',2).arg(fAB,0,'f',2);
                qInfo() << QString(">>> [相位监测] U∠: %1/%2/%3° | I∠: %4/%5/%6°")
                               .arg(uap,0,'f',1).arg(ubp,0,'f',1).arg(ucp,0,'f',1)
                               .arg(iap,0,'f',1).arg(ibp,0,'f',1).arg(icp,0,'f',1);

                // ==========================================
                // 启动无死角地毯式校验
                // ==========================================
                bool vOk = okVal(ua, tgtV, 0.5f) && okVal(ub, tgtV, 0.5f) && okVal(uc, tgtV, 0.5f);
                bool iOk = okVal(ia, tgtI, 0.05f) && okVal(ib, tgtI, 0.05f) && okVal(ic, tgtI, 0.05f);
                bool fOk = okVal(fAB, tgtFreq, 0.5f) && okVal(fC, tgtFreq, 0.5f);

                bool vpOk = okPhs(uap, tgtVpA) && okPhs(ubp, tgtVpB) && okPhs(ucp, tgtVpC);
                bool ipOk = okPhs(iap, tgtIpA) && okPhs(ibp, tgtIpB) && okPhs(icp, tgtIpC);

                if (vOk && iOk && fOk && vpOk && ipOk) {
                    qInfo() << ">>> [无死角校验通过] 你下发的 20 项参数已全量达标并稳定生效！";
                    return true;
                }
            } else {
                qWarning().noquote() << "[Rx 错帧或残包]" << rx.toHex(' ').toUpper();
            }
        }

        QThread::msleep(500);
        elapsed += 500;
    }

    if (m_isRunning) {
        emit showTopMessage("致命超时：标准源全量参数未能同时达标稳住！", "error");
        m_isRunning = false;
    }
    return false;
}

QString CalibrationThread::translateSerialError(QSerialPort::SerialPortError error) {
    switch (error) {
    case QSerialPort::NoError:                 return "正常";
    case QSerialPort::DeviceNotFoundError:     return "未找到物理设备";
    case QSerialPort::PermissionError:         return "端口占用或被拔出";
    case QSerialPort::OpenError:               return "硬件挂起";
    case QSerialPort::NotOpenError:            return "串口处于未打开状态，无法通信";
    case QSerialPort::WriteError:              return "硬件底层写入数据发生硬件死锁";
    case QSerialPort::ReadError:               return "硬件底层读取数据发生硬件死锁";
    case QSerialPort::ResourceError:           return "物理总线强行拔出";
    case QSerialPort::UnsupportedOperationError:   return "当前操作系统不支持该串口配置";
    case QSerialPort::TimeoutError:            return "响应超时";
    case QSerialPort::UnknownError:
    default:                                   return "未知底层故障";
    }
}

