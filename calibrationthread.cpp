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

void CalibrationThread::setConfig(const QString &srcPort, int srcBaud, const QString &meterPort, int meterBaud, const QVariantList &meterConfigs)
{
    m_srcPortName = srcPort; m_srcBaud = srcBaud;
    m_meterPortName = meterPort; m_meterBaud = meterBaud;

    // 解析前端传来的 5 台表配置
    m_meterTasks.clear();
    for (int i = 0; i < meterConfigs.size(); ++i) {
        QVariantMap map = meterConfigs.at(i).toMap();
        bool isEnabled = map.value("enabled").toBool();
        quint8 address = (quint8)map.value("address").toUInt();

        // 将其加入任务列表。
        // 精髓：如果前端没有勾选“启用”，isEnabled 为 false。
        // 我们直接把它存入名单，但 isAlive 置为 false。
        // 这样 run() 里面的大循环遇到它时，就会自动 continue 跳过它！
        m_meterTasks.append({i, address, isEnabled});
    }
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

    connect(&srcPort, &QSerialPort::errorOccurred, this, &CalibrationThread::onSourcePortError);
    connect(&meterPort, &QSerialPort::errorOccurred, this, &CalibrationThread::onMeterPortError);

    qDebug() << "串口打开成功!";

    if (!handshakeSource(srcPort)){
        srcPort.close();
        meterPort.close();
        qInfo() << "====== INIT_ERROR 串口已安全释放 ======";
        return;
    }

    // 🌟 提取动态名单
    QList<MeterTask> meters = m_meterTasks;
    int aliveCount = 0;

    // =========================================================
    // 阶段一：标准源输出 220V 5A 1.0PF
    // =========================================================
    qDebug("1. 下发 220V 5A 1.0PF 配置...");
    if (!sendSourceCmd(srcPort, m_cfgCmd1)) goto ABORT_PROCESS;

    qDebug("2. 启动标准源输出...");
    emit srcMessage("220V/5A/PF=1.0 等待源稳定...", "info");
    if (!sendSourceCmd(srcPort, m_startCmd, 6000)) goto ABORT_PROCESS;

    qInfo() << "正在全通道监测物理输出，验证三相配置...";
    if (!waitSourceStable(srcPort, 1.0f, 1000)) goto ABORT_PROCESS;

    emit srcMessage("220V/5A/PF=1.0", "success");
    if (!m_isRunning) goto ABORT_PROCESS;

    // =========================================================
    // 步骤 0：解除写保护
    // =========================================================
    qInfo() << ">>> 开始批量执行 [解除写保护]...";
    aliveCount = 0;
    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        emit meterStepStatusChanged(meter.uiIndex, Step_Unlock, State_Running);

        writeMeterReg(meterPort, meter.address, m_regWriteProtect, 1);

        if (waitMeterState(meterPort, meter.address, m_regWriteProtect, 1, 2000)) {
            emit meterStepStatusChanged(meter.uiIndex, Step_Unlock, State_Success);
            aliveCount++;
        } else {
            emit meterStepStatusChanged(meter.uiIndex, Step_Unlock, State_Failed);
            //emit meterMessage("解除写保护超时失败", "error", meter.uiIndex);
            meter.isAlive = false; // 淘汰
        }
    }
    if (aliveCount == 0) {
        emit showTopMessage("仪表全部失败，校准终止", "error");
        goto ABORT_PROCESS;
    }

    // =========================================================
    // 步骤 1：校准准备 (包含写 0 和写 1)
    // =========================================================
    qInfo() << ">>> 开始批量执行 [校准准备]...";
    aliveCount = 0;
    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        emit meterStepStatusChanged(meter.uiIndex, Step_Prepare, State_Running);

        // 先置空闲
        writeMeterReg(meterPort, meter.address, m_regState, 0);
        bool ok = waitMeterState(meterPort, meter.address, m_regState, 0, 2000);

        // 再写 1 准备
        if (ok) {
            writeMeterReg(meterPort, meter.address, m_regState, 1);
            ok = waitMeterState(meterPort, meter.address, m_regState, 2, 2000);
        }

        if (ok) {
            emit meterStepStatusChanged(meter.uiIndex, Step_Prepare, State_Success);
            aliveCount++;
        } else {
            emit meterStepStatusChanged(meter.uiIndex, Step_Prepare, State_Failed);
            //emit meterMessage("校准准备超时失败", "error", meter.uiIndex);
            meter.isAlive = false;
        }
    }
    if (aliveCount == 0) {
        emit showTopMessage("仪表全部失败，校准终止", "error");
        goto ABORT_PROCESS;
    }

    // =========================================================
    // 步骤 2：电压电流校准 (PF=1.0)
    // =========================================================
    qInfo() << ">>> 开始批量执行 [PF=1.0 校准]...";
    aliveCount = 0;
    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        emit meterStepStatusChanged(meter.uiIndex, Step_VI_10, State_Running);

        writeMeterReg(meterPort, meter.address, m_regState, 3);
        if (waitMeterState(meterPort, meter.address, m_regState, 4, 3000)) {
            emit meterStepStatusChanged(meter.uiIndex, Step_VI_10, State_Success);
            aliveCount++;
        } else {
            emit meterStepStatusChanged(meter.uiIndex, Step_VI_10, State_Failed);
            //emit meterMessage("1.0PF 校准参数计算失败", "error", meter.uiIndex);
            meter.isAlive = false;
        }
    }
    if (aliveCount == 0) {
        emit showTopMessage("仪表全部失败，校准终止", "error");
        goto ABORT_PROCESS;
    }
    if (!m_isRunning) goto ABORT_PROCESS;

    // =========================================================
    // 阶段三：标准源输出 220V 5A 0.5PF
    // =========================================================
    qDebug("7. 切换源至 0.5PF...");
    if (!sendSourceCmd(srcPort, m_cfgCmd2)) goto ABORT_PROCESS;

    qDebug("8. 启动标准源输出...");
    emit srcMessage("220V/5A/PF=0.5 等待源稳定...", "info");
    if (!sendSourceCmd(srcPort, m_startCmd, 6000)) goto ABORT_PROCESS;

    qInfo() << "正在全通道监测物理输出，验证三相全量配置(0.5PF)...";
    if (!waitSourceStable(srcPort, 0.5f, 1000)) goto ABORT_PROCESS;

    emit srcMessage("220V/5A/PF=0.5", "success");
    if (!m_isRunning) goto ABORT_PROCESS;

    // =========================================================
    // 步骤 3：电压电流校准 (PF=0.5)
    // =========================================================
    qInfo() << ">>> 开始批量执行 [PF=0.5 相位校准]...";
    aliveCount = 0;
    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        emit meterStepStatusChanged(meter.uiIndex, Step_VI_05, State_Running);

        writeMeterReg(meterPort, meter.address, m_regState, 5);
        if (waitMeterState(meterPort, meter.address, m_regState, 6, 3000)) {
            emit meterStepStatusChanged(meter.uiIndex, Step_VI_05, State_Success);
            aliveCount++;
        } else {
            emit meterStepStatusChanged(meter.uiIndex, Step_VI_05, State_Failed);
            //emit meterMessage("0.5PF 相位校准失败", "error", meter.uiIndex);
            meter.isAlive = false;
        }
    }
    if (aliveCount == 0) {
        emit showTopMessage("仪表全部失败，校准终止", "error");
        goto ABORT_PROCESS;
    }

    // =========================================================
    // 步骤 4：参数固化保存
    // 注：根据你之前的协议，计算完成即代表可保存，这里走个过场更新 UI 状态
    // =========================================================
    qInfo() << ">>> 正在更新 [参数固化保存] 状态...";
    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        emit meterStepStatusChanged(meter.uiIndex, Step_Save, State_Running);
        QThread::msleep(100); // 视觉缓冲
        emit meterStepStatusChanged(meter.uiIndex, Step_Save, State_Success);
    }

    // =========================================================
    // 步骤 5：等待复位 (💥 极度优雅的并发休眠优化)
    // =========================================================
    qInfo() << ">>> 开始批量下发 [复位指令]...";
    aliveCount = 0;

    // 动作A：给所有存活的表瞬间连发复位指令（不等待响应）
    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        emit meterStepStatusChanged(meter.uiIndex, Step_Reset, State_Running);
        writeMeterReg(meterPort, meter.address, m_regState, 7, false);
        QThread::msleep(500);
    }

    // 动作B：所有人一起共享这 1.5 秒的休眠时间！不需要每台表傻傻睡 1.5 秒！
    qInfo() << ">>> 所有表复位已下发，总线静默 1500ms 等待单片机重启...";
    QThread::msleep(2000);

    // 动作C：挨个点名收作业，看是不是都变回了 0
    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        if (waitMeterState(meterPort, meter.address, m_regState, 0, 1000)) {
            emit meterStepStatusChanged(meter.uiIndex, Step_Reset, State_Success);
            aliveCount++;
        } else {
            emit meterStepStatusChanged(meter.uiIndex, Step_Reset, State_Failed);
            //emit meterMessage("设备未能在复位后正常唤醒", "error", meter.uiIndex);
            meter.isAlive = false;
        }
    }

    if (aliveCount == 0) {
        emit showTopMessage("仪表全部失败，校准终止", "error");
        goto ABORT_PROCESS;
    }

    // 全部通关！
    if (m_isRunning) {
        qInfo() << "====== 淘汰制校准流程圆满结束，存活表数：" << aliveCount << " ======";
        emit showTopMessage(QString("校准完毕，成功校准 %1 台仪表").arg(aliveCount), "success");
        goto SUCCESS_EXIT;
    }

ABORT_PROCESS:
    qWarning() << ">>> 流程异常中断！正在向物理总线追发强停命令...";
    //emit calirResult("流程异常中断！", "error");

SUCCESS_EXIT:
    qInfo() << "正在停止标准源...";
    emit srcMessage("正在停止标准源...", "success");
    srcPort.write(m_stopCmd);
    srcPort.waitForBytesWritten(500);
    if (srcPort.waitForReadyRead(500)) {
        qInfo().noquote() << "[Rx 源强停确认]" << srcPort.readAll().toHex(' ').toUpper();
        emit srcMessage("已停止", "success");
    }
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

bool CalibrationThread::sendSourceCmd(QSerialPort &port, const QByteArray &cmdHex, int timeoutMs)
{
    // 🌟 1. 打印发给标准源的报文
    qInfo().noquote() << "[Tx 标准源]" << cmdHex.toHex(' ').toUpper();
    port.write(cmdHex);

    if (!checkSourceResponse(port,timeoutMs)) return false;

    QByteArray rx = port.readAll();

    // 🌟 2. 打印标准源返回的报文
    qInfo().noquote() << "[Rx 标准源]" << rx.toHex(' ').toUpper();

    if (rx == m_srcNack) {
        qCritical() << "❌ [拒绝执行] 标准源返回否定全帧(NACK)！指令被拒收。";
        emit srcMessage("标准源配置被拒(否定应答)", "error");
        emit calirResult("标准源配置被拒(否定应答)", "error");
        m_isRunning = false;
        return false;
    }
    if (rx == m_srcAck) {
        return true; // 完美的 8 字节全帧匹配，放行
    }

    emit srcMessage("标准源应答特征不匹配", "error");
    emit calirResult("标准源应答特征不匹配", "error");
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
        QThread::msleep(500);
        elapsed += 500;
    }

    qDebug()<<QString("等待状态机寄存器变更为 %2 失败").arg(targetState);
    return false;
}

bool CalibrationThread::handshakeSource(QSerialPort &port)
{
    qInfo() << ">>> 开启标准源物理握手探测（1秒一发，最多等待10秒）...";
    emit srcMessage("通讯建立中", "info");

    // 清空一下开机前可能因为波特率不稳定产生的无用乱码脏数据
    port.readAll();

    for (int i = 1; i <= 10; ++i) {
        if (!m_isRunning) return false;

        qInfo() << QString(">>> [第 %1 次尝试] 正在发送握手命令...").arg(i);
        port.write(m_handshakeCmd);

        if (port.waitForReadyRead(1000)) {

            QByteArray rx = port.readAll();
            qInfo().noquote() << QString("[Rx 第 %1 次应答]").arg(i) << rx.toHex(' ').toUpper();

            // 精准全帧匹配
            if (rx == m_srcAck) {
                qInfo() << ">>> 标准源握手成功！设备已在线。";
                emit srcMessage("通讯成功,设备已连接", "info");
                return true; // 只要成功一次，立刻跳出整个函数，开始后面的流程
            }
            if (rx == m_srcNack) {
                qCritical() << "标准源返回否定全帧(NACK)！当前状态拒绝通信。";
                // 收到 NACK 说明硬件在线只是拒绝，可以根据现场情况选择是直接退出还是继续重试
                // 如果是开机不稳，建议继续重试
            }
            // 🌟 关键修补：收到错帧或 NACK（由于上面很快就返回了，没耗够 1 秒）
            // 为了防止狂发命令，这里补一个延时，稳住 1 秒 1 发的节奏
            if (i < 10) {
                QThread::msleep(1000);
            }
        } else {
            qWarning() << QString(">>> [第 %1 次尝试] 串口响应超时，设备未就绪。").arg(i);
        }
    }

    // 10次机会全用完了
    qCritical() << "致命错误：连续 10 秒钟标准源均无合法应答，终止全流程！";
    emit srcMessage("请确认设备是否开机或接线正确", "error");
    emit showTopMessage("标准源通讯失败，请确认设备是否开机或接线正确", "error");
    m_isRunning = false;
    return false;
}

bool CalibrationThread::checkSourceResponse(QSerialPort &port, int timeoutMs) {
    if (!port.waitForBytesWritten(200) || !port.waitForReadyRead(timeoutMs)) {
        QSerialPort::SerialPortError err = port.error();
        if (err == QSerialPort::NoError) err = QSerialPort::TimeoutError;

        QString errStr = translateSerialError(err);
        emit srcMessage("标准源中断: " + errStr, "error");
        emit calirResult("标准源中断: " + errStr, "error");
        m_isRunning = false;
        return false;
    }
    while (port.waitForReadyRead(20)) {
    }
    return true;
}

bool CalibrationThread::checkMeterResponse(QSerialPort &port, int meterIndex) {
    if (!port.waitForBytesWritten(200) || !port.waitForReadyRead(200)) {
        QSerialPort::SerialPortError err = port.error();
        if (err == QSerialPort::NoError) err = QSerialPort::TimeoutError;

        if (err != QSerialPort::TimeoutError) {
            emit showTopMessage("总线物理故障: " + translateSerialError(err), "error");
            m_isRunning = false;
        } else {
            qWarning() << "[Thread] 仪表" << meterIndex << "请求超时";
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
                qCritical() << "[拒绝读取] 标准源返回否定全帧，输出未就绪！";
                emit calirResult("[拒绝读取] 标准源返回否定全帧，输出未就绪！", "error");

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
        emit srcMessage("致命超时：标准源全量参数未能同时达标稳住！", "error");
        emit calirResult("致命超时：标准源全量参数未能同时达标稳住！", "error");
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

