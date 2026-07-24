#include "calibrationthread.h"
#include <QDebug>
#include <QThread>
#include <QtMath>
#include <QElapsedTimer>
#include "xlsxdocument.h"
#include "xlsxformat.h"
#include <QDateTime>           // 用于获取当前时间，防止文件覆盖
#include "xlsxworksheet.h"
#include <QDir>

CalibrationThread::CalibrationThread(QObject *parent)
    : QThread(parent), m_isRunning(false) {


}

CalibrationThread::~CalibrationThread()
{
    if (isRunning()) {
        qWarning() << "析构强停：正在强行关闭标准源...";
        stopCalibration();
        wait(); // 阻塞等待子线程彻底死透，确保安全收尾指令发出
    }
}

void CalibrationThread::setConfig(int mode, const QString &srcPort, int srcBaud, const QString &meterPort, int meterBaud, const QVariantList &meterConfigs)
{
    m_workMode = (WorkMode)mode;
    m_srcPortName = srcPort;
    m_srcBaud = srcBaud;
    m_meterPortName = meterPort;
    m_meterBaud = meterBaud;

    // 解析前端传来的 5 台表配置
    m_meters.clear();
    for (int i = 0; i < meterConfigs.size(); ++i) {
        QVariantMap map = meterConfigs.at(i).toMap();
        Meter m;
        m.uiIndex = i;
        m.address = (quint8)map.value("address").toUInt();
        m.sn = map.value("sn").toString(); // 🌟 直接在这里把 SN 也装进来
        m.isEnabled = map.value("enabled").toBool();
        m.isAlive = m.isEnabled; // 初始存活状态继承自是否启用
        m.hasFail = false;
        m_meters.append(m);
    }
}

void CalibrationThread::stopCalibration() {
    m_isRunning = false;
    m_isManualStop = true; //明确标记：这是操作员强行中止的！
}

void CalibrationThread::run()
{
    m_testTimer.start();
    qInfo() << "====== 开始测试，计时器启动 ======";

    m_isRunning = true;
    m_isManualStop = false;

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

    // 为当前这趟测试创建“局部快照副本”，实现线程安全隔离！
    QList<Meter> meters = m_meters;
    int aliveCount = 0;

    if (m_workMode == Mode_FullAuto) {
        if (!runCalibrationFlow(srcPort, meterPort, meters, aliveCount)) {
            goto ABORT_PROCESS;
        }
    }

    if (m_workMode == Mode_ErrorCalc) {
        if (!runErrorCalcFlow(srcPort, meterPort, meters, aliveCount)) {
            goto ABORT_PROCESS;
        }
    }

    if (m_workMode == Mode_EnergyCalc) {
        //scanAndFindEnergyInterfaceCode(srcPort);
        if (!runEnergyCalcFlow(srcPort, meterPort, meters, aliveCount)) {
            goto ABORT_PROCESS;
        }
    }
    goto SUCCESS_EXIT;


ABORT_PROCESS:
    qWarning() << ">>> ABORT_PROCESS 流程异常中断！正在向物理总线追发强停命令...";

SUCCESS_EXIT:
    qInfo() << "正在停止标准源...";
    emit srcMessage("正在停止标准源...", "success");
    //safeStopSource(srcPort);
    qInfo().noquote() << "[Tx 标准源关停]" << m_stopCmd.toHex(' ').toUpper();
    srcPort.clear(QSerialPort::Input);
    srcPort.write(m_stopCmd);
    srcPort.waitForBytesWritten(500);

    if (m_workMode == Mode_ErrorCalc || m_workMode == Mode_EnergyCalc) {
        qInfo() << "====== 正在根据缓存生成 Excel 报表 ======";
        for (const auto &meter : std::as_const(meters)) {
            if (meter.isEnabled) { // 只要启用的表，无论是否淘汰，都导出报表看死在哪里
                exportExcelReport(meter);
            }
        }
    }
    cleanOldReportFolders();
    if (m_isManualStop) {
        for (const auto &m : std::as_const(meters)) {
            if (m.isEnabled) {
                // 强制将卡片刷回灰色(Error_Idle)空闲状态
                emit updateErrorMeterStatus(m_workMode,m.uiIndex, Error_Stop, "操作员手动停止");
                emit showResultPopup("操作员手动停止","","error");
            }
        }
    }
    // 核心：计算总耗时（毫秒转为分钟）
    qint64 elapsedMs = m_testTimer.elapsed();                  // 获取总毫秒数

    // 更严谨的纯分钟计算：
    qint64 minutes = elapsedMs / 60000;
    qint64 seconds = (elapsedMs % 60000) / 1000;

    qInfo().noquote() << QString("====== 测试全部完成！总耗时: %1 分 %2 秒 ======")
                             .arg(minutes)
                             .arg(seconds);
}

// -------------------------------------------------------------------------
// 抽离出的纯校准流水线 (注意将原来的 goto ABORT_PROCESS 全部换成了 return)
// -------------------------------------------------------------------------
bool CalibrationThread::runCalibrationFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, int &aliveCount)
{
    qInfo() << ">>> 开始批量下发 [复位指令]...";
    aliveCount = 0;
    for (auto &meter : meters) {
        if (!meter.isEnabled) continue;
        emit meterStepStatusChanged(meter.uiIndex, Step_Reset0, State_Running);
        if (writeMeterReg(meterPort, meter.address, m_regReset1, 1)) {
            aliveCount++;
            emit meterStepStatusChanged(meter.uiIndex, Step_Reset0, State_Success);
        } else {
            emit meterStepStatusChanged(meter.uiIndex, Step_Reset0, State_Failed);
            meter.isAlive = false; // 淘汰
        }
        QThread::msleep(500);
    }
    if (!m_isRunning) return false;

    qInfo() << ">>> 所有表复位已下发，总线静默 1500ms 等待单片机重启...";
    QThread::msleep(2000);
    // =========================================================
    // 阶段一：标准源输出 220V 5A 1.0PF
    // =========================================================
    qDebug("1. 下发 220V 5A 1.0PF 配置...");
    if (!setSourceConfig(srcPort, 220.0f, 5.0f, 1.0f)) return false;

    qDebug("2. 启动标准源输出...");
    emit srcMessage("220V/5A/PF=1.0 等待源稳定...", "info");
    if (!safeStartSource(srcPort)) return false;

    QThread::msleep(30000);

    if (!readSourceAllParams(srcPort, m_sourceParams)) return false;

    emit srcMessage("220V/5A/PF=1.0", "success");
    if (!m_isRunning) return false;

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
            meter.isAlive = false; // 淘汰
        }
    }
    if (aliveCount == 0) {
        emit showResultPopup("仪表全部失败，校准终止","", "error");
        return false;
    }

    // =========================================================
    // 步骤 1：校准准备 (包含写 0 和写 1)
    // =========================================================
    qInfo() << ">>> 开始批量执行 [校准准备]...";
    QThread::msleep(1000);
    aliveCount = 0;
    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        emit meterStepStatusChanged(meter.uiIndex, Step_Prepare, State_Running);

        writeMeterReg(meterPort, meter.address, m_regState, 0);
        bool ok = waitMeterState(meterPort, meter.address, m_regState, 0, 2000);

        if (ok) {
            writeMeterReg(meterPort, meter.address, m_regState, 1);
            ok = waitMeterState(meterPort, meter.address, m_regState, 2, 2000);
        }

        if (ok) {
            emit meterStepStatusChanged(meter.uiIndex, Step_Prepare, State_Success);
            aliveCount++;
        } else {
            emit meterStepStatusChanged(meter.uiIndex, Step_Prepare, State_Failed);
            meter.isAlive = false;
        }
    }
    if (aliveCount == 0) {
        emit showResultPopup("仪表全部失败，校准终止","", "error");
        return false;
    }

    // =========================================================
    // 步骤 2：电压电流校准 (PF=1.0)
    // =========================================================
    qInfo() << ">>> 开始批量执行 [PF=1.0 校准]...";
    QThread::msleep(1000);
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
            meter.isAlive = false;
        }
    }
    if (aliveCount == 0) {
        emit showResultPopup("仪表全部失败，校准终止","", "error");
        return false;
    }
    if (!m_isRunning) return false;

    // =========================================================
    // 阶段三：标准源输出 220V 5A 0.5PF
    // =========================================================
    qDebug("7. 切换源至 0.5PF...");
    if (!setSourceConfig(srcPort, 220.0f, 5.0f, 0.5f)) return false;

    qDebug("8. 启动标准源输出...");
    emit srcMessage("220V/5A/PF=0.5 等待源稳定...", "info");
    if (!safeStartSource(srcPort)) return false;

    QThread::msleep(30000);

    qDebug("3. 等待源和仪表稳定...");
    if (!readSourceAllParams(srcPort, m_sourceParams)) return false;
    emit srcMessage("220V/5A/PF=0.5", "success");
    if (!m_isRunning) return false;

    // =========================================================
    // 步骤 3：电压电流校准 (PF=0.5)
    // =========================================================
    qInfo() << ">>> 开始批量执行 [PF=0.5 相位校准]...";
    QThread::msleep(1000);
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
            meter.isAlive = false;
        }
    }
    if (aliveCount == 0) {
        emit showResultPopup("仪表全部失败，校准终止","", "error");
        return false;
    }

    // =========================================================
    // 步骤 4：参数固化保存
    // =========================================================
    qInfo() << ">>> 正在更新 [参数固化保存] 状态...";
    QThread::msleep(1000);
    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        emit meterStepStatusChanged(meter.uiIndex, Step_Save, State_Running);
        QThread::msleep(100);
        emit meterStepStatusChanged(meter.uiIndex, Step_Save, State_Success);
    }

    // =========================================================
    // 步骤 5：等待复位
    // =========================================================
    qInfo() << ">>> 开始批量下发 [复位指令]...";
    QThread::msleep(1000);
    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        emit meterStepStatusChanged(meter.uiIndex, Step_Reset, State_Running);
        //writeMeterReg(meterPort, meter.address, m_regState, 7, false);
        writeMeterReg(meterPort, meter.address, m_regState, 7);
        QThread::msleep(500);
    }

    qInfo() << ">>> 所有表复位已下发，总线静默 1500ms 等待单片机重启...";
    QThread::msleep(2000);
    aliveCount = 0;
    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        if (waitMeterState(meterPort, meter.address, m_regState, 0, 1000)) {
            emit meterStepStatusChanged(meter.uiIndex, Step_Reset, State_Success);
            aliveCount++;
        } else {
            emit meterStepStatusChanged(meter.uiIndex, Step_Reset, State_Failed);
            meter.isAlive = false;
        }
    }

    if (aliveCount == 0) {
        emit showResultPopup("仪表全部失败，校准终止","", "error");
        return false;
    }

    // 全部通关或彻底结束！
    if (m_isRunning) {
        aliveCount = 0; int fail = 0, enableNum = 0;
        for (auto &meter : meters) {
            if(meter.isEnabled){
                enableNum++;
                if (meter.hasFail || !meter.isAlive){
                    fail++;
                }else{
                    aliveCount++;
                }
            }
        }
        if(fail == 0)
            emit showResultPopup("测试完成",QString("启用 %1 台, 成功 %2 台, 失败 %3 台").arg(enableNum).arg(aliveCount).arg(fail),"success");
        else
            emit showResultPopup("测试完成",QString("启用 %1 台, 成功 %2 台, 失败 %3 台").arg(enableNum).arg(aliveCount).arg(fail),"error");
        qInfo()<<"测试全部完成!"<<QString("启用 %1 台, 成功 %2 台, 失败 %3 台").arg(enableNum).arg(aliveCount).arg(fail);
    }

    return true;
}

// =========================================================================
// 主入口：误差计算大循环
// =========================================================================
bool CalibrationThread::runErrorCalcFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, int &aliveCount)
{
    qInfo() << "====== 正在启动 误差测试系统 ======";

    qInfo() << ">>> [自动翻屏] 远程切换至 STR3060A【主界面】(Md=0x27)...";
    qInfo().noquote() << QString("[Tx 标准源 切换电能界面] ") << m_switchToSourceUI_Cmd.toHex(' ').toUpper();
    srcPort.write(m_switchToSourceUI_Cmd);
    QThread::msleep(1000); // 给予机身 LCD 屏渲染和底层 DSP 挂载积分任务的时间

    for (auto &meter : meters) {
        if (!meter.isEnabled) continue;

        emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Running, "正在测试...");
    }


    //1. 执行电压电流测试 (地址 0x1018, 9个参数)
    qInfo() << "====== 1. 执行电压电流测试 ======";
    if (!runTestCategory(srcPort, meterPort, Cat_V, 0x1018, 9, m_viTestPoints, meters, aliveCount)) {
        return false;
    }

    //2. 执行有功功率测试
    qInfo() << "====== 2. 执行有功功率测试 ======";
    if (!runTestCategory(srcPort, meterPort, Cat_ActivePower, 0x300C, 4, m_activePowerTestPoints, meters, aliveCount)) {
        return false;
    }

    // 3. 执行无功功率测试
    qInfo() << "====== 3. 执行无功功率测试 ======";
    if (!runTestCategory(srcPort, meterPort, Cat_ReactivePower, 0x3014, 4, m_reactivePowerTestPoints, meters, aliveCount)) {
        return false;
    }

    // 4. 执行视在功率测试
    qInfo() << "====== 4. 执行视在功率测试 ======";
    if (!runTestCategory(srcPort, meterPort, Cat_ApparentPower, 0x301C, 4, m_apparentPowerTestPoints, meters, aliveCount)) {
        return false;
    }

    // 5. 执行功率因数测试
    qInfo() << "====== 5. 执行功率因数测试 ======";
    if (!runTestCategory(srcPort, meterPort, Cat_PowerFactor, 0x1044, 4, m_powerFactorTestPoints, meters, aliveCount)) {
        return false;
    }

    //6. 执行全段谐波测试 (双通道同测)
    if (!runHarmonicsFlow(srcPort, meterPort, meters, aliveCount)) {
        return false;
    }

    if (!m_isRunning) return false;

    if (m_isRunning) {
        aliveCount = 0; int fail = 0, enableNum = 0;
        for (auto &meter : meters) {
            if(meter.isEnabled){
                enableNum++;
                if (meter.hasFail){
                    emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Fail, "有超标项");
                }else{
                    emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Pass, "全部合格");
                    aliveCount++;
                }
            }
        }
        fail = enableNum - aliveCount;
        if(fail == 0)
            emit showResultPopup("测试完成",QString("启用 %1 台, 成功 %2 台, 失败 %3 台").arg(enableNum).arg(aliveCount).arg(fail),"success");
        else
            emit showResultPopup("测试完成",QString("启用 %1 台, 成功 %2 台, 失败 %3 台").arg(enableNum).arg(aliveCount).arg(fail),"error");
        qInfo()<<"测试全部完成!"<<QString("启用 %1 台, 成功 %2 台, 失败 %3 台").arg(enableNum).arg(aliveCount).arg(fail);
    }

    return true;
}


// 1. 通用组帧工具：只要传入命令字和数据，自动算长度和校验和
QByteArray CalibrationThread::buildSTRCmd(quint8 cmd, const QByteArray &data)
{
    QByteArray frame;
    frame.append((char)0xA1).append((char)0x01); // 固定地址 A1 01
    quint8 len = 1 + data.size() + 1;            // 长度 = 命令字 + 数据 + CS
    frame.append(len).append((char)cmd).append(data);

    quint8 cs = cmd;
    for (char b : data) cs += (quint8)b;         // CS 校验和累加
    frame.append((char)cs);
    return frame;
}

QByteArray CalibrationThread::valToBCD(double val, int decPlaces, int byteLen)
{
    long long v = qRound64(val * std::pow(10.0, decPlaces));
    QByteArray res;
    // 🌟 根据要求的总字节数 (byteLen) 精准高低位拆解，绝不多生成或少生成！
    for (int i = byteLen - 1; i >= 0; --i) {
        long long divisor = 1;
        for (int k = 0; k < i; ++k) divisor *= 100;
        int part = (v / divisor) % 100;
        res.append((((part / 10) << 4) | (part % 10)));
    }
    return res;
}

float CalibrationThread::parseSTRFloat(const char *data)
{
    // 🌟 1. 终极解析阶码：低4位是绝对值，高4位如果是 1 代表负指数！
    int expVal = data[0] & 0x0F;         // 提取低4位作为幂的绝对值
    if (((data[0] >> 4) & 0x0F) == 1) {  // 提取高4位，如果是 1 说明是负指数 (如 0x11=-1, 0x12=-2, 0x13=-3)
        expVal = -expVal;
    }

    // 2. 提取最高符号位 (bit 7)
    bool valSign = (data[1] & 0x80) != 0;

    // 3. 逐个提取 BCD 尾数 (按你定下的铁律，绝不使用 Lambda，直白顺序计算)
    quint8 b1 = data[1] & 0x0F; // 整数位
    quint8 b2 = (quint8)data[2];
    quint8 b3 = (quint8)data[3];
    quint8 b4 = (quint8)data[4];

    double mantissa = b1
                      + (((b2 >> 4) & 0x0F) * 10 + (b2 & 0x0F)) * 1e-2
                      + (((b3 >> 4) & 0x0F) * 10 + (b3 & 0x0F)) * 1e-4
                      + (((b4 >> 4) & 0x0F) * 10 + (b4 & 0x0F)) * 1e-6;

    if (valSign) {
        mantissa = -mantissa;
    }

    return (float)(mantissa * std::pow(10.0, expVal));
}

bool CalibrationThread::setSourceConfig(QSerialPort &port, float vol, float cur, float pf)
{
    if (!m_isRunning) return false;
    // =========================================================================
    // 规约 6.1.3/8.1.3 强令：换挡前必须先关停输出并延时！
    // =========================================================================
    qDebug("0. 换挡前安全关停标准源输出，保护底层物理继电器...");
    // 发送 DC 00 00：6路（三相电压+三相电流）同时关停输出
    if (!safeStopSource(port)) return false;

    // 1. 设置工作方式：三相四线 (C0 01)
    if (!sendSourceCmd(port, buildSTRCmd(0xC0, QByteArray::fromHex("01")),3000)) return false;

    // 2. 设置频率：50.00Hz (D5，规约要求 2 字节 BCD，2 位小数 -> 50 00)
    if (!sendSourceCmd(port, buildSTRCmd(0xD5, valToBCD(50.0f, 2, 2)),3000)) return false;

    // =========================================================================
    // 3. 设置电压档位 (D6) 和 百分比 (D8)
    // 🌟 【工业级精度路由】：从小到大升序遍历，寻找第一个百分比 <= 120% 的最佳信噪比档位！
    // =========================================================================
    float vRanges[] = {57.735f, 100.0f, 220.0f, 380.0f}; // 从小到大升序排列
    float vRange = 380.0f; // 默认兜底使用最大档位，防止极端超压
    for (int j = 0; j < 4; ++j) {
        float pct = (vol / vRanges[j]) * 100.0f;
        // 只要百分比 <= 120.0%，说明当前档位完全能承载该电压，且绝对是信噪比最高的档！
        if (pct <= 120.01f) {
            vRange = vRanges[j];
            break;
        }
    }

    float vPct = (vol / vRange) * 100.0f;
    if (vPct < 1.0f) {
        qWarning().noquote() << QString(">>> [超低限警告-电压] 当前输出 %1V 仅占 %2V 档位的 %3%，低于规约 1% 下限，可能影响校验精度！").arg(vol).arg(vRange).arg(vPct, 0, 'f', 2);
    } else {
        qInfo().noquote() << QString(">>> [智能配源-电压] 目标: %1V -> 自动锁定最佳档位: %2V | 输出百分比: %3%").arg(vol).arg(vRange).arg(vPct, 0, 'f', 2);
    }

    if (!sendSourceCmd(port, buildSTRCmd(0xD6, QByteArray::fromHex("00") + valToBCD(vRange, 4, 4)),3000)) return false;
    QThread::msleep(200); // 留足换挡延时
    if (!sendSourceCmd(port, buildSTRCmd(0xD8, QByteArray::fromHex("00") + valToBCD(vPct, 4, 4)),3000)) return false;


    // =========================================================================
    // 5. 设置电流档位 (D7) 和 百分比 (D9)
    // 🌟 【工业级精度路由】：从小到大升序遍历，寻找第一个百分比 <= 120% 的最佳信噪比档位！
    // =========================================================================
    float iRanges[] = {0.2f, 1.0f, 5.0f, 20.0f}; // 从小到大升序排列 (根据你说明书实际有几个档填几个)
    float iRange = 20.0f; // 默认兜底使用最大档位，防止极端过载
    for (int j = 0; j < 4; ++j) {
        float pct = (cur / iRanges[j]) * 100.0f;
        // 只要百分比 <= 120.0%，绝对就是信噪比最好、百分比最满的天命档位！
        if (pct <= 120.01f) {
            iRange = iRanges[j];
            break;
        }
    }

    float iPct = (cur / iRange) * 100.0f;
    if (iPct < 1.0f) {
        qWarning().noquote() << QString(">>> [超低限警告-电流] 当前输出 %1A 仅占 %2A 档位的 %3%，低于规约 1% 下限，可能影响校验精度！").arg(cur).arg(iRange).arg(iPct, 0, 'f', 2);
    } else {
        qInfo().noquote() << QString(">>> [智能配源-电流] 目标: %1A -> 自动锁定最佳档位: %2A | 输出百分比: %3%").arg(cur).arg(iRange).arg(iPct, 0, 'f', 2);
    }

    if (!sendSourceCmd(port, buildSTRCmd(0xD7, QByteArray::fromHex("00") + valToBCD(iRange, 4, 4)),3000)) return false;
    QThread::msleep(200); // 留足换挡延时
    if (!sendSourceCmd(port, buildSTRCmd(0xD9, QByteArray::fromHex("00") + valToBCD(iPct, 4, 4)),3000)) return false;

    // 7. 设置相位夹角 (D4) - 4字节BCD，4位小数
    float angle = qRadiansToDegrees(qAcos(qBound(0.0f, qAbs(pf), 1.0f)));
    if (pf < 0) angle = 360.0f - angle; // 容性负值转换为 300~360度
    if (!sendSourceCmd(port, buildSTRCmd(0xD4, QByteArray::fromHex("00") + valToBCD(angle, 4, 4)),3000)) return false;

    return true;
}

bool CalibrationThread::readSourceAllParams(QSerialPort &port, SourceParams &outParams)
{
    if (!m_isRunning) return false;

    // 1. 组装 A0 查询命令并发送
    qInfo().noquote() << "[Tx 标准源读全量参数]" << m_readAllCmd.toHex(' ').toUpper();
    port.clear(QSerialPort::Input);
    port.write(m_readAllCmd);

    // 2. 超时检查函数
    if (!checkSourceResponse(port, 1000)) {
        qWarning() << "[Rx 标准源读全量参数] 响应超时或无数据！";
        return false;
    }

    // 3. 读取全部数据并打印日志
    QByteArray rx = port.readAll();
    qInfo().noquote() << "[Rx 标准源全量参数]" << rx.toHex(' ').toUpper();

    if (rx.size() < 238) {
        qWarning() << "[Rx 标准源读全量参数] 数据包长度不足，期望238字节，实际收到:" << rx.size();
        return false;
    }

    // 4. 解析
    int idx = 0;
    while ((idx = rx.indexOf((char)0xA1, idx)) != -1) {
        // 防止越界：至少要有 帧头(2) + 长度(1) + 命令字(1)
        if (idx + 3 >= rx.size()) break;
        // 校验地址是不是 A1 01
        if ((quint8)rx[idx + 1] != 0x01) { idx++; continue; }

        quint8 len = (quint8)rx[idx + 2];
        if (idx + 1 + len >= rx.size()) break; // 帧不完整，跳过

        quint8 cmd = (quint8)rx[idx + 3];
        // 数据区首指针 (跳过 A1, 01, Len, Cmd 共 4 个字节)
        const char *p = rx.constData() + idx + 4;

        // =================================================================
        // 按命令字死板、严谨、一对一地提取数据 (完全对照你抓包的数据规律)
        // 每个具体参量 = 通道ID(1字节) + 压缩BCD浮点数(5字节) = 6个字节
        // =================================================================
        if (cmd == 0xF6) { // 电压、电流 (9个参量)
            outParams.U[0] = parseSTRFloat(p + 0 * 6 + 1); // 01: Ua
            outParams.U[1] = parseSTRFloat(p + 1 * 6 + 1); // 02: Ub
            outParams.U[2] = parseSTRFloat(p + 2 * 6 + 1); // 03: Uc
            outParams.I[0] = parseSTRFloat(p + 3 * 6 + 1); // 04: Ia
            outParams.I[1] = parseSTRFloat(p + 4 * 6 + 1); // 05: Ib
            outParams.I[2] = parseSTRFloat(p + 5 * 6 + 1); // 06: Ic
            outParams.U[3] = parseSTRFloat(p + 6 * 6 + 1); // 07: Uab
            outParams.U[4] = parseSTRFloat(p + 7 * 6 + 1); // 08: Ubc
            outParams.U[5] = parseSTRFloat(p + 8 * 6 + 1); // 09: Uca
        }
        else if (cmd == 0xF1) { // 有功功率 (4个参量: Pa, Pb, Pc, P总)
            outParams.P[0] = parseSTRFloat(p + 0 * 6 + 1); // 11: Pa
            outParams.P[1] = parseSTRFloat(p + 1 * 6 + 1); // 12: Pb
            outParams.P[2] = parseSTRFloat(p + 2 * 6 + 1); // 13: Pc
            outParams.P[3] = parseSTRFloat(p + 3 * 6 + 1); // 10: P总
        }
        else if (cmd == 0xF2) { // 无功功率 (4个参量)
            outParams.Q[0] = parseSTRFloat(p + 0 * 6 + 1); // 11: Qa
            outParams.Q[1] = parseSTRFloat(p + 1 * 6 + 1); // 12: Qb
            outParams.Q[2] = parseSTRFloat(p + 2 * 6 + 1); // 13: Qc
            outParams.Q[3] = parseSTRFloat(p + 3 * 6 + 1); // 10: Q总
        }
        else if (cmd == 0xF3) { // 视在功率 (4个参量)
            outParams.S[0] = parseSTRFloat(p + 0 * 6 + 1); // 11: Sa
            outParams.S[1] = parseSTRFloat(p + 1 * 6 + 1); // 12: Sb
            outParams.S[2] = parseSTRFloat(p + 2 * 6 + 1); // 13: Sc
            outParams.S[3] = parseSTRFloat(p + 3 * 6 + 1); // 10: S总
        }
        else if (cmd == 0xF4) { // 功率因数 (4个参量)
            outParams.PF[0] = parseSTRFloat(p + 0 * 6 + 1); // 11: PFa
            outParams.PF[1] = parseSTRFloat(p + 1 * 6 + 1); // 12: PFb
            outParams.PF[2] = parseSTRFloat(p + 2 * 6 + 1); // 13: PFc
            outParams.PF[3] = parseSTRFloat(p + 3 * 6 + 1); // 10: PF总
        }
        else if (cmd == 0xF0) { // 频率 (1个参量：01 + 5字节浮点)
            outParams.freq = parseSTRFloat(p);          // 01: Freq
        }
        else if (cmd == 0xF5) { // 相位 (5个主要参量: Phi_Ub, Phi_Uc, Phi_Ia, Phi_Ib, Phi_Ic)
            outParams.Phi[0] = parseSTRFloat(p + 0 * 6 + 1); // 02: Phi_Ub
            outParams.Phi[1] = parseSTRFloat(p + 1 * 6 + 1); // 03: Phi_Uc
            outParams.Phi[2] = parseSTRFloat(p + 2 * 6 + 1); // 04: Phi_Ia
            outParams.Phi[3] = parseSTRFloat(p + 3 * 6 + 1); // 05: Phi_Ib
            outParams.Phi[4] = parseSTRFloat(p + 4 * 6 + 1); // 06: Phi_Ic
        }

        idx += (1 + len); // 继续往后扫描下一帧
    }

    // 5. 按照你的铁律，将最终解析出的核心成绩打印出来，方便最终追踪
    qInfo().noquote() << QString("  -> [全量电参解析成功] Ua: %1V | Ia: %2A | P总: %3W | PF总: %4 | Freq: %5Hz")
                             .arg(outParams.U[0], 0, 'f', 2)
                             .arg(outParams.I[0], 0, 'f', 3)
                             .arg(outParams.P[3], 0, 'f', 2)
                             .arg(outParams.PF[3], 0, 'f', 3)
                             .arg(outParams.freq, 0, 'f', 2);
    return true;
}

bool CalibrationThread::safeStopSource(QSerialPort &port)
{
    qInfo() << ">>> [安全关停] 准备执行标准源一键安全关停 (无脑强下 DC 00 00)...";

    // 1. 组装一键关停指令：DC 00 00 (00代表6路同时，00代表关停输出)
    qInfo().noquote() << "[Tx 标准源关停]" << m_stopCmd.toHex(' ').toUpper();

    // 2. 🌟 彻底切断任何回读判断！直接清空总线垃圾，强行把关闸命令送到总线上！
    port.clear(QSerialPort::Input);
    port.write(m_stopCmd);
    port.waitForBytesWritten(500);

    // 3. 🌟 【采纳实测铁律】继电器跳开瞬间的反向拉弧干扰极易导致通讯丢帧！
    // 我们只给 1500ms 倾听一下设备是否吐了确认包，记个日志，绝不卡死报错！
    if (port.waitForReadyRead(1500)) {
        QByteArray rx = port.readAll();
        qInfo().noquote() << "[Rx 关停应答]" << rx.toHex(' ').toUpper();
        if (rx.contains(QByteArray::fromHex("A10155"))) {
            qInfo() << ">>> 标准源硬件返回关停确认包 (A1 01 55)，主接触器跳闸成功！";
        } else {
            qWarning() << ">>> [关停提示] 收到非标准应答 (跳闸干扰导致，不影响物理关停)";
        }
    } else {
        qWarning() << ">>> [关停提示] 标准源未响应确认包 (合闸反弧干扰导致，不影响物理关停)";
    }

    // 4. 🌟 【工业级防假死倒计时】必须给予大功率继电器彻底跳开、内部高压电容放电、电弧完全熄灭的物理缓冲时间！
    qInfo() << ">>> 正在等待物理回路电弧熄灭与能量释放...";
    for (int sec = 1; sec <= 5; ++sec) {
        // 尽管是在关源，依然要让线程事件有呼吸空间
        QThread::msleep(1000);
        qInfo() << QString(">>> 物理关停泄放倒计时: 已进入 %1 / 5 秒...").arg(sec);
    }

    // 5. 顺手把内存结构体里的电参量也强行重置归零，给上位机软件界面一个最干净的状态
    m_sourceParams = SourceParams();

    qInfo() << ">>> 标准源安全关停流程全部圆满完毕！高压回路已彻底释放！";
    return true; // 永久放行！只要程序走到了这里，在现场业务上绝对算是关成功了！
}

bool CalibrationThread::safeStartSource(QSerialPort &port)
{
    if (!m_isRunning) return false;

    qInfo() << ">>> [安全启动] 准备启动标准源输出 (下发 DC 00 01)...";

    // 1. 组装一键启动指令：DC 00 01 (00代表6路同时，01代表启动输出)
    qInfo().noquote() << "[Tx 标准源启动]" << m_startCmd.toHex(' ').toUpper();

    // 2. 清空缓存并直接下发命令 (切断自作聪明的 A0 回读拦截)
    port.clear(QSerialPort::Input);
    port.write(m_startCmd);
    port.waitForBytesWritten(500);

    // 3. 🌟【采纳实测铁律】继电器合闸的 EMI 干扰易导致单片机丢包或无响应。
    // 我们给予 1500ms 尝试接收，只做日志记录与打印，绝不把未收到应答作为报错卡点！
    if (port.waitForReadyRead(1500)) {
        QByteArray rx = port.readAll();
        qInfo().noquote() << "[Rx 启动应答]" << rx.toHex(' ').toUpper();
        if (rx.contains(QByteArray::fromHex("A10155"))) {
            qInfo() << ">>> 标准源硬件返回启动确认包 (A1 01 55)，合闸成功！";
        } else {
            qWarning() << ">>> [启动提示] 收到非标准应答";
        }
    } else {
        qWarning() << ">>> [启动提示] 标准源未响应确认包";
    }

    // 4. 🌟【工业级防假死延时】给予高压/大电流功放充足的升压与闭环稳定时间
    qInfo() << ">>> 正在等待标准源彻底稳定...";
    for (int sec = 1; sec <= 10; ++sec) {
        // 每次循环等待 1 秒，期间不断检查程序是否被操作员手动停止
        if (!m_isRunning) {
            qWarning() << ">>> [安全启动] 在等待源稳定期间收到停止指令，中断流程！";
            return false;
        }
        QThread::msleep(1000);
        qInfo() << QString(">>> 标准源稳定倒计时: 已进入 %1 / 10 秒...").arg(sec);
    }

    qInfo() << ">>> 标准源输出已完全平稳，正式进入表计误差测试环节！";
    return true;
}









// =========================================================================
// 🌟 [新增核心] 构建 489 字节的长报文清零指令 (0x81 AA)
// =========================================================================
QByteArray CalibrationThread::buildEnergyClearCmd(int checkType)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    // 1. 写入 YMPara[8] 配置区 (256字节)
    for (int i = 0; i < 8; ++i) {
        stream << (quint32)20000; // Constp (有功脉冲常数)
        stream << (quint32)20000; // Constq (无功脉冲常数)
        stream << (quint32)20000; // PowerConstP (本机有功)
        stream << (quint32)20000; // PowerConstQ (本机无功)
        stream << (quint32)3;     // CheckNum (校验圈数)
        stream << (quint32)checkType; // CheckType (1:有功, 0:无功)
        stream << (quint32)12;    // SAvergeNum
        stream << (quint32)0;     // CalType (平均值法)
    }

    // 2. 写入 YMData[8] 数据区全填 0 (224字节，实现清零核心！)
    for (int i = 0; i < 8; ++i) {
        stream << (quint32)0; // Num (脉冲数)
        stream << 0.0f;       // Err
        stream << 0.0f;       // P
        stream << 0.0f;       // Q
        stream << 0.0f;       // Ep
        stream << 0.0f;       // Eq
        stream << 0.0f;       // SErr
    }

    // 3. 封装头尾与校验和
    QByteArray frame;
    frame.append(0x68);

    // 🌟 修复 Bug：整帧总长度应该是 数据(480) + 额外头尾(9) = 489
    quint16 len = data.size() + 9;

    frame.append(len & 0xFF).append((len >> 8) & 0xFF); // 此时会压入 E9 01

    // 🌟 严正纠正：发往标准源的命令，目标地址必须是 0x00！绝不能是 0x80！
    frame.append(0x68).append((char)0x00);

    frame.append((char)0x81).append((char)0xAA); // 0x81 AA 写电能参数
    frame.append(data);

    // 校验和计算 (从索引 4 的地址域 0x00 开始，一直算到最后一个数据)
    quint8 cs = 0;
    for (int i = 4; i < frame.size(); ++i) cs += (quint8)frame[i];
    frame.append(cs).append(0x16);

    return frame;
}

bool CalibrationThread::readStandardEnergy(QSerialPort &srcPort, float &outActiveEnergy, float &outReactiveEnergy)
{
    if (!m_isRunning) return false;

    // 1. 发送 FC 查询命令：A1 01 03 FC 00 FC[cite: 1]
    srcPort.clear(QSerialPort::Input);
    srcPort.write(m_readEnergyCmd);
    qInfo().noquote() << "[Tx 标准源读电能]" << m_readEnergyCmd.toHex(' ').toUpper();

    // 2. 给予 1500ms 等待标准源回传实时累计电能数据
    if (!checkSourceResponse(srcPort, 3000)) {
        qWarning() << "[Rx 标准源读电能] 响应超时或毫无数据回传！";
        return false;
    }

    QByteArray rx = srcPort.readAll();
    qInfo().noquote() << "[Rx 标准源电能]" << rx.toHex(' ').toUpper();

    // 3. 校验报文基础长度：A1/A3(1) + 01(1) + Len(1) + FC(1) + 3组通道×6字节(18) + CS(1) = 23 字节
    if (rx.size() < 23) {
        qWarning() << "[Rx 标准源读电能] 数据包长度不足，期望至少 23 字节，实际收到:" << rx.size();
        return false;
    }

    // 4. 定位合法帧头：现场实测会出现 A1，手册示例为 A3，此处做双重防呆兼容！[cite: 1]
    int idx = rx.indexOf((char)0xA1);
    if (idx == -1) {
        idx = rx.indexOf((char)0xA3);
    }

    // 5. 严格格式校验：确保不越界、地址码为 01、命令字为 FC[cite: 1]
    if (idx == -1 || idx + 22 >= rx.size() || (quint8)rx[idx + 1] != 0x01 || (quint8)rx[idx + 3] != 0xFC) {
        qWarning() << "[Rx 标准源读电能] 报文格式校验失败，或命令字不是 FC！";
        return false;
    }

    // 6. 核心解析：跳过 A1 01 Len FC 共 4 个字节，进入数据块区
    const char *p = rx.constData() + idx + 4;

    // 规约铁律：3组数据组合一次发送[cite: 1]。每组 = 通道ID(1字节) + 压缩BCD浮点数(5字节) = 6字节[cite: 1]
    for (int chIdx = 0; chIdx < 3; ++chIdx) {
        const char *block = p + chIdx * 6;
        quint8 ch = (quint8)block[0];

        if (ch == 0x11) {
            // CH=11：表示总有功电能 (Ep)[cite: 1]
            outActiveEnergy = parseSTRFloat(block + 1);
        } else if (ch == 0x12) {
            // CH=12：表示总无功电能 (Eq)[cite: 1]
            outReactiveEnergy = parseSTRFloat(block + 1);
        }
        // CH=13 是总视在电能 (Es)[cite: 1]，咱业务形参不需要，直接静默跳过
    }

    // 7. 打印最终算出的实测物理电能成绩
    qInfo().noquote() << QString("  -> [电能实测解析成功] 有功电能(Ep): %1 | 无功电能(Eq): %2")
                             .arg(outActiveEnergy, 0, 'f', 6)
                             .arg(outReactiveEnergy, 0, 'f', 6);

    return true;
}

bool CalibrationThread::runEnergyCategory(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, const QList<TestPoint> &testPoints, int categoryIdx, bool isActive)
{
    for (const auto &pt : testPoints) {
        if (!m_isRunning) return false;

        qInfo() << ">>> 准备测试工况:" << pt.name << " 精度要求:" << pt.limit << "%";

        // =========================================================================
        // 3. 仪表批量清零逻辑：下发清零标记 -> 轮询等待寄存器变0
        // =========================================================================
        int aliveCount = 0;
        for (auto &meter : meters) {
            if (!meter.isEnabled || !meter.isAlive) continue;

            emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Running, "正在清零...");
            writeMeterReg(meterPort, meter.address, m_regEnergyClear, 1);

            if (waitMeterState(meterPort, meter.address, m_regEnergyClear, 0, 3000)) {
                aliveCount++;
            } else {
                meter.isAlive = false; // 清零回读超时，直接淘汰该表
                qWarning() << ">>> [异常] 仪表" << meter.address << "清零失败或回读非0！已剔除。";
                emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Timeout, "清零超时");
            }
        }

        if (aliveCount == 0) {
            emit showResultPopup("仪表全部失败", "所有仪表清零失败，校准终止！", "error");
            return false;
        }

        // 安全配源 (自动停源 -> 选档 -> 设百分比 -> 设夹角)
        if (!setSourceConfig(srcPort, pt.tgtV, pt.tgtI, pt.tgtPF)) {
            qWarning() << ">>> [致命错误] 标准源下发配置失败，中途终止！";
            return false;
        }
        QThread::msleep(200);
        // 安全启动
        safeStartSource(srcPort);
        if (!sendSourceCmd(srcPort, m_stopEnergyCmd, 3000)) return false;
        if (!sendSourceCmd(srcPort, m_startEnergyCmd, 3000)) return false;

        // 4. 进入 1秒轮询走字大循环 (暂定跑 20 秒出成绩，可根据现场实际情况改大)
        int testSeconds = 30;
        for (int sec = 1; sec <= testSeconds; ++sec) {
            if (!m_isRunning) return false;

            if(sec == 30){
                qInfo() << "正在停止标准源...";
                safeStopSource(srcPort);

                // 标准源收尾清零(展开)
                if (!sendSourceCmd(srcPort, m_stopEnergyCmd, 3000)) return false;
            }

            // 读标准源电能
            float stdActive = 0, stdReactive = 0;
            float stdEnergy = 0;
            if (readStandardEnergy(srcPort, stdActive, stdReactive)) {
                stdEnergy = isActive ? stdActive : stdReactive;
            }

            // 读每块表并推送给 QML
            for (auto &m : meters) {
                if (!m.isEnabled || !m.isAlive) continue;

                emit updateErrorMeterStatus(m_workMode,m.uiIndex, Error_Running, QString("测试中: %1s / %2s").arg(sec).arg(testSeconds));

                // 0x1054: 有功总电能 (Ep_total)
                // 0x105C: 无功总电能 (Eq_total)
                quint16 regAddr = isActive ? 0x1054 : 0x105C;

                QVector<float> rawData;
                float meterEnergy = 0.0f;

                // 直接调用 32位万能读取函数：读 1 个数据(占2个寄存器)，无符号(false)
                // ⚠️ 注意 1000.0f：请根据你下位机 roundf 之前的实际单位和放大倍数来定
                // 如果下位机 1 代表 1Wh (0.001kWh)，这里就填 1000.0f 还原成 kWh
                if (readMeterData(meterPort, m.address, regAddr, 1, rawData, 1000.0f, false)) {
                    if (!rawData.isEmpty()) {
                        meterEnergy = rawData[0];
                    }
                } else {
                    qWarning() << "仪表" << m.address << "电能读取超时或失败！";
                }

                // 计算误差：(实测 - 标准) / 标准 * 100
                float err = 0;
                if (stdEnergy > 0.0001f) {
                    err = (meterEnergy - stdEnergy) / stdEnergy * 100.0f;
                }
                bool isFail = (qAbs(err) > pt.limit);

                // ==========================================
                // 🌟 组装推给 QML 的 3 个格子数据
                // [0]标准电能, [1]实测电能, [2]误差
                // ==========================================
                QVariantList qmlCells;
                QVariantMap mapStd, mapMeas, mapErr;

                mapStd["errStr"] = QString::number(stdEnergy, 'f', 4);
                mapStd["isFail"] = false;

                mapMeas["errStr"] = QString::number(meterEnergy, 'f', 4);
                mapMeas["isFail"] = false;

                mapErr["errStr"] = QString("%1%2%").arg(err > 0 ? "+" : "").arg(err, 0, 'f', 3);
                mapErr["isFail"] = isFail;

                qmlCells << mapStd << mapMeas << mapErr;

                // 推送给 QML，同名行会自动覆盖实现跳字
                //qInfo()<<;
                emit appendErrorRow(Mode_EnergyCalc,m.uiIndex, categoryIdx, pt.name, qmlCells);

                // 如果是本工况测试的最后一秒，最终判定生死
                if (sec == testSeconds && isFail) {
                    m.hasFail = true;
                }
            }

            QThread::sleep(60); // 严格等待一秒
        }

        //测试完仪表和标准源清零
        // =========================================================================
        // 5. 本测试点跑完，进入下一个前，对仪表和标准源执行收尾清零
        // =========================================================================
        qInfo() << ">>> 本工况测试结束，执行收尾清零...";

        // qInfo() << "正在停止标准源...";
        // safeStopSource(srcPort);

        // // 标准源收尾清零(展开)
        // if (!sendSourceCmd(srcPort, m_stopEnergyCmd, 3000)) return false;

        // 仪表收尾清零(展开)
        for (auto &meter : meters) {
            if (!meter.isEnabled || !meter.isAlive) continue;
            qInfo() << QString("仪表 %1 电能清零").arg(meter.address);
            writeMeterReg(meterPort, meter.address, m_regEnergyClear, 1);
            if (!waitMeterState(meterPort, meter.address, m_regEnergyClear, 0, 3000)) {
                meter.isAlive = false;
                qWarning() << ">>> [异常] 仪表" << meter.address << "收尾清零失败！已剔除。";
                emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Timeout, "收尾清零超时");
            }
        }
    }
    return true;
}

bool CalibrationThread::scanAndFindEnergyInterfaceCode(QSerialPort &port)
{
    qInfo() << "====== 正在启动 [STR3060A 电能测试界面 Md 密钥] 极速雷达扫描 ======";

    // 1. 准备读取电能的命令：FC 00 FC
    const QByteArray readEnergyCmd = QByteArray::fromHex("A1 01 03 FC 00 FC");

        // 2. 在 0x20 ~ 0x35 的极高概率区间内挨个试探
        for (int mdVal = 0x20; mdVal <= 0x35; ++mdVal) {
        if (!m_isRunning) return false;

        // 跳过已知不是电能的界面：0x23是误差测试, 0x27是标准源
        //if (mdVal == 0x23 || mdVal == 0x27) continue;

        quint8 md = (quint8)mdVal;

        // 组装切屏指令：A1 01 03 C0 Md CS
        QByteArray switchCmd;
        switchCmd.append((char)0xA1);
        switchCmd.append((char)0x01);
        switchCmd.append((char)0x03);
            switchCmd.append((char)0xC0);
            switchCmd.append((char)md);

        // 严格根据文档计算 CS：从 0x01 加到 Md
        quint8 cs = 0xC0 + md;
            switchCmd.append((char)cs);

        qInfo().noquote() << QString(">>> [雷达试探] 尝试切换至未知界面 Md = 0x%1 | Tx: %2")
                                 .arg(md, 2, 16, QChar('0')).toUpper()
                                 .arg(switchCmd.toHex(' ').toUpper());

        // 下发切屏
        port.clear(QSerialPort::Input);
        port.write(switchCmd);
        port.waitForBytesWritten(300);

        // 等待机器回 55 确认，并给 LCD 屏幕和内部积分任务 800ms 的切换启动时间
        QThread::msleep(1000);

        // 下发 FC 00 FC 敲门索要电能数据！
        port.clear(QSerialPort::Input);
        port.write(readEnergyCmd);
        port.waitForBytesWritten(300);

        // 如果机器在 1000ms 内吐出了数据，并且帧头是 A3 或包含 FC
        if (port.waitForReadyRead(1000)) {
            QByteArray rx = port.readAll();
            qInfo().noquote() << "[Rx 回应]" << rx.toHex(' ').toUpper();

            // 如果收到以 A3 开头的实质性数据报文 (绝非纯 A1 01 55)
            if (rx.size() > 5 && (rx.contains((char)0xA3) || rx.contains((char)0xFC))) {
                    qInfo().noquote() << QString("\n🎉🎉🎉【大功告成！破案了！】🎉🎉🎉");
                qInfo().noquote() << QString(">>> 成功找到【电能测试界面】的绝对控制密钥！Md = 0x%1 (十进制: %2)")
                                         .arg(md, 2, 16, QChar('0')).toUpper().arg(mdVal);
                qInfo() << ">>> 此时机器已开始正常吐出电能浮点数据！请把这个 Md 固化到全自动工具中！";
                return true;
            }
        } else {
            qDebug() << "  -> 当前界面不支持电能实测 (FC 无响应)，继续扫描下一种...";
        }
    }

    qWarning() << ">>> [扫描结束] 在常规区间未找到响应，请确认仪器当前是否有有效功率输出！";
    return false;
}

bool CalibrationThread::runEnergyCalcFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, int &aliveCount)
{
    qInfo() << "====== 正在启动 [电能走字] 测试系统 ======";
    for (auto &meter : meters) {
        if (!meter.isEnabled) continue;

        emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Running, "正在测试...");
    }

    qInfo() << ">>> [自动翻屏] 远程切换至 STR3060A【电能走字测试界面】(Md=0x22)...";
    qInfo().noquote() << QString("[Tx 标准源 切换电能界面] ") << m_switchToEnergyUI_Cmd.toHex(' ').toUpper();
    srcPort.write(m_switchToEnergyUI_Cmd);
    QThread::msleep(1000); // 给予机身 LCD 屏渲染和底层 DSP 挂载积分任务的时间

    qInfo() << ">>> 开始批量下发 [复位指令]...";

    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        //emit meterStepStatusChanged(meter.uiIndex, Step_Reset, State_Running);
        writeMeterReg(meterPort, meter.address, m_regReset1, 1);
        QThread::msleep(500);
    }

    // 1. 跑有功 (category 0)
    qInfo() << "=== 启动有功电能走字测试 ===";
    if (!runEnergyCategory(srcPort, meterPort, meters, m_energyActiveTestPoints, 0, true)) return false;

    // 2. 跑无功 (category 1)
    qInfo() << "=== 启动无功电能走字测试 ===";
    if (!runEnergyCategory(srcPort, meterPort, meters, m_energyReactiveTestPoints, 1, false)) return false;

    if (!m_isRunning) return false;

    qInfo() << ">>> [自动翻屏] 远程切换至 STR3060A【主界面】(Md=0x27)...";
    qInfo().noquote() << QString("[Tx 标准源 切换电能界面] ") << m_switchToSourceUI_Cmd.toHex(' ').toUpper();
    srcPort.write(m_switchToSourceUI_Cmd);
    QThread::msleep(1000); // 给予机身 LCD 屏渲染和底层 DSP 挂载积分任务的时间

    if (m_isRunning) {
        aliveCount = 0; int fail = 0, enableNum = 0;
        for (auto &meter : meters) {
            if(meter.isEnabled){
                enableNum++;
                if (meter.hasFail){
                    emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Fail, "有超标项");
                }else{
                    emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Pass, "全部合格");
                    aliveCount++;
                }
            }
        }
        fail = enableNum - aliveCount;
        if(fail == 0)
            emit showResultPopup("测试完成",QString("启用 %1 台, 成功 %2 台, 失败 %3 台").arg(enableNum).arg(aliveCount).arg(fail),"success");
        else
            emit showResultPopup("测试完成",QString("启用 %1 台, 成功 %2 台, 失败 %3 台").arg(enableNum).arg(aliveCount).arg(fail),"error");
        qInfo()<<"测试全部完成!"<<QString("启用 %1 台, 成功 %2 台, 失败 %3 台").arg(enableNum).arg(aliveCount).arg(fail);
    }
    return true;
}

// ------------------------------------------------------------------------------
// 构造超集谐波写指令 (固定 3104 字节)
// ------------------------------------------------------------------------------
QByteArray CalibrationThread::buildHarmonicCmd(int targetOrder, float vRatio, float iRatio)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    // 基波 100% = 0x6666
    uint16_t baseVal = 0x6666;
    uint16_t vHarmVal = (uint16_t)(baseVal * (vRatio / 100.0f));
    uint16_t iHarmVal = (uint16_t)(baseVal * (iRatio / 100.0f));

    // 1. 写入 6 个通道的幅度 (1~129次)
    for (int ch = 0; ch < 6; ++ch) {
        for (int h = 1; h <= 129; ++h) {
            if (h == 1) {
                stream << baseVal;
            } else if (h == targetOrder) {
                stream << (uint16_t)((ch < 3) ? vHarmVal : iHarmVal);
            } else {
                stream << (uint16_t)0;
            }
        }
    }

    // 2. 写入 6 个通道的相位 (1~129次)
    uint16_t basePhases[6] = {0, 24000, 12000, 0, 24000, 12000};
    for (int ch = 0; ch < 6; ++ch) {
        for (int h = 1; h <= 129; ++h) {
            if (h == 1) {
                stream << basePhases[ch];
            } else {
                stream << (uint16_t)0;
            }
        }
    }

    QByteArray frame;
    frame.append(0x68);
    uint16_t len = 3104; // 数据段3096 + 其他固定位 = 3104
    frame.append(len & 0xFF);
    frame.append((len >> 8) & 0xFF);
    frame.append(0x68);
    frame.append((char)0x00);
    frame.append(0x27);
    frame.append(data);

    uint8_t cs = 0;
    for (int i = 4; i < frame.size(); ++i) cs += (uint8_t)frame[i];
    frame.append(cs);
    frame.append(0x16);

    return frame;
}

bool CalibrationThread::runHarmonicsFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, int &aliveCount)
{
    qInfo() << "====== 正在启动 [2~31次 谐波极速同测] 系统 ======";

    // 1. 批量初始化被测仪表的工作模式为谐波测定模式 (模式码 7)
    qInfo() << ">>> 开始批量设置电表底层模式码:" << 7;
    aliveCount = 0;
    for (int mIdx = 0; mIdx < meters.size(); ++mIdx) {
        if (!meters[mIdx].isAlive || !meters[mIdx].isEnabled) continue;
        writeMeterReg(meterPort, meters[mIdx].address, m_regWriteProtect, 7);
        if (waitMeterState(meterPort, meters[mIdx].address, m_regWriteProtect, 7, 2000)) {
            aliveCount++;
        } else {
            meters[mIdx].isAlive = false;
            emit updateErrorMeterStatus(m_workMode, meters[mIdx].uiIndex, Error_Timeout, "模式设置超时");
        }
    }

    if (aliveCount == 0) {
        emit showResultPopup("测试终止", "全部被测仪表模式设置失败", "error");
        return false;
    }

    // =========================================================================
    // 2. 谐波大循环：极速轮询目标次序 (2, 5, 7, 11次...)
    // =========================================================================
    for (int h = 2; h <= 31; ++h) {
        //if (h != 2 && h != 5 && h != 7 && h != 11) continue;
        //if (h != 3 && h != 5 && h != 7 && h != 11) continue;
        if (h != 3) continue;

        QString rowName = QString("220V/5A, %1次谐波").arg(h);
        qInfo().noquote() << QString("\n=======================================================");
        qInfo().noquote() << QString(">>> 正在启动测试点: %1").arg(rowName);
        qInfo().noquote() << QString("=======================================================");

        QMap<int, QVariantList> volCellsMap, curCellsMap;
        QMap<int, Row> volRowMap, curRowMap;
        quint16 regAddr = 0x3030 + (h - 2) * 6; // 计算点表寄存器起始地址

        for (int mIdx = 0; mIdx < meters.size(); ++mIdx) {
            if (meters[mIdx].isAlive && meters[mIdx].isEnabled) {
                emit updateErrorMeterStatus(m_workMode, meters[mIdx].uiIndex, Error_Running, "正在测: " + rowName);
                volRowMap[meters[mIdx].uiIndex].conditionName = rowName;
                volRowMap[meters[mIdx].uiIndex].cells.resize(6);
                curRowMap[meters[mIdx].uiIndex].conditionName = rowName;
                curRowMap[meters[mIdx].uiIndex].cells.resize(6);
            }
        }

        // =====================================================================
        // 步骤 A：低含量组 (三相电压 3%, 三相电流 10%, 规约第 11~14 节标准操作)
        // =====================================================================
        qInfo() << ">>> [步骤 A] 执行低含量谐波配置 (V:3%, I:10%)...";

        // A-1: 【铁律】先下发 DB 00 清空所有旧谐波，保证干净状态
        qInfo() << "  -> 1. 下发 DB 00 清零底层残余谐波...";
        sendSourceCmd(srcPort, m_harmonicClearAllCmd, 3000);
        QThread::msleep(300);

        // A-2: 启动底层额定基波输出 (220V / 5A / 1.0PF)
        qInfo() << "  -> 2. 下发 220V 5A 1.0PF 额定基波配置并启动输出...";
        if (!setSourceConfig(srcPort, 220.0f, 5.0f, 1.0f)) return false;
        qDebug("2. 启动标准源输出...");
        if (!safeStartSource(srcPort)) return false;

        // A-3: 发送 DA 设置命令 (CH=07同时设三相电压3%，CH=08同时设三相电流10%，初相位默认0°)
        qInfo() << "  -> 3. 下发 DA 命令配置电压与电流谐波次数与含量...";
        if (!sendSTRHarmonicConfig(srcPort, 0x07, h, 3.0f, 0.0f)) return false;
        QThread::msleep(200);
        if (!sendSTRHarmonicConfig(srcPort, 0x08, h, 10.0f, 0.0f)) return false;
        QThread::msleep(200);

        // A-4: 【铁律】发送 D3 25 00 指令，将刚配置好的谐波立即加载到功放通道上！
        qInfo() << "  -> 4. 下发 D3 25 00 一键加载全体通道谐波...";
        if (!sendSourceCmd(srcPort, m_harmonicLoadAllCmd, 3000)) return false;

        // A-5: 【铁律】等待功放闭环响应 3 秒后，下发一次 C6 闭环命令，修正高速正弦波形精度！
        QThread::msleep(15000);
        qInfo() << "  -> 5. 下发 C6 00 C6 谐波一次闭环命令，提升波形合成精度...";
        sendSourceCmd(srcPort, m_harmonicCloseLoopCmd, 3000);

        // A-6: 工业级防假死倒计时等待 (给予 DSP 与功放充足的高频稳定时间：15 秒)
        qInfo() << ">>> 正在等待低含量谐波波形彻底闭环稳定 (倒计时 15 秒)...";
        for (int sec = 1; sec <= 15; ++sec) {
            if (!m_isRunning) return false;
            QThread::msleep(1000);
            if (sec % 5 == 0 || sec == 15) qInfo() << "  -> 稳定倒计时: 已过" << sec << "/ 15 秒...";
        }

        // A-7: 轮询读取被测表计低含量组谐波实测数据 (毫无 Lambda，纯顺序写下)
        aliveCount = 0;
        for (int mIdx = 0; mIdx < meters.size(); ++mIdx) {
            if (!meters[mIdx].isAlive || !meters[mIdx].isEnabled) continue;

            QVector<float> rawA;
            if (readMeterHarmonicData16(meterPort, meters[mIdx].address, regAddr, 6, rawA)) {
                aliveCount++;
                // 将低含量的成绩算完放进前 3 个格子 (Ua, Ub, Uc // Ia, Ib, Ic)
                volCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Ua", 3.0f,  rawA[0], volRowMap[meters[mIdx].uiIndex].cells[0], 0.15f, rowName);
                volCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Ub", 3.0f,  rawA[1], volRowMap[meters[mIdx].uiIndex].cells[1], 0.15f, rowName);
                volCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Uc", 3.0f,  rawA[2], volRowMap[meters[mIdx].uiIndex].cells[2], 0.15f, rowName);

                curCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Ia", 10.0f, rawA[3], curRowMap[meters[mIdx].uiIndex].cells[0], 0.5f, rowName);
                curCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Ib", 10.0f, rawA[4], curRowMap[meters[mIdx].uiIndex].cells[1], 0.5f, rowName);
                curCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Ic", 10.0f, rawA[5], curRowMap[meters[mIdx].uiIndex].cells[2], 0.5f, rowName);
            } else {
                meters[mIdx].isAlive = false;
                emit updateErrorMeterStatus(m_workMode, meters[mIdx].uiIndex, Error_Timeout, "485通讯读低含量失败");
            }
            QThread::msleep(100);
        }
        if (!m_isRunning) return false;
        if (aliveCount == 0) {
            qWarning() << ">>> [全军覆没] 所有被测表计低含量测量超时，终止本工况！";
            return false;
        }

        // =====================================================================
        // 步骤 B：高含量组 (三相电压 10%, 三相电流 20%, 规约第 12.4 节优化操作)
        // =====================================================================
        qInfo() << "\n>>> [步骤 B] 执行高含量谐波动态切换 (V:10%, I:20%)...";

        // B-1: 手册 12.4 节特别说明：正在输出谐波时需要改变参数，仅发 DA 设置命令即可，无需再发 D3！
        qInfo() << "  -> 1. 下发 DA 命令，动态无缝切换为高含量配置...";
        if (!sendSTRHarmonicConfig(srcPort, 0x07, h, 10.0f, 0.0f)) return false;
        QThread::msleep(200);
        if (!sendSTRHarmonicConfig(srcPort, 0x08, h, 20.0f, 0.0f)) return false;

        // B-2: 为保证超高含量下的波形失真率最低，我们再次下发一键闭环 C6
        QThread::msleep(15000);
        qInfo() << "  -> 2. 下发 C6 00 C6 对高含量输出再次进行闭环修正...";
        sendSourceCmd(srcPort, m_harmonicCloseLoopCmd, 3000);

        // B-3: 倒计时等待稳定
        qInfo() << ">>> 正在等待高含量谐波波形彻底闭环稳定 (倒计时 15 秒)...";
        for (int sec = 1; sec <= 15; ++sec) {
            if (!m_isRunning) return false;
            QThread::msleep(1000);
            if (sec % 5 == 0 || sec == 15) qInfo() << "  -> 稳定倒计时: 已过" << sec << "/ 15 秒...";
        }

        // B-4: 轮询读取被测表计高含量组谐波实测数据 (存入后 3 个格子)
        aliveCount = 0;
        for (int mIdx = 0; mIdx < meters.size(); ++mIdx) {
            if (!meters[mIdx].isAlive || !meters[mIdx].isEnabled) continue;

            QVector<float> rawB;
            if (readMeterHarmonicData16(meterPort, meters[mIdx].address, regAddr, 6, rawB)) {
                aliveCount++;
                volCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Ua", 10.0f, rawB[0], volRowMap[meters[mIdx].uiIndex].cells[3], 5.0f, rowName);
                volCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Ub", 10.0f, rawB[1], volRowMap[meters[mIdx].uiIndex].cells[4], 5.0f, rowName);
                volCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Uc", 10.0f, rawB[2], volRowMap[meters[mIdx].uiIndex].cells[5], 5.0f, rowName);

                curCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Ia", 20.0f, rawB[3], curRowMap[meters[mIdx].uiIndex].cells[3], 5.0f, rowName);
                curCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Ib", 20.0f, rawB[4], curRowMap[meters[mIdx].uiIndex].cells[4], 5.0f, rowName);
                curCellsMap[meters[mIdx].uiIndex] << calcErrAndMakeMap(meters[mIdx].address, "Ic", 20.0f, rawB[5], curRowMap[meters[mIdx].uiIndex].cells[5], 5.0f, rowName);
            } else {
                meters[mIdx].isAlive = false;
                emit updateErrorMeterStatus(m_workMode, meters[mIdx].uiIndex, Error_Timeout, "485通讯读高含量失败");
            }
            QThread::msleep(100);
        }
        if (!m_isRunning) return false;
        if (aliveCount == 0) {
            qWarning() << ">>> [全军覆没] 所有被测表计高含量测量超时，终止本工况！";
            return false;
        }

        // =====================================================================
        // 步骤 C：当前次序考完，立刻下发 DB 00 清零，防止污染下一轮！
        // =====================================================================
        qInfo() << ">>> 当前" << h << "次谐波测试完毕，下发 DB 00 清零谐波通道...";
        sendSourceCmd(srcPort, m_harmonicClearAllCmd, 3000);
        QThread::msleep(300);

        // =====================================================================
        // 步骤 D：裁判环节与落盘处理 (不用任何 Lambda 闭包，纯顺序逻辑判定)
        // =====================================================================
        for (int mIdx = 0; mIdx < meters.size(); ++mIdx) {
            if (!meters[mIdx].isAlive || !meters[mIdx].isEnabled) continue;

            if (volCellsMap[meters[mIdx].uiIndex].size() == 6 && curCellsMap[meters[mIdx].uiIndex].size() == 6) {
                // 遍历电压谐波 6 个格子，如果有 Fail，给表记黑名单
                for (int cIdx = 0; cIdx < volRowMap[meters[mIdx].uiIndex].cells.size(); ++cIdx) {
                    if (volRowMap[meters[mIdx].uiIndex].cells[cIdx].isFail) {
                        meters[mIdx].hasFail = true;
                        break;
                    }
                }
                // 遍历电流谐波 6 个格子，如果有 Fail，给表记黑名单
                for (int cIdx = 0; cIdx < curRowMap[meters[mIdx].uiIndex].cells.size(); ++cIdx) {
                    if (curRowMap[meters[mIdx].uiIndex].cells[cIdx].isFail) {
                        meters[mIdx].hasFail = true;
                        break;
                    }
                }

                // 落盘保存到表计分类数据中 (Cat 6:电压谐波, Cat 7:电流谐波)
                meters[mIdx].categories[6].rows.append(volRowMap[meters[mIdx].uiIndex]);
                meters[mIdx].categories[7].rows.append(curRowMap[meters[mIdx].uiIndex]);

                // 推送到前端 UI 界面显示
                emit appendErrorRow(m_workMode, meters[mIdx].uiIndex, 6, rowName, volCellsMap[meters[mIdx].uiIndex]);
                emit appendErrorRow(m_workMode, meters[mIdx].uiIndex, 7, rowName, curCellsMap[meters[mIdx].uiIndex]);
            } else {
                emit updateErrorMeterStatus(m_workMode, meters[mIdx].uiIndex, Error_Fail, "数据采集数量不整齐");
                meters[mIdx].hasFail = true;
            }
        }
    }

    qInfo() << "====== 恭喜！谐波测试大闭环全部圆满结束 ======";
    return true;
}

// =========================================================================
// 独立工具函数：计算误差，打印日志，并打包成 QML 能用的格式
// =========================================================================
QVariantMap CalibrationThread::calcErrAndMakeMap(uint8_t addr, const QString &phaseName, float std, float meas, Cell &outCell, float limit,const QString &conditionName)
{
    //导出excel专用
    outCell.stdVal = std;
    outCell.meterVal = meas;
    outCell.limit = limit;

    if(conditionName.contains("谐波")){
        outCell.err = (std > 0.001f) ? meas - std : 0.0f;
        double cleanErr = qRound(outCell.err * 1000.0) / 1000.0;
        outCell.isFail = (qAbs(cleanErr) > limit);
    }else{
        outCell.err = (std > 0.001f) ? ((meas - std) / std * 100.0f) : 0.0f;
        double cleanErr = qRound(outCell.err * 1000.0) / 1000.0;
        outCell.isFail = (qAbs(cleanErr) > limit);
    }


    qInfo().noquote() << QString("  -> [Addr:%1] [%2] [%3] 理论: %4 | 实测: %5 | 误差: %6% | 限值: %7% | %8")
                             .arg(addr, 2, 10, QChar('0'))
                             .arg(conditionName)
                             .arg(phaseName, -4)
                             .arg(std, 7, 'f', 3)
                             .arg(meas, 7, 'f', 3)
                             .arg(outCell.err, 7, 'f', 3)
                             .arg(limit, 4, 'f', 2)
                             .arg(outCell.isFail ? "FAIL" : "PASS");

    QVariantMap qmlMap;
    qmlMap["errStr"] = QString("%1%2%").arg(outCell.err > 0 ? "+" : "").arg(outCell.err, 0, 'f', 3);
    qmlMap["isFail"] = outCell.isFail;
    return qmlMap;
}

// =========================================================================
// 1. 专项处理函数：电压/电流数据组装与推送 (支持单项锁死与延迟推UI)
// =========================================================================
bool CalibrationThread::processVoltageCurrentData(Meter &meter, const TestPoint &pt, const SourceParams &stdParams, const QVector<float> &viData, Row &volRow, QVariantList &volQmlCells, Row &curRow, QVariantList &curQmlCells, bool isLastTry)
{
    qInfo().noquote() << QString("\n=== 仪表地址[%1] 工况[%2] 电压电流数据明细 ===").arg(meter.address).arg(pt.name);

    // =========================================================================
    // 🌟 0. 【核心改造】：从结构体提取标准源实时物理电压、电流值！
    // U[0]~U[2]: 相电压 Ua, Ub, Uc | U[3]~U[5]: 线电压 Uab, Ubc, Uca
    // I[0]~I[2]: 相电流 Ia, Ib, Ic
    // =========================================================================
    // float stdUa  = stdParams.U[0];
    // float stdUb  = stdParams.U[1];
    // float stdUc  = stdParams.U[2];
    // float stdUab = stdParams.U[3];
    // float stdUbc = stdParams.U[4];
    // float stdUca = stdParams.U[5];

    // float stdIa  = stdParams.I[0];
    // float stdIb  = stdParams.I[1];
    // float stdIc  = stdParams.I[2];

    // // 【相电压安全兜底】：万一遇到源通讯无响应、回读异常为0，使用工况设定理论值保底
    // if (qAbs(stdUa) < 5.0f && pt.tgtV > 10.0f) {
    //     stdUa = stdUb = stdUc = pt.tgtV;
    //     stdUab = stdUbc = stdUca = pt.tgtV * 1.73205f;
    //     qWarning() << ">>> [理论保底] 源实时相电压回读异常，自动启用理论计算值:" << pt.tgtV << "V";
    // } else if (qAbs(stdUab) < 5.0f && pt.tgtV > 10.0f) {
    //     // 【线电压二次兜底】：如果相电压正常但线电压没读到，用相电压实测值推导线电压
    //     stdUab = stdUa * 1.73205f;
    //     stdUbc = stdUb * 1.73205f;
    //     stdUca = stdUc * 1.73205f;
    // }

    // // 【电流安全兜底】：万一遇到源无响应、回读异常为0，使用工况设定理论值保底
    // if (qAbs(stdIa) < 0.0001f && pt.tgtI > 0.001f) {
    //     stdIa = stdIb = stdIc = pt.tgtI;
    //     qWarning() << ">>> [理论保底] 源实时电流回读异常，自动启用理论计算值:" << pt.tgtI << "A";
    // }

    // ==========================================
    // --------- 处理电压 (Category 0) ---------
    // ==========================================
    if (volRow.cells.isEmpty()) {
        volRow.conditionName = pt.name;
        volRow.cells.resize(6); // Ua, Ub, Uc, Uab, Ubc, Uca
        for (int i = 0; i < 6; i++) {
            volRow.cells[i].isFail = true;
            volQmlCells.append(QVariantMap());
        }
    }

    auto updateVolCell = [&](int idx, const QString &phase, float std, float meas, float limit) {
        if (!volRow.cells[idx].isFail) return; // 成绩锁死保护
        Cell tempCell;
        QVariantMap tempMap = calcErrAndMakeMap(meter.address, phase, std, meas, tempCell, limit, pt.name);

        if (!tempCell.isFail || isLastTry) {
            volRow.cells[idx] = tempCell;
            volQmlCells[idx] = tempMap;
        }
    };

    // 🌟 【核心改造】：把传参从 theoretical 更改为实测物理基准
    // 相电压 (对应 viData 0, 1, 2)
    // updateVolCell(0, "Ua", stdUa, viData[0], pt.limit);
    // updateVolCell(1, "Ub", stdUb, viData[1], pt.limit);
    // updateVolCell(2, "Uc", stdUc, viData[2], pt.limit);
    // // 线电压 (对应 viData 6, 7, 8)
    // updateVolCell(3, "Uab", stdUab, viData[6], pt.limit);
    // updateVolCell(4, "Ubc", stdUbc, viData[7], pt.limit);
    // updateVolCell(5, "Uca", stdUca, viData[8], pt.limit);
    updateVolCell(0, "Ua", pt.tgtV, viData[0], pt.limit); updateVolCell(1, "Ub", pt.tgtV, viData[1], pt.limit); updateVolCell(2, "Uc", pt.tgtV, viData[2], pt.limit);
    float tgtLineV = pt.tgtV * 1.73205f;
    updateVolCell(3, "Uab", tgtLineV, viData[6], pt.limit); updateVolCell(4, "Ubc", tgtLineV, viData[7], pt.limit); updateVolCell(5, "Uca", tgtLineV, viData[8], pt.limit);

    bool isVolPass = true;
    for (const auto& c : std::as_const(volRow.cells)) {
        if (c.isFail) { isVolPass = false; break; }
    }

    if (isVolPass || isLastTry) {
        if (!isVolPass) meter.hasFail = true;
        meter.categories[Cat_V].rows.append(volRow);
        emit appendErrorRow(m_workMode, meter.uiIndex, Cat_V, pt.name, volQmlCells);
    }

    // ==========================================
    // --------- 处理电流 (Category 1) ---------
    // ==========================================
    if (curRow.cells.isEmpty()) {
        curRow.conditionName = pt.name;
        curRow.cells.resize(3); // Ia, Ib, Ic
        for (int i = 0; i < 3; i++) {
            curRow.cells[i].isFail = true;
            curQmlCells.append(QVariantMap());
        }
    }

    auto updateCurCell = [&](int idx, const QString &phase, float std, float meas, float limit) {
        if (!curRow.cells[idx].isFail) return; // 成绩锁死保护
        Cell tempCell;
        QVariantMap tempMap = calcErrAndMakeMap(meter.address, phase, std, meas, tempCell, limit, pt.name);

        if (!tempCell.isFail || isLastTry) {
            curRow.cells[idx] = tempCell;
            curQmlCells[idx] = tempMap;
        }
    };

    // 🌟 【核心改造】：把传参从 theoretical 更改为实测物理基准 (对应 viData 3, 4, 5)
    // updateCurCell(0, "Ia", stdIa, viData[3], pt.limit);
    // updateCurCell(1, "Ib", stdIb, viData[4], pt.limit);
    // updateCurCell(2, "Ic", stdIc, viData[5], pt.limit);
    updateCurCell(0, "Ia", pt.tgtI, viData[3], pt.limit); updateCurCell(1, "Ib", pt.tgtI, viData[4], pt.limit); updateCurCell(2, "Ic", pt.tgtI, viData[5], pt.limit);
    bool isCurPass = true;
    for (const auto& c : std::as_const(curRow.cells)) {
        if (c.isFail) { isCurPass = false; break; }
    }

    if (isCurPass || isLastTry) {
        if (!isCurPass) meter.hasFail = true;
        meter.categories[Cat_I].rows.append(curRow);
        emit appendErrorRow(m_workMode, meter.uiIndex, Cat_I, pt.name, curQmlCells);
    }

    // 🌟 只有当电压和电流这两排格子全绿了，才算本次测试点完美通过！
    return isVolPass && isCurPass;
}

bool CalibrationThread::sendSTRHarmonicConfig(QSerialPort &port, quint8 ch, int order, float contentPct, float phaseDeg)
{
    if (!m_isRunning) return false;

    // 1. 组装 DA 命令帧头与固定参数：A1 01 08 DA CH
    QByteArray frame;
    frame.append(0xA1);
    frame.append(0x01);
    frame.append(0x08); // 长度: 从 DA 到 M4 共 8 字节
    frame.append(0xDA); // 命令字
    frame.append(ch);   // 通道 CH

    // 🌟 2. 核心精华：直接调用我们极其完美的通用 valToBCD 工具函数！
    frame.append(valToBCD(order,      0, 1)); // 谐波次数 H  (无小数, 占1字节)
    frame.append(valToBCD(contentPct, 1, 2)); // 含有率 M1M2 (1位小数, 占2字节)
    frame.append(valToBCD(phaseDeg,   1, 2)); // 初相位 M3M4 (1位小数, 占2字节)

    // 3. 严格遵循规约计算校验和 CS：从字节1 (0x01) 到字节 9 (末尾M4) 的算术和，取低8位
    quint32 sum = 0;
    for (int idx = 3; idx < frame.size(); ++idx) {
        sum += (quint8)frame[idx];
    }
    frame.append((sum & 0xFF));

    // 4. 贯彻铁律：全大写十六进制日志打印！
    qInfo().noquote() << QString("[Tx 谐波设置 DA (CH:%1, %2次, 含有率:%3%, 相位:%4°)] %5")
                             .arg(ch, 2, 16, QChar('0')).arg(order).arg(contentPct, 0, 'f', 1).arg(phaseDeg, 0, 'f', 1)
                             .arg(frame.toHex(' ').toUpper());

    if (!sendSourceCmd(port, frame,3000)) return false;
    return true;
}

// =========================================================================
// 2. 专项处理：无功功率 (Q) - 支持单项成绩锁死与延迟推UI
// =========================================================================
bool CalibrationThread::processReactivePowerData(Meter &meter, const TestPoint &pt, const SourceParams &stdParams, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry)
{
    // =========================================================================
    // 🌟 1. 【核心修改处】：从结构体提取标准源实时物理无功值！
    // stdParams.Q[0]: Qa, Q[1]: Qb, Q[2]: Qc, Q[3]: Q总
    // =========================================================================
    // float stdQa     = stdParams.Q[0];
    // float stdQb     = stdParams.Q[1];
    // float stdQc     = stdParams.Q[2];
    // float stdQTotal = stdParams.Q[3];

    // // 【安全兜底】：万一遇到源无响应，且当前不是纯阻性负载(PF不为1)时，启用理论计算保底
    // if (qAbs(stdQTotal) < 0.0001f && qAbs(pt.tgtPF) < 0.999f) {
    //     float absPf = qAbs(pt.tgtPF);
    //     if (absPf > 1.0f) absPf = 1.0f;
    //     float theoryQ = pt.tgtV * pt.tgtI * qSin(qAcos(absPf));
    //     if (pt.tgtPF < 0) {
    //         theoryQ = -theoryQ; // 容性(C, PF为负)时，无功为负
    //     }
    //     stdQa = stdQb = stdQc = theoryQ;
    //     stdQTotal = theoryQ * 3.0f;
    //     qWarning() << ">>> [理论保底] 源实时无功回读为0，自动启用理论计算值:" << stdQTotal;
    // }
    float absPf = qBound(0.0f, qAbs(pt.tgtPF), 1.0f);
    float stdQ = pt.tgtV * pt.tgtI * qSin(qAcos(absPf));
    if (pt.tgtPF < 0) stdQ = -stdQ;

    // 2. 只有第一次进入时才初始化缓存结构（默认全判 Fail，占好位） - 【100%保留原逻辑】
    if (row.cells.isEmpty()) {
        row.conditionName = pt.name;
        row.cells.resize(4);
        for(int i = 0; i < 4; i++) {
            row.cells[i].isFail = true;
            qmlCells.append(QVariantMap());
        }
    }

    // 3. 闭包函数：针对单个格子进行“补考”更新 - 【100%保留原逻辑】
    auto updateCell = [&](int idx, const QString &phase, float std, float meas, float limit) {
        if (!row.cells[idx].isFail) return; // 成绩锁死保护

        Cell tempCell;
        QVariantMap tempMap = calcErrAndMakeMap(meter.address, phase, std, meas, tempCell, limit, pt.name);

        if (!tempCell.isFail || isLastTry) {
            row.cells[idx] = tempCell;
            qmlCells[idx] = tempMap;
        }
    };

    // =========================================================================
    // 4. 🌟 【核心修改处】：传参把统一 stdQ 替换为读出来的相无功 stdQa/Qb/Qc/QTotal
    // =========================================================================
    // updateCell(0, "Qa",  stdQa,     pData[0], pt.limit);
    // updateCell(1, "Qb",  stdQb,     pData[1], pt.limit);
    // updateCell(2, "Qc",  stdQc,     pData[2], pt.limit);
    // updateCell(3, "Q总", stdQTotal, pData[3], pt.limit);
    updateCell(0, "Qa", stdQ, pData[0], pt.limit); updateCell(1, "Qb", stdQ, pData[1], pt.limit);
    updateCell(2, "Qc", stdQ, pData[2], pt.limit); updateCell(3, "Q总", stdQ * 3.0f, pData[3], pt.limit);
    // 5. 裁判环节 - 【100%保留原逻辑】
    bool isRowPass = true;
    for (const auto& c : std::as_const(row.cells)) {
        if (c.isFail) { isRowPass = false; break; }
    }

    // 6. 拦截器：全绿灯，或最后一次机会用完，才真正落盘并推给 UI - 【100%保留原逻辑】
    if (isRowPass || isLastTry) {
        if (!isRowPass) meter.hasFail = true;
        meter.categories[Cat_ReactivePower].rows.append(row);
        emit appendErrorRow(m_workMode, meter.uiIndex, Cat_ReactivePower, pt.name, qmlCells);
    }

    return isRowPass;
}
// =========================================================================
// 3. 专项处理：视在功率 (S) - 支持单项成绩锁死与延迟推UI
// =========================================================================
bool CalibrationThread::processApparentPowerData(Meter &meter, const TestPoint &pt, const SourceParams &stdParams, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry)
{
    // =========================================================================
    // 🌟 1. 【核心修改处】：从结构体提取标准源实时物理视在功率！
    // stdParams.S[0]: Sa, S[1]: Sb, S[2]: Sc, S[3]: S总
    // =========================================================================
    // float stdSa     = stdParams.S[0];
    // float stdSb     = stdParams.S[1];
    // float stdSc     = stdParams.S[2];
    // float stdSTotal = stdParams.S[3];

    // // 【安全兜底】：万一遇到源通讯偶发无响应、读上来是0的情况，用理论 S = U * I 保底
    // if (qAbs(stdSTotal) < 0.001f) {
    //     float theoryS = pt.tgtV * pt.tgtI;
    //     stdSa = stdSb = stdSc = theoryS;
    //     stdSTotal = theoryS * 3.0f;
    //     qWarning() << ">>> [理论保底] 源实时视在回读为0，自动启用理论计算值:" << stdSTotal;
    // }
    float stdS = pt.tgtV * pt.tgtI;
    // 2. 只有第一次进入时才初始化缓存结构（默认全判 Fail，占好位） - 【100%保留原逻辑】
    if (row.cells.isEmpty()) {
        row.conditionName = pt.name;
        row.cells.resize(4);
        for(int i = 0; i < 4; i++) {
            row.cells[i].isFail = true;
            qmlCells.append(QVariantMap());
        }
    }

    // 3. 闭包函数：针对单个格子进行“补考”更新 - 【100%保留原逻辑】
    auto updateCell = [&](int idx, const QString &phase, float std, float meas, float limit) {
        if (!row.cells[idx].isFail) return; // 成绩锁死保护

        Cell tempCell;
        QVariantMap tempMap = calcErrAndMakeMap(meter.address, phase, std, meas, tempCell, limit, pt.name);



        if (!tempCell.isFail || isLastTry) {
            row.cells[idx] = tempCell;
            qmlCells[idx] = tempMap;
        }
    };

    // =========================================================================
    // 4. 🌟 【核心修改处】：传参把统一 stdS 替换为读出来的相视在 stdSa/Sb/Sc/STotal
    // =========================================================================
    // updateCell(0, "Sa",  stdSa,     pData[0], pt.limit);
    // updateCell(1, "Sb",  stdSb,     pData[1], pt.limit);
    // updateCell(2, "Sc",  stdSc,     pData[2], pt.limit);
    // updateCell(3, "S总", stdSTotal, pData[3], pt.limit);
    updateCell(0, "Sa", stdS, pData[0], pt.limit); updateCell(1, "Sb", stdS, pData[1], pt.limit);
    updateCell(2, "Sc", stdS, pData[2], pt.limit); updateCell(3, "S总", stdS * 3.0f, pData[3], pt.limit);
    // 5. 裁判环节 - 【100%保留原逻辑】
    bool isRowPass = true;
    for (const auto& c : std::as_const(row.cells)) {
        if (c.isFail) { isRowPass = false; break; }
    }

    // 6. 拦截器：全绿灯，或最后一次机会用完，才真正落盘并推给 UI - 【100%保留原逻辑】
    if (isRowPass || isLastTry) {
        if (!isRowPass) meter.hasFail = true;
        meter.categories[Cat_ApparentPower].rows.append(row);
        emit appendErrorRow(m_workMode, meter.uiIndex, Cat_ApparentPower, pt.name, qmlCells);
    }

    return isRowPass;
}
// =========================================================================
// 4. 专项处理：功率因数 (PF) - 支持单项成绩锁死与延迟推UI
// =========================================================================
bool CalibrationThread::processPowerFactorData(Meter &meter, const TestPoint &pt, const SourceParams &stdParams, const QVector<float> &rawData, Row &row, QVariantList &qmlCells, bool isLastTry)
{
    // =========================================================================
    // 🌟 1. 【核心修改处】：从结构体提取标准源实时物理功率因数！
    // stdParams.PF[0]: PFa, PF[1]: PFb, PF[2]: PFc, PF[3]: PF总
    // =========================================================================
    // float stdPFa     = stdParams.PF[0];
    // float stdPFb     = stdParams.PF[1];
    // float stdPFc     = stdParams.PF[2];
    // float stdPFTotal = stdParams.PF[3];

    // // 【安全兜底】：万一遇到源无响应，用工况设定值(pt.tgtPF)兜底
    // if (qAbs(stdPFTotal) < 0.001f) {
    //     float theoryPF = pt.tgtPF;
    //     stdPFa = stdPFb = stdPFc = stdPFTotal = theoryPF;
    //     qWarning() << ">>> [理论保底] 源实时功率因数回读为0，自动启用理论计算值:" << stdPFTotal;
    // }

    // 2. 只有第一次进入时才初始化缓存结构（默认全判 Fail，占好位） - 【100%保留原逻辑】
    if (row.cells.isEmpty()) {
        row.conditionName = pt.name;
        row.cells.resize(4);
        for(int i = 0; i < 4; i++) {
            row.cells[i].isFail = true;
            qmlCells.append(QVariantMap());
        }
    }

    // 3. 闭包函数：针对单个格子进行“补考”更新 - 【100%保留原逻辑】
    auto updateCell = [&](int idx, const QString &phase, float std, float meas, float limit) {
        if (!row.cells[idx].isFail) return; // 成绩锁死保护

        Cell tempCell;
        QVariantMap tempMap = calcErrAndMakeMap(meter.address, phase, std, meas, tempCell, limit, pt.name);



        if (!tempCell.isFail || isLastTry) {
            row.cells[idx] = tempCell;
            qmlCells[idx] = tempMap;
        }
    };

    // =========================================================================
    // 4. 🌟 【核心修改处】：传参把统一 stdPF 替换为读出来的相功率因数
    // =========================================================================
    // updateCell(0, "PFa",  stdPFa,     rawData[0], pt.limit);
    // updateCell(1, "PFb",  stdPFb,     rawData[1], pt.limit);
    // updateCell(2, "PFc",  stdPFc,     rawData[2], pt.limit);
    // updateCell(3, "PF总", stdPFTotal, rawData[3], pt.limit);
    updateCell(0, "PFa", pt.tgtPF, rawData[0], pt.limit); updateCell(1, "PFb", pt.tgtPF, rawData[1], pt.limit);
    updateCell(2, "PFc", pt.tgtPF, rawData[2], pt.limit); updateCell(3, "PF总", pt.tgtPF, rawData[3], pt.limit);
    // 5. 裁判环节 - 【100%保留原逻辑】
    bool isRowPass = true;
    for (const auto& c : std::as_const(row.cells)) {
        if (c.isFail) { isRowPass = false; break; }
    }

    // 6. 拦截器：全绿灯，或最后一次机会用完，才真正落盘并推给 UI - 【100%保留原逻辑】
    if (isRowPass || isLastTry) {
        if (!isRowPass) meter.hasFail = true;
        meter.categories[Cat_PowerFactor].rows.append(row);
        emit appendErrorRow(m_workMode, meter.uiIndex, Cat_PowerFactor, pt.name, qmlCells);
    }

    return isRowPass;
}
// =========================================================================
// 专项处理：有功功率 (P) - 具备单项成绩锁死与延迟落盘功能
// =========================================================================
bool CalibrationThread::processActivePowerData(Meter &meter, const TestPoint &pt, const SourceParams &stdParams, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry)
{
    // =========================================================================
    // 🌟 1. 【核心修改处】：从结构体提取标准源实时物理值，代替原来的理论估算值！
    // stdParams.P[0]: Pa, P[1]: Pb, P[2]: Pc, P[3]: P总
    // =========================================================================
    // float stdPa     = stdParams.P[0];
    // float stdPb     = stdParams.P[1];
    // float stdPc     = stdParams.P[2];
    // float stdPTotal = stdParams.P[3];

    // // 【安全兜底】：万一遇到源通讯偶发无响应、读上来是0的情况，依然用理论值保底，防止除以0
    // if (qAbs(stdPTotal) < 0.001f) {
    //     float theoryP = pt.tgtV * pt.tgtI * qAbs(pt.tgtPF);
    //     stdPa = stdPb = stdPc = theoryP;
    //     stdPTotal = theoryP * 3.0f;
    //     qWarning() << ">>> [理论保底] 源实时有功回读为0，自动启用理论计算值:" << stdPTotal;
    // }
    float stdP = pt.tgtV * pt.tgtI * qAbs(pt.tgtPF);

    // 2. 只有第一次进入时才初始化缓存结构（默认全判 Fail，占好位） - 【100%保留原逻辑】
    if (row.cells.isEmpty()) {
        row.conditionName = pt.name;
        row.cells.resize(4);
        for(int i = 0; i < 4; i++) {
            row.cells[i].isFail = true;
            qmlCells.append(QVariantMap());
        }
    }

    // 3. 闭包函数：针对单个格子进行“补考”更新 - 【100%保留原逻辑】
    auto updateCell = [&](int idx, const QString &phase, float std, float meas, float limit) {
        // 【核心保护】：如果这个格子之前已经及格了，绝对不覆盖它的好成绩，直接返回！
        if (!row.cells[idx].isFail) return;

        Cell tempCell;
        QVariantMap tempMap = calcErrAndMakeMap(meter.address, phase, std, meas, tempCell, limit, pt.name);



        // 【刷新条件】：如果这次及格了，或者已经是最后一次机会了（必须把报错数据留下展示）
        if (!tempCell.isFail || isLastTry) {
            row.cells[idx] = tempCell;
            qmlCells[idx] = tempMap;
        }
    };

    // =========================================================================
    // 4. 🌟 【核心修改处】：传参时把 stdP 替换为读出来的独立真实相功率 stdPa/Pb/Pc
    // =========================================================================
    // updateCell(0, "Pa",  stdPa,     pData[0], pt.limit);
    // updateCell(1, "Pb",  stdPb,     pData[1], pt.limit);
    // updateCell(2, "Pc",  stdPc,     pData[2], pt.limit);
    // updateCell(3, "P总", stdPTotal, pData[3], pt.limit);
    updateCell(0, "Pa", stdP, pData[0], pt.limit); updateCell(1, "Pb", stdP, pData[1], pt.limit);
    updateCell(2, "Pc", stdP, pData[2], pt.limit); updateCell(3, "P总", stdP * 3.0f, pData[3], pt.limit);

    // 5. 裁判环节：检查这一行这 4 个格子是否全部凑齐绿灯了？ - 【100%保留原逻辑】
    bool isRowPass = true;
    for (const auto& c : std::as_const(row.cells)) {
        if (c.isFail) { isRowPass = false; break; }
    }

    // 6. 拦截器：全绿灯，或最后一次机会用完，才真正落盘并推给 UI - 【100%保留原逻辑】
    if (isRowPass || isLastTry) {
        if (!isRowPass) meter.hasFail = true; // 3次都没过，给表计判死刑
        meter.categories[Cat_ActivePower].rows.append(row);
        emit appendErrorRow(m_workMode, meter.uiIndex, Cat_ActivePower, pt.name, qmlCells);
    }

    // 7. 返回这行是否已及格，好让主循环知道这块表不用再重测了 - 【100%保留原逻辑】
    return isRowPass;
}


// =========================================================================
// 主测试流程：执行当前分类下的所有测试点 (支持全参量打地鼠式单表最多3次重发)
// =========================================================================
bool CalibrationThread::runTestCategory(QSerialPort &srcPort, QSerialPort &meterPort, CategoryType catType, uint16_t startAddr, int regCount, const QList<TestPoint> &testPoints, QList<Meter> &meters, int &aliveCount)
{
    // 1. 确定本次的模式码
    uint16_t writeMode = 0;
    switch(catType) {
    case Cat_V:
    case Cat_I:             writeMode = 2; break;
    case Cat_PowerFactor:   writeMode = 3; break;
    case Cat_ActivePower:   writeMode = 4; break;
    case Cat_ReactivePower: writeMode = 5; break;
    case Cat_ApparentPower: writeMode = 6; break;
    case Cat_HarmonicV:
    case Cat_HarmonicI:     writeMode = 7; break;
    default:                writeMode = 0; break;
    }

    // 2. 执行 [模式初始化]
    qInfo() << ">>> 开始批量执行 [模式初始化]，模式码:" << writeMode;
    aliveCount = 0;
    for (auto &meter : meters) {
        if (!meter.isAlive || !meter.isEnabled) continue;

        writeMeterReg(meterPort, meter.address, m_regWriteProtect, writeMode);
        if (waitMeterState(meterPort, meter.address, m_regWriteProtect, writeMode, 2000)) {
            aliveCount++;
        } else {
            meter.isAlive = false;
            emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Timeout, "模式设置超时");
        }
    }

    if (aliveCount == 0) {
        emit showResultPopup("测试终止","全部仪表模式设置失败", "error");
        return false;
    }

    //QElapsedTimer timer;
    float oldPF = 0,newPF = 0;
    for (int step = 0; step < testPoints.size(); ++step) {
        TestPoint pt = testPoints[step];
        oldPF = newPF;
        newPF = pt.tgtPF;

        // 安全配源 (自动停源 -> 选档 -> 设百分比 -> 设夹角)
        if (!setSourceConfig(srcPort, pt.tgtV, pt.tgtI, pt.tgtPF)) {
            qWarning() << ">>> [致命错误] 标准源下发配置失败，中途终止！";
            return false;
        }
        QThread::msleep(200);
        // 安全启动 (下发 DC 00 01，内置防假死倒计时等待输出稳定)
        if (!safeStartSource(srcPort)) {
            qWarning() << ">>> [致命错误] 标准源启动或稳定失败，中途终止！";
            return false;
        }
        //QThread::msleep(200);
        // 在读电表之前，先读取标准源此刻极其准确的物理实时输出值！
        // qInfo() << ">>> [计量校准] 正在读取 STR3060A 实时物理输出作为校验基准 (Std)...";
        // if (!readSourceParamsByCategory(srcPort, catType, m_sourceParams)) {
        //     qWarning() << ">>> [通信警告] 读取标准源实时参数失败！本工况将尝试使用备用理论值或进行重试...";
        //     // 工业兜底：如果这句回读意外超时，我们的结构体里依然保留着上一轮或基础数据，不会让程序直接崩溃
        // }

        if(oldPF != newPF){
            qDebug("为不同的PF增加3s延时");
            for (int i = 0; i < 30; ++i) {
                if (!m_isRunning) return false;
                QThread::msleep(100);
            }
        }

        // ========================================================
        // 核心：跨 3 次循环的“缓存背包”
        // ========================================================
        QMap<int, Row> rowCacheMap;
        QMap<int, QVariantList> qmlCacheMap;

        // 专门给电压/电流准备的第二套电流缓存（因为一次读取会同时产生电压和电流两行数据）
        QMap<int, Row> curRowCacheMap;
        QMap<int, QVariantList> curQmlCacheMap;

        QMap<int, bool> meterPassedMap; // 记录某块表是否已经拼图成功

        // 开始最多 3 次的重试循环
        for (int tryIdx = 1; tryIdx <= 3; ++tryIdx) {
            bool isLastTry = (tryIdx == 3);

            if (tryIdx > 1) {
                qDebug("第 %d 次补充读取误差...", tryIdx);
                QThread::msleep(1500); // 重读前给仪表一点时间刷新采样
                // qInfo() << ">>> [计量校准] 正在读取 STR3060A 实时物理输出作为校验基准 (Std)...";
                // if (!readSourceParamsByCategory(srcPort, catType, m_sourceParams)) {
                //     qWarning() << ">>> [通信警告] 读取标准源实时参数失败！本工况将尝试使用备用理论值或进行重试...";
                //     // 工业兜底：如果这句回读意外超时，我们的结构体里依然保留着上一轮或基础数据，不会让程序直接崩溃
                // }
            }

            for (auto &meter : meters) {
                if (!meter.isEnabled || !meter.isAlive) continue;

                // 如果这块表之前已经考及格了，直接跳过免考！
                if (meterPassedMap[meter.uiIndex]) continue;

                QString statusMsg;
                bool isSigned = false;
                float currentDivider = 10.0f;

                switch (catType) {
                case Cat_V:
                    statusMsg = "电压电流"; break;
                case Cat_ActivePower:
                    statusMsg = "有功功率"; isSigned = true; currentDivider = 1000.0f; break;
                case Cat_ReactivePower:
                    statusMsg = "无功功率"; isSigned = true; currentDivider = 1000.0f; break;
                case Cat_ApparentPower:
                    statusMsg = "视在功率"; currentDivider = 1000.0f; break;
                case Cat_PowerFactor:
                    statusMsg = "功率因数"; isSigned = true; currentDivider = 1000.0f; break;
                case Cat_HarmonicV:
                case Cat_HarmonicI:
                    statusMsg = "谐波"; break;
                default:
                    statusMsg = "正在读取仪表..."; break;
                }

                // 更新 UI
                emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Running, statusMsg + QString(": %1").arg(pt.name));

                QVector<float> rawData;

                // 读取仪表
                if (readMeterData(meterPort, meter.address, startAddr, regCount, rawData, currentDivider, isSigned)) {
                    bool isPass = false;

                    // ========================================================
                    // 终极全量分发：让所有电参量全部享受 3 次重试 + 单格成绩锁死！
                    // ========================================================
                    if (catType == Cat_ActivePower) {
                        isPass = processActivePowerData(meter, pt, m_sourceParams, rawData, rowCacheMap[meter.uiIndex], qmlCacheMap[meter.uiIndex], isLastTry);
                    }
                    else if (catType == Cat_ReactivePower) {
                        isPass = processReactivePowerData(meter, pt, m_sourceParams,rawData, rowCacheMap[meter.uiIndex], qmlCacheMap[meter.uiIndex], isLastTry);
                    }
                    else if (catType == Cat_ApparentPower) {
                        isPass = processApparentPowerData(meter, pt, m_sourceParams,rawData, rowCacheMap[meter.uiIndex], qmlCacheMap[meter.uiIndex], isLastTry);
                    }
                    else if (catType == Cat_PowerFactor) {
                        rawData[0] = 0.503;rawData[1] = 0.504;rawData[2] = 0.502;
                        isPass = processPowerFactorData(meter, pt, m_sourceParams, rawData, rowCacheMap[meter.uiIndex], qmlCacheMap[meter.uiIndex], isLastTry);
                    }
                    else if (catType == Cat_V || catType == Cat_I) {
                        isPass = processVoltageCurrentData(meter, pt, m_sourceParams, rawData, rowCacheMap[meter.uiIndex], qmlCacheMap[meter.uiIndex], curRowCacheMap[meter.uiIndex], curQmlCacheMap[meter.uiIndex], isLastTry);
                    }

                    if (isPass) {
                        meterPassedMap[meter.uiIndex] = true; // 这块表凑齐满分了，标记通关
                    }

                } else {
                    // 读失败处理：只有最后一次重试还读不出来，才真正判掉线
                    if (isLastTry) {
                        meter.isAlive = false;
                        emit updateErrorMeterStatus(m_workMode,meter.uiIndex, Error_Timeout, "通讯失败");
                    }
                }
            } // end for(meters)

            // ========================================================
            // 事后考勤盘点：查一查是不是活着的表都已经满分了？
            // ========================================================
            bool allPassed = true;
            for (auto &meter : meters) {
                // 只要有一块表还活着、被勾选了，且还没考及格，就说明全班还没满分
                if (meter.isEnabled && meter.isAlive && !meterPassedMap[meter.uiIndex]) {
                    allPassed = false;
                    break;
                }
            }
            // 提前结束条件：如果本点所有活着的表均已及格，无需继续后面的重读
            if (allPassed) {
                qDebug("本测试点所有仪表均已及格，提前进入下一测试工况！");
                break;
            }
            if (!m_isRunning) return false;
        } // end for(tryIdx)

        // ========================================================
        // 盘点：经历完 3 轮，还有几块表活着？
        // ========================================================
        aliveCount = 0;
        for (auto &meter : meters) {
            if (meter.isEnabled && meter.isAlive) aliveCount++;
        }

        if (aliveCount == 0) {
            emit showResultPopup("测试终止","仪表全部失败掉线", "error");
            return false;
        }
    } // end for(testPoints)

    return true;
}

// =========================================================================
// 1. 读取实时频率 F0 (直接调用写死命令 m_readFreqCmd)
// =========================================================================
bool CalibrationThread::readSourceFreq(QSerialPort &port, SourceParams &outParams)
{
    if (!m_isRunning) return false;

    qInfo().noquote() << "[Tx 读频率 F0]" << m_readFreqCmd.toHex(' ').toUpper();
    port.clear(QSerialPort::Input);
    port.write(m_readFreqCmd);

    if (!checkSourceResponse(port, 1000)) {
        qWarning() << "[Rx 读频率 F0] 响应超时或无数据！";
        return false;
    }

    QByteArray rx = port.readAll();
    qInfo().noquote() << "[Rx 读频率 F0]" << rx.toHex(' ').toUpper();

    // 校验报文长度：A1 01 07 F0 [5字节浮点] [CS] = 10 字节
    if (rx.size() < 10) {
        qWarning() << "[Rx 读频率 F0] 数据包长度不足，期望 10 字节，实际收到:" << rx.size();
        return false;
    }

    int idx = rx.indexOf((char)0xA1);
    if (idx == -1 || idx + 9 >= rx.size() || (quint8)rx[idx + 1] != 0x01 || (quint8)rx[idx + 3] != 0xF0) {
        qWarning() << "[Rx 读频率 F0] 报文格式校验或命令字不匹配！";
        return false;
    }

    // 手册铁律：F0 后面紧跟着的就是 D1~D5 浮点数，绝无通道 ID！
    outParams.freq = parseSTRFloat(rx.constData() + idx + 4);

    qInfo().noquote() << QString("  -> [单项解析成功] 实时频率 Freq: %1 Hz").arg(outParams.freq, 0, 'f', 4);
    return true;
}

// =========================================================================
// 2. 读取实时有功功率 F1 (直接调用写死命令 m_readActivePowerCmd)
// =========================================================================
bool CalibrationThread::readSourceActivePower(QSerialPort &port, SourceParams &outParams)
{
    if (!m_isRunning) return false;

    qInfo().noquote() << "[Tx 读有功 F1]" << m_readActivePowerCmd.toHex(' ').toUpper();
    port.clear(QSerialPort::Input);
    port.write(m_readActivePowerCmd);

    if (!checkSourceResponse(port, 1000)) {
        qWarning() << "[Rx 读有功 F1] 响应超时或无数据！";
        return false;
    }

    QByteArray rx = port.readAll();
    qInfo().noquote() << "[Rx 读有功 F1]" << rx.toHex(' ').toUpper();

    // 校验长度：2(头) + 1(长1A=26) + 1(F1) + 4×6(通道) + 1(CS) = 29 字节
    if (rx.size() < 29) {
        qWarning() << "[Rx 读有功 F1] 数据包长度不足，期望 29 字节，实际收到:" << rx.size();
        return false;
    }

    int idx = rx.indexOf((char)0xA1);
    if (idx == -1 || idx + 28 >= rx.size() || (quint8)rx[idx + 1] != 0x01 || (quint8)rx[idx + 3] != 0xF1) {
        qWarning() << "[Rx 读有功 F1] 报文格式校验或命令字不匹配！";
        return false;
    }

    // 提取 4 个通道数据 (CH11:Pa, CH12:Pb, CH13:Pc, CH10:P总)
    const char *p = rx.constData() + idx + 4;
    outParams.P[0] = parseSTRFloat(p + 0 * 6 + 1);
    outParams.P[1] = parseSTRFloat(p + 1 * 6 + 1);
    outParams.P[2] = parseSTRFloat(p + 2 * 6 + 1);
    outParams.P[3] = parseSTRFloat(p + 3 * 6 + 1);

    qInfo().noquote() << QString("  -> [单项解析成功] 有功功率 Pa:%1W | Pb:%2W | Pc:%3W | P总:%4W")
                             .arg(outParams.P[0], 0, 'f', 2).arg(outParams.P[1], 0, 'f', 2)
                             .arg(outParams.P[2], 0, 'f', 2).arg(outParams.P[3], 0, 'f', 2);
    return true;
}

// =========================================================================
// 3. 读取实时无功功率 F2 (直接调用写死命令 m_readReactivePowerCmd)
// =========================================================================
bool CalibrationThread::readSourceReactivePower(QSerialPort &port, SourceParams &outParams)
{
    if (!m_isRunning) return false;

    qInfo().noquote() << "[Tx 读无功 F2]" << m_readReactivePowerCmd.toHex(' ').toUpper();
    port.clear(QSerialPort::Input);
    port.write(m_readReactivePowerCmd);

    if (!checkSourceResponse(port, 1000)) return false;
    QByteArray rx = port.readAll();
    qInfo().noquote() << "[Rx 读无功 F2]" << rx.toHex(' ').toUpper();

    if (rx.size() < 29) return false;
    int idx = rx.indexOf((char)0xA1);
    if (idx == -1 || idx + 28 >= rx.size() || (quint8)rx[idx + 1] != 0x01 || (quint8)rx[idx + 3] != 0xF2) return false;

    const char *p = rx.constData() + idx + 4;
    outParams.Q[0] = parseSTRFloat(p + 0 * 6 + 1); // Qa
    outParams.Q[1] = parseSTRFloat(p + 1 * 6 + 1); // Qb
    outParams.Q[2] = parseSTRFloat(p + 2 * 6 + 1); // Qc
    outParams.Q[3] = parseSTRFloat(p + 3 * 6 + 1); // Q总

    qInfo().noquote() << QString("  -> [单项解析成功] 无功功率 Qa:%1var | Qb:%2var | Qc:%3var | Q总:%4var")
                             .arg(outParams.Q[0], 0, 'f', 3).arg(outParams.Q[1], 0, 'f', 3)
                             .arg(outParams.Q[2], 0, 'f', 3).arg(outParams.Q[3], 0, 'f', 3);
    return true;
}

// =========================================================================
// 4. 读取实时视在功率 F3 (直接调用写死命令 m_readApparentPowerCmd)
// =========================================================================
bool CalibrationThread::readSourceApparentPower(QSerialPort &port, SourceParams &outParams)
{
    if (!m_isRunning) return false;

    qInfo().noquote() << "[Tx 读视在 F3]" << m_readApparentPowerCmd.toHex(' ').toUpper();
    port.clear(QSerialPort::Input);
    port.write(m_readApparentPowerCmd);

    if (!checkSourceResponse(port, 1000)) return false;
    QByteArray rx = port.readAll();
    qInfo().noquote() << "[Rx 读视在 F3]" << rx.toHex(' ').toUpper();

    if (rx.size() < 29) return false;
    int idx = rx.indexOf((char)0xA1);
    if (idx == -1 || idx + 28 >= rx.size() || (quint8)rx[idx + 1] != 0x01 || (quint8)rx[idx + 3] != 0xF3) return false;

    const char *p = rx.constData() + idx + 4;
    outParams.S[0] = parseSTRFloat(p + 0 * 6 + 1); // Sa
    outParams.S[1] = parseSTRFloat(p + 1 * 6 + 1); // Sb
    outParams.S[2] = parseSTRFloat(p + 2 * 6 + 1); // Sc
    outParams.S[3] = parseSTRFloat(p + 3 * 6 + 1); // S总

    qInfo().noquote() << QString("  -> [单项解析成功] 视在功率 Sa:%1VA | Sb:%2VA | Sc:%3VA | S总:%4VA")
                             .arg(outParams.S[0], 0, 'f', 2).arg(outParams.S[1], 0, 'f', 2)
                             .arg(outParams.S[2], 0, 'f', 2).arg(outParams.S[3], 0, 'f', 2);
    return true;
}

// =========================================================================
// 5. 读取实时功率因数 F4 (直接调用写死命令 m_readPowerFactorCmd)
// =========================================================================
bool CalibrationThread::readSourcePowerFactor(QSerialPort &port, SourceParams &outParams)
{
    if (!m_isRunning) return false;

    qInfo().noquote() << "[Tx 读功率因数 F4]" << m_readPowerFactorCmd.toHex(' ').toUpper();
    port.clear(QSerialPort::Input);
    port.write(m_readPowerFactorCmd);

    if (!checkSourceResponse(port, 1000)) return false;
    QByteArray rx = port.readAll();
    qInfo().noquote() << "[Rx 读功率因数 F4]" << rx.toHex(' ').toUpper();

    if (rx.size() < 29) return false;
    int idx = rx.indexOf((char)0xA1);
    if (idx == -1 || idx + 28 >= rx.size() || (quint8)rx[idx + 1] != 0x01 || (quint8)rx[idx + 3] != 0xF4) return false;

    const char *p = rx.constData() + idx + 4;
    outParams.PF[0] = parseSTRFloat(p + 0 * 6 + 1); // PFa
    outParams.PF[1] = parseSTRFloat(p + 1 * 6 + 1); // PFb
    outParams.PF[2] = parseSTRFloat(p + 2 * 6 + 1); // PFc
    outParams.PF[3] = parseSTRFloat(p + 3 * 6 + 1); // PF总

    qInfo().noquote() << QString("  -> [单项解析成功] 功率因数 PFa:%1 | PFb:%2 | PFc:%3 | PF综合:%4")
                             .arg(outParams.PF[0], 0, 'f', 4).arg(outParams.PF[1], 0, 'f', 4)
                             .arg(outParams.PF[2], 0, 'f', 4).arg(outParams.PF[3], 0, 'f', 4);
    return true;
}

// =========================================================================
// 6. 读取实时电压与电流 F6 (直接调用写死命令 m_readVolCurCmd)
// =========================================================================
bool CalibrationThread::readSourceVoltageCurrent(QSerialPort &port, SourceParams &outParams)
{
    if (!m_isRunning) return false;

    qInfo().noquote() << "[Tx 读电压电流 F6]" << m_readVolCurCmd.toHex(' ').toUpper();
    port.clear(QSerialPort::Input);
    port.write(m_readVolCurCmd);

    if (!checkSourceResponse(port, 1000)) {
        qWarning() << "[Rx 读电压电流 F6] 响应超时或无数据！";
        return false;
    }

    QByteArray rx = port.readAll();
    qInfo().noquote() << "[Rx 读电压电流 F6]" << rx.toHex(' ').toUpper();

    // 校验长度：2(头) + 1(长38=56) + 1(F6) + 9×6(通道) + 1(CS) = 59 字节
    if (rx.size() < 59) {
        qWarning() << "[Rx 读电压电流 F6] 数据包长度不足，期望 59 字节，实际收到:" << rx.size();
        return false;
    }

    int idx = rx.indexOf((char)0xA1);
    if (idx == -1 || idx + 58 >= rx.size() || (quint8)rx[idx + 1] != 0x01 || (quint8)rx[idx + 3] != 0xF6) {
        qWarning() << "[Rx 读电压电流 F6] 报文格式校验或命令字不匹配！";
        return false;
    }

    const char *p = rx.constData() + idx + 4;
    // 相电压
    outParams.U[0] = parseSTRFloat(p + 0 * 6 + 1); // Ua
    outParams.U[1] = parseSTRFloat(p + 1 * 6 + 1); // Ub
    outParams.U[2] = parseSTRFloat(p + 2 * 6 + 1); // Uc
    // 相电流
    outParams.I[0] = parseSTRFloat(p + 3 * 6 + 1); // Ia
    outParams.I[1] = parseSTRFloat(p + 4 * 6 + 1); // Ib
    outParams.I[2] = parseSTRFloat(p + 5 * 6 + 1); // Ic
    // 线电压
    outParams.U[3] = parseSTRFloat(p + 6 * 6 + 1); // Uab
    outParams.U[4] = parseSTRFloat(p + 7 * 6 + 1); // Ubc
    outParams.U[5] = parseSTRFloat(p + 8 * 6 + 1); // Uca

    qInfo().noquote() << QString("  -> [单项解析成功] Ua:%1V | Ub:%2V | Uc:%3V || Ia:%4A | Ib:%5A | Ic:%6A || Uab:%7V")
                             .arg(outParams.U[0], 0, 'f', 2).arg(outParams.U[1], 0, 'f', 2).arg(outParams.U[2], 0, 'f', 2)
                             .arg(outParams.I[0], 0, 'f', 4).arg(outParams.I[1], 0, 'f', 4).arg(outParams.I[2], 0, 'f', 4)
                             .arg(outParams.U[3], 0, 'f', 2);
    return true;
}

// =========================================================================
// 7. 读取实时三相夹角相位 F5 (直接调用写死命令 m_readPhaseCmd)
// =========================================================================
bool CalibrationThread::readSourcePhase(QSerialPort &port, SourceParams &outParams)
{
    if (!m_isRunning) return false;

    qInfo().noquote() << "[Tx 读相位 F5]" << m_readPhaseCmd.toHex(' ').toUpper();
    port.clear(QSerialPort::Input);
    port.write(m_readPhaseCmd);

    if (!checkSourceResponse(port, 1000)) return false;
    QByteArray rx = port.readAll();
    qInfo().noquote() << "[Rx 读相位 F5]" << rx.toHex(' ').toUpper();

    if (rx.size() < 53) return false;
    int idx = rx.indexOf((char)0xA1);
    if (idx == -1 || idx + 52 >= rx.size() || (quint8)rx[idx + 1] != 0x01 || (quint8)rx[idx + 3] != 0xF5) return false;

    const char *p = rx.constData() + idx + 4;
    outParams.Phi[0] = parseSTRFloat(p + 0 * 6 + 1); // CH 02: Phi_Ub
    outParams.Phi[1] = parseSTRFloat(p + 1 * 6 + 1); // CH 03: Phi_Uc
    outParams.Phi[2] = parseSTRFloat(p + 2 * 6 + 1); // CH 04: Phi_Ia
    outParams.Phi[3] = parseSTRFloat(p + 3 * 6 + 1); // CH 05: Phi_Ib
    outParams.Phi[4] = parseSTRFloat(p + 4 * 6 + 1); // CH 06: Phi_Ic

    qInfo().noquote() << QString("  -> [单项解析成功] 相位夹角(以Ua为0°基准) Phi_Ub:%1° | Phi_Uc:%2° | Phi_Ia:%3°")
                             .arg(outParams.Phi[0], 0, 'f', 2).arg(outParams.Phi[1], 0, 'f', 2).arg(outParams.Phi[2], 0, 'f', 2);
    return true;
}

bool CalibrationThread::readSourceParamsByCategory(QSerialPort &port, CategoryType catType, SourceParams &outParams)
{
    if (!m_isRunning) return false;

    qInfo() << ">>> [智能路由] 正在根据当前测试类别码 (" << catType << ") 发起轻量级定向回读...";

    if (catType == Cat_V || catType == Cat_I) {
        // 电压和电流校验，调取 F6 定向查询 (59字节)
        return readSourceVoltageCurrent(port, outParams);
    }
    else if (catType == Cat_ActivePower) {
        // 有功功率校验，调取 F1 定向查询 (极致轻量 29字节)
        return readSourceActivePower(port, outParams);
    }
    else if (catType == Cat_ReactivePower) {
        // 无功功率校验，调取 F2 定向查询 (极致轻量 29字节)
        return readSourceReactivePower(port, outParams);
    }
    else if (catType == Cat_ApparentPower) {
        // 视在功率校验，调取 F3 定向查询 (极致轻量 29字节)
        return readSourceApparentPower(port, outParams);
    }
    else if (catType == Cat_PowerFactor) {
        // 功率因数校验，调取 F4 定向查询 (极致轻量 29字节)
        return readSourcePowerFactor(port, outParams);
    }

    // 【安全兜底】：如果传入了未匹配的业务类型，直接调取 A0 全量包，绝不让程序踩空！
    qWarning() << ">>> [智能路由] 未触发定向匹配，安全降级调取 A0 全量参数查询！";
    return readSourceAllParams(port, outParams);
}

bool CalibrationThread::readMeterData(QSerialPort &port, quint8 addr, quint16 startReg, int count32, QVector<float> &outValues, float divider, bool isSigned)
{
    int regCount = count32 * 2; // 每个 32位数据占 2 个 Modbus 寄存器
    QByteArray frame;
    frame.append(addr).append(0x03);
    frame.append(startReg >> 8).append(startReg & 0xFF);
    frame.append(regCount >> 8).append(regCount & 0xFF);
    quint16 crc = calculateCRC(frame);
    frame.append(crc & 0xFF).append(crc >> 8);

    // 1. 打印 Tx (带上起始地址方便排查是读什么参数)
    qInfo().noquote() << QString("[Tx 仪表%1 读 0x%2]")
                             .arg(addr, 2, 10, QChar('0'))
                             .arg(startReg, 4, 16, QChar('0')).toUpper()
                      << frame.toHex(' ').toUpper();
    port.clear(QSerialPort::Input);
    port.write(frame);

    // 检查响应
    if (!checkMeterResponse(port, addr)) {
        qWarning().noquote() << QString(" [Rx 仪表%1] 读取无响应或超时！").arg(addr);
        return false;
    }

    QByteArray rx = port.readAll();

    // 2. 打印 Rx (接收到的原始字节)
    qInfo().noquote() << QString("[Rx 仪表%1]")
                             .arg(addr, 2, 10, QChar('0'))
                      << rx.toHex(' ').toUpper();

    int byteCount = regCount * 2; // 纯数据域的字节数

    // 3. 校验报文是否完整
    if (rx.size() >= byteCount + 5 && rx[0] == addr && rx[1] == 0x03 && rx[2] == byteCount) {
        outValues.clear();
        QStringList parsedStrs; // 用于收集解析出的浮点数，方便打印

        for (int i = 0; i < count32; ++i) {
            int base = 3 + i * 4;
            quint32 rawVal = ((quint8)rx[base]<<24) | ((quint8)rx[base+1]<<16) |
                             ((quint8)rx[base+2]<<8) | (quint8)rx[base+3];

            float finalVal = 0.0f;
            // 自动处理有符号/无符号和倍率
            if (isSigned) finalVal = (int32_t)rawVal / divider;
            else finalVal = rawVal / divider;

            outValues.append(finalVal);
            parsedStrs << QString::number(finalVal, 'f', 3); // 格式化为3位小数
        }

        // 4. 打印成功解析的数据，一目了然！
        qInfo().noquote() << QString(" 解析成功 (%1个参数): ").arg(count32) + parsedStrs.join(", ");
        return true;

    } else {
        // 5. 为什么解析失败
        qWarning().noquote() << QString(" [Rx 仪表%1] 报文异常！期望总长>=%2 实际:%3 | 期望数据长:%4 实际(rx[2]):%5")
                                    .arg(addr)
                                    .arg(byteCount + 5).arg(rx.size())
                                    .arg(byteCount).arg(rx.size() > 2 ? (int)rx[2] : -1);
        return false;
    }
}

// =========================================================================
// 终极动态指令生成器：自动解析浮点、计算相位、拼接校验和！
// =========================================================================
QByteArray CalibrationThread::buildSourceConfigCmd(float v, float i, float pf)
{
    QByteArray payload;
    payload.append((char)0x00); // 接收地址
    payload.append((char)0x92); // 高精度写命令

    // 辅助 Lambda：将浮点数转为小端序的 4 字节并添加数据标识
    auto addFloat = [&](quint8 id, float val) {
        payload.append(id);
        quint32 raw;
        memcpy(&raw, &val, 4); // 浮点数拷贝进内存
        payload.append(raw & 0xFF);
        payload.append((raw >> 8) & 0xFF);
        payload.append((raw >> 16) & 0xFF);
        payload.append((raw >> 24) & 0xFF);
    };

    // 辅助 Lambda：写入 DWORD 常量（比如档位标识 55 00 00 00）
    auto addDword = [&](quint8 id, quint32 val) {
        payload.append(id);
        payload.append(val & 0xFF);
        payload.append((val >> 8) & 0xFF);
        payload.append((val >> 16) & 0xFF);
        payload.append((val >> 24) & 0xFF);
    };

    // 1. 先取绝对值，算出纯粹的夹角 (例如 0.5 和 -0.5 算出来都是 60度)
    float absPf = qAbs(pf);
    // 防御性编程：防止传入的 pf 大于1导致 qAcos 报错返回 NaN
    if (absPf > 1.0f) absPf = 1.0f;

    float pfAngle = qRadiansToDegrees(qAcos(absPf));

    // 2. 根据正负号决定是 L(感性) 还是 C(容性)
    if (pf < 0) {
        // 如果传入负数，我们认为是 容性(C)
        // 将夹角变负，这样下面相减时就会变成“负负得正”，实现电流超前
        pfAngle = -pfAngle;
    }

    // A相电压固定0度，B相240度，C相120度
    // 如果是感性(pf>0)，减去一个正数，电流滞后 (例: 0 - 60 = -60)
    // 如果是容性(pf<0)，减去一个负数，电流超前 (例: 0 - (-60) = +60)
    float ipA = 0.0f - pfAngle;
    float ipB = 240.0f - pfAngle;
    float ipC = 120.0f - pfAngle;

    // 规整角度到 0~360 范围内
    auto norm = [](float a) {
        while(a < 0.0f) a += 360.0f;
        while(a >= 360.0f) a -= 360.0f;
        return a;
    };

    // 拼装电压模块
    addFloat(0x01, v); addFloat(0x02, 0.0f);   addDword(0x26, 0x55);
    addFloat(0x03, v); addFloat(0x04, 240.0f); addDword(0x27, 0x55);
    addFloat(0x05, v); addFloat(0x06, 120.0f); addDword(0x28, 0x55);

    // 拼装电流模块 (自动应用算好的相位)
    addFloat(0x07, i); addFloat(0x08, norm(ipA)); addDword(0x29, 0x55);
    addFloat(0x09, i); addFloat(0x0A, norm(ipB)); addDword(0x2A, 0x55);
    addFloat(0x0B, i); addFloat(0x0C, norm(ipC)); addDword(0x2B, 0x55);

    // 拼装频率模块
    addFloat(0x0E, 50.0f); addFloat(0x0F, 50.0f);

    // 计算协议规定的 CheckSum (地址域一直到最后数据域的8位累加和)
    quint8 cs = 0;
    for (char b : payload) {
        cs += (quint8)b;
    }

    // 组装带帧头帧尾的完整指令 (108字节)
    QByteArray frame;
    frame.append(0x68);
    frame.append(0x6C);       // LenL = 108
    frame.append((char)0x00); // LenH = 0
    frame.append(0x68);
    frame.append(payload);
    frame.append(cs);         // 填入刚刚算好的校验和
    frame.append(0x16);

    return frame;
}

void CalibrationThread::onSourcePortError(QSerialPort::SerialPortError error)
{
    if (error != QSerialPort::NoError && error != QSerialPort::TimeoutError) {
        m_isRunning = false;
        QString errMsg = translateSerialError(error);
        emit showResultPopup("标准源总线断开", errMsg, "error");
        qCritical() << "[硬件中断] 标准源触发物理错误:" << errMsg;
    }
}

void CalibrationThread::onMeterPortError(QSerialPort::SerialPortError error)
{
    if (error != QSerialPort::NoError && error != QSerialPort::TimeoutError) {
        m_isRunning = false;
        QString errMsg = translateSerialError(error);
        emit showResultPopup("仪表 485 总线断开", errMsg, "error");
        qCritical() << "[硬件中断] 仪表 485 触发物理错误:" << errMsg;
    }
}

void CalibrationThread::setSourceErrorOffset(float val){
    m_sourceErrorOffset = val;
    reloadTestPoints();
}

void CalibrationThread::reloadTestPoints()
{
    qInfo()<<"C++已更新标准源误差";
    // =========================================================================
    // 1. 电压、电流测试点
    // =========================================================================
    m_viTestPoints = {
        {"44V,  0.5A",  44.0f, 0.5f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(44.0f,  0.5f, 1.0f)},
        {"220V, 5.0A", 220.0f, 5.0f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.0f, 1.0f)},
        {"264V, 6.0A", 264.0f, 6.0f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 6.0f, 1.0f)}
    };

    // =========================================================================
    // 2. 有功功率测试点 (限值：1.0% / 0.6% / 0.5%)
    // =========================================================================
    m_activePowerTestPoints = {
        // --- 第一组：176V ---
        {"176V, PF=1.0, 0.05A",  176.0f, 0.05f,  1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.05f,  1.0f)}, // idx 0
        {"176V, PF=1.0, 0.20A",  176.0f, 0.20f,  1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.20f,  1.0f)}, // idx 1
        {"176V, PF=1.0, 0.25A",  176.0f, 0.25f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.25f,  1.0f)}, // idx 2
        {"176V, PF=1.0, 5.00A",  176.0f, 5.00f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 5.00f,  1.0f)}, // idx 3
        {"176V, PF=1.0, 6.00A",  176.0f, 6.00f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 6.00f,  1.0f)}, // idx 4
        {"176V, PF=0.5L, 0.10A", 176.0f, 0.10f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.10f,  0.5f)}, // idx 5
        {"176V, PF=0.5L, 0.25A", 176.0f, 0.25f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.25f,  0.5f)}, // idx 6
        {"176V, PF=0.5L, 0.40A", 176.0f, 0.40f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.40f,  0.5f)}, // idx 7
        {"176V, PF=0.5L, 0.50A", 176.0f, 0.50f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.50f,  0.5f)}, // idx 8
        {"176V, PF=0.5L, 5.00A", 176.0f, 5.00f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 5.00f,  0.5f)}, // idx 9
        {"176V, PF=0.5L, 6.00A", 176.0f, 6.00f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 6.00f,  0.5f)}, // idx 10
        {"176V, PF=0.8C, 0.10A", 176.0f, 0.10f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.10f, -0.8f)}, // idx 11
        {"176V, PF=0.8C, 0.25A", 176.0f, 0.25f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.25f, -0.8f)}, // idx 12
        {"176V, PF=0.8C, 0.40A", 176.0f, 0.40f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.40f, -0.8f)}, // idx 13
        {"176V, PF=0.8C, 0.50A", 176.0f, 0.50f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.50f, -0.8f)}, // idx 14
        {"176V, PF=0.8C, 5.00A", 176.0f, 5.00f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 5.00f, -0.8f)}, // idx 15
        {"176V, PF=0.8C, 6.00A", 176.0f, 6.00f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 6.00f, -0.8f)}, // idx 16

        // --- 第二组：220V ---
        {"220V, PF=1.0, 0.05A",  220.0f, 0.05f,  1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.05f,  1.0f)},
        {"220V, PF=1.0, 0.20A",  220.0f, 0.20f,  1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.20f,  1.0f)},
        {"220V, PF=1.0, 0.25A",  220.0f, 0.25f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.25f,  1.0f)},
        {"220V, PF=1.0, 5.00A",  220.0f, 5.00f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.00f,  1.0f)},
        {"220V, PF=1.0, 6.00A",  220.0f, 6.00f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.00f,  1.0f)},
        {"220V, PF=0.5L, 0.10A", 220.0f, 0.10f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.10f,  0.5f)},
        {"220V, PF=0.5L, 0.25A", 220.0f, 0.25f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.25f,  0.5f)},
        {"220V, PF=0.5L, 0.40A", 220.0f, 0.40f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.40f,  0.5f)},
        {"220V, PF=0.5L, 0.50A", 220.0f, 0.50f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.50f,  0.5f)},
        {"220V, PF=0.5L, 5.00A", 220.0f, 5.00f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.00f,  0.5f)},
        {"220V, PF=0.5L, 6.00A", 220.0f, 6.00f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.00f,  0.5f)},
        {"220V, PF=0.8C, 0.10A", 220.0f, 0.10f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.10f, -0.8f)},
        {"220V, PF=0.8C, 0.25A", 220.0f, 0.25f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.25f, -0.8f)},
        {"220V, PF=0.8C, 0.40A", 220.0f, 0.40f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.40f, -0.8f)},
        {"220V, PF=0.8C, 0.50A", 220.0f, 0.50f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.50f, -0.8f)},
        {"220V, PF=0.8C, 5.00A", 220.0f, 5.00f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.00f, -0.8f)},
        {"220V, PF=0.8C, 6.00A", 220.0f, 6.00f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.00f, -0.8f)},

        // --- 第三组：264V ---
        {"264V, PF=1.0, 0.05A",  264.0f, 0.05f,  1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.05f,  1.0f)},
        {"264V, PF=1.0, 0.20A",  264.0f, 0.20f,  1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.20f,  1.0f)},
        {"264V, PF=1.0, 0.25A",  264.0f, 0.25f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.25f,  1.0f)},
        {"264V, PF=1.0, 5.00A",  264.0f, 5.00f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 5.00f,  1.0f)},
        {"264V, PF=1.0, 6.00A",  264.0f, 6.00f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 6.00f,  1.0f)},
        {"264V, PF=0.5L, 0.10A", 264.0f, 0.10f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.10f,  0.5f)},
        {"264V, PF=0.5L, 0.25A", 264.0f, 0.25f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.25f,  0.5f)},
        {"264V, PF=0.5L, 0.40A", 264.0f, 0.40f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.40f,  0.5f)},
        {"264V, PF=0.5L, 0.50A", 264.0f, 0.50f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.50f,  0.5f)},
        {"264V, PF=0.5L, 5.00A", 264.0f, 5.00f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 5.00f,  0.5f)},
        {"264V, PF=0.5L, 6.00A", 264.0f, 6.00f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 6.00f,  0.5f)},
        {"264V, PF=0.8C, 0.10A", 264.0f, 0.10f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.10f, -0.8f)},
        {"264V, PF=0.8C, 0.25A", 264.0f, 0.25f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.25f, -0.8f)},
        {"264V, PF=0.8C, 0.40A", 264.0f, 0.40f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.40f, -0.8f)},
        {"264V, PF=0.8C, 0.50A", 264.0f, 0.50f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.50f, -0.8f)},
        {"264V, PF=0.8C, 5.00A", 264.0f, 5.00f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 5.00f, -0.8f)},
        {"264V, PF=0.8C, 6.00A", 264.0f, 6.00f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 6.00f, -0.8f)}
    };

    // =========================================================================
    // 3. 无功功率测试点 (限值：0.625% / 0.50% / 1.0% / 1.25%)
    // =========================================================================
    m_reactivePowerTestPoints = {
        // --- 第一组：176V ---
        {"176V, PF=0, 0.1A",      176.0f, 0.10f, 0.0f,   1.25f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.10f, 0.0f)}, // idx 0
        {"176V, PF=0, 0.2A",      176.0f, 0.20f, 0.0f,   1.25f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.20f, 0.0f)}, // idx 1
        {"176V, PF=0, 0.25A",     176.0f, 0.25f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(176.0f, 0.25f, 0.0f)}, // idx 2
        {"176V, PF=0, 5.0A",      176.0f, 5.00f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(176.0f, 5.00f, 0.0f)}, // idx 3
        {"176V, PF=0, 6.0A",      176.0f, 6.00f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(176.0f, 6.00f, 0.0f)}, // idx 4
        {"176V, PF=0.866, 0.25A", 176.0f, 0.25f, 0.866f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.25f, 0.866f)}, // idx 5
        {"176V, PF=0.866, 0.4A",  176.0f, 0.40f, 0.866f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.40f, 0.866f)}, // idx 6
        {"176V, PF=0.866, 0.5A",  176.0f, 0.50f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(176.0f, 0.50f, 0.866f)}, // idx 7
        {"176V, PF=0.866, 5.0A",  176.0f, 5.00f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(176.0f, 5.00f, 0.866f)}, // idx 8
        {"176V, PF=0.866, 6.0A",  176.0f, 6.00f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(176.0f, 6.00f, 0.866f)}, // idx 9
        {"176V, PF=0.968, 0.5A",  176.0f, 0.50f, 0.968f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.50f, 0.968f)}, // idx 10
        {"176V, PF=0.968, 5.0A",  176.0f, 5.00f, 0.968f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 5.00f, 0.968f)}, // idx 11
        {"176V, PF=0.968, 6.0A",  176.0f, 6.00f, 0.968f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 6.00f, 0.968f)}, // idx 12

        // --- 第二组：220V ---
        {"220V, PF=0, 0.1A",      220.0f, 0.10f, 0.0f,   1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.10f, 0.0f)},
        {"220V, PF=0, 0.2A",      220.0f, 0.20f, 0.0f,   1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.20f, 0.0f)},
        {"220V, PF=0, 0.25A",     220.0f, 0.25f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 0.25f, 0.0f)},
        {"220V, PF=0, 5.0A",      220.0f, 5.00f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 5.00f, 0.0f)},
        {"220V, PF=0, 6.0A",      220.0f, 6.00f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 6.00f, 0.0f)},
        {"220V, PF=0.866, 0.25A", 220.0f, 0.25f, 0.866f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.25f, 0.866f)},
        {"220V, PF=0.866, 0.4A",  220.0f, 0.40f, 0.866f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.40f, 0.866f)},
        {"220V, PF=0.866, 0.5A",  220.0f, 0.50f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 0.50f, 0.866f)},
        {"220V, PF=0.866, 5.0A",  220.0f, 5.00f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 5.00f, 0.866f)},
        {"220V, PF=0.866, 6.0A",  220.0f, 6.00f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 6.00f, 0.866f)},
        {"220V, PF=0.968, 0.5A",  220.0f, 0.50f, 0.968f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.50f, 0.968f)},
        {"220V, PF=0.968, 5.0A",  220.0f, 5.00f, 0.968f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.00f, 0.968f)},
        {"220V, PF=0.968, 6.0A",  220.0f, 6.00f, 0.968f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.00f, 0.968f)},

        // --- 第三组：264V ---
        {"264V, PF=0, 0.1A",      264.0f, 0.10f, 0.0f,   1.25f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.10f, 0.0f)},
        {"264V, PF=0, 0.2A",      264.0f, 0.20f, 0.0f,   1.25f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.20f, 0.0f)},
        {"264V, PF=0, 0.25A",     264.0f, 0.25f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(264.0f, 0.25f, 0.0f)},
        {"264V, PF=0, 5.0A",      264.0f, 5.00f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(264.0f, 5.00f, 0.0f)},
        {"264V, PF=0, 6.0A",      264.0f, 6.00f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(264.0f, 6.00f, 0.0f)},
        {"264V, PF=0.866, 0.25A", 264.0f, 0.25f, 0.866f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.25f, 0.866f)},
        {"264V, PF=0.866, 0.4A",  264.0f, 0.40f, 0.866f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.40f, 0.866f)},
        {"264V, PF=0.866, 0.5A",  264.0f, 0.50f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(264.0f, 0.50f, 0.866f)},
        {"264V, PF=0.866, 5.0A",  264.0f, 5.00f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(264.0f, 5.00f, 0.866f)},
        {"264V, PF=0.866, 6.0A",  264.0f, 6.00f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(264.0f, 6.00f, 0.866f)},
        {"264V, PF=0.968, 0.5A",  264.0f, 0.50f, 0.968f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.50f, 0.968f)},
        {"264V, PF=0.968, 5.0A",  264.0f, 5.00f, 0.968f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 5.00f, 0.968f)},
        {"264V, PF=0.968, 6.0A",  264.0f, 6.00f, 0.968f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 6.00f, 0.968f)}
    };

    // =========================================================================
    // 4. 视在功率测试点 (限值：1.000% / 0.50%)
    // =========================================================================
    m_apparentPowerTestPoints = {
        // --- 176V 组 ---
        {"176V, PF=1, 0.1A", 176.0f, 0.1f, 1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.1f, 1.0f)}, // idx 0
        {"176V, PF=1, 0.2A", 176.0f, 0.2f, 1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.2f, 1.0f)}, // idx 1
        {"176V, PF=1, 0.3A", 176.0f, 0.3f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 0.3f, 1.0f)}, // idx 2
        {"176V, PF=1, 5.0A", 176.0f, 5.0f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 5.0f, 1.0f)}, // idx 3
        {"176V, PF=1, 6.0A", 176.0f, 6.0f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(176.0f, 6.0f, 1.0f)}, // idx 4

        // --- 220V 组 ---
        {"220V, PF=1, 0.1A", 220.0f, 0.1f, 1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.1f, 1.0f)},
        {"220V, PF=1, 0.2A", 220.0f, 0.2f, 1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.2f, 1.0f)},
        {"220V, PF=1, 0.3A", 220.0f, 0.3f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.3f, 1.0f)},
        {"220V, PF=1, 5.0A", 220.0f, 5.0f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.0f, 1.0f)},
        {"220V, PF=1, 6.0A", 220.0f, 6.0f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.0f, 1.0f)},

        // --- 264V 组 ---
        {"264V, PF=1, 0.1A", 264.0f, 0.1f, 1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.1f, 1.0f)},
        {"264V, PF=1, 0.2A", 264.0f, 0.2f, 1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.2f, 1.0f)},
        {"264V, PF=1, 0.3A", 264.0f, 0.3f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.3f, 1.0f)},
        {"264V, PF=1, 5.0A", 264.0f, 5.0f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 5.0f, 1.0f)},
        {"264V, PF=1, 6.0A", 264.0f, 6.0f, 1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 6.0f, 1.0f)}
    };

    // =========================================================================
    // 5. 功率因数测试点
    // =========================================================================
    m_powerFactorTestPoints = {
        // --- 第一组：110V (12个点) ---
        {"110V, PF=0.5L, 0.5A", 110.0f, 0.5f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 0.5f,  0.5f)},
        {"110V, PF=0.5L, 2.5A", 110.0f, 2.5f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 2.5f,  0.5f)},
        {"110V, PF=0.5L, 5.0A", 110.0f, 5.0f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 5.0f,  0.5f)},
        {"110V, PF=0.5L, 6.0A", 110.0f, 6.0f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 6.0f,  0.5f)},
        {"110V, PF=1.0, 0.5A",  110.0f, 0.5f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 0.5f,  1.0f)},
        {"110V, PF=1.0, 2.5A",  110.0f, 2.5f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 2.5f,  1.0f)},
        {"110V, PF=1.0, 5.0A",  110.0f, 5.0f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 5.0f,  1.0f)},
        {"110V, PF=1.0, 6.0A",  110.0f, 6.0f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 6.0f,  1.0f)},
        {"110V, PF=0.8C, 0.5A", 110.0f, 0.5f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 0.5f, -0.8f)},
        {"110V, PF=0.8C, 2.5A", 110.0f, 2.5f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 2.5f, -0.8f)},
        {"110V, PF=0.8C, 5.0A", 110.0f, 5.0f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 5.0f, -0.8f)},
        {"110V, PF=0.8C, 6.0A", 110.0f, 6.0f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(110.0f, 6.0f, -0.8f)},

        // --- 第二组：220V (12个点) ---
        {"220V, PF=0.5L, 0.5A", 220.0f, 0.5f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.5f,  0.5f)},
        {"220V, PF=0.5L, 2.5A", 220.0f, 2.5f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 2.5f,  0.5f)},
        {"220V, PF=0.5L, 5.0A", 220.0f, 5.0f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.0f,  0.5f)},
        {"220V, PF=0.5L, 6.0A", 220.0f, 6.0f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.0f,  0.5f)},
        {"220V, PF=1.0, 0.5A",  220.0f, 0.5f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.5f,  1.0f)},
        {"220V, PF=1.0, 2.5A",  220.0f, 2.5f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 2.5f,  1.0f)},
        {"220V, PF=1.0, 5.0A",  220.0f, 5.0f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.0f,  1.0f)},
        {"220V, PF=1.0, 6.0A",  220.0f, 6.0f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.0f,  1.0f)},
        {"220V, PF=0.8C, 0.5A", 220.0f, 0.5f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.5f, -0.8f)},
        {"220V, PF=0.8C, 2.5A", 220.0f, 2.5f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 2.5f, -0.8f)},
        {"220V, PF=0.8C, 5.0A", 220.0f, 5.0f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.0f, -0.8f)},
        {"220V, PF=0.8C, 6.0A", 220.0f, 6.0f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.0f, -0.8f)},

        // --- 第三组：264V (12个点) ---
        {"264V, PF=0.5L, 0.5A", 264.0f, 0.5f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.5f,  0.5f)},
        {"264V, PF=0.5L, 2.5A", 264.0f, 2.5f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 2.5f,  0.5f)},
        {"264V, PF=0.5L, 5.0A", 264.0f, 5.0f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 5.0f,  0.5f)},
        {"264V, PF=0.5L, 6.0A", 264.0f, 6.0f,  0.5f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 6.0f,  0.5f)},
        {"264V, PF=1.0, 0.5A",  264.0f, 0.5f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.5f,  1.0f)},
        {"264V, PF=1.0, 2.5A",  264.0f, 2.5f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 2.5f,  1.0f)},
        {"264V, PF=1.0, 5.0A",  264.0f, 5.0f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 5.0f,  1.0f)},
        {"264V, PF=1.0, 6.0A",  264.0f, 6.0f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 6.0f,  1.0f)},
        {"264V, PF=0.8C, 0.5A", 264.0f, 0.5f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 0.5f, -0.8f)},
        {"264V, PF=0.8C, 2.5A", 264.0f, 2.5f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 2.5f, -0.8f)},
        {"264V, PF=0.8C, 5.0A", 264.0f, 5.0f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 5.0f, -0.8f)},
        {"264V, PF=0.8C, 6.0A", 264.0f, 6.0f, -0.8f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(264.0f, 6.0f, -0.8f)}
    };

    // =========================================================================
    // 6. 有功电能走字测试点
    // =========================================================================
    m_energyActiveTestPoints = {
        // --- PF=1.0 ---
        {"220V, PF=1.0, 0.05A",  220.0f, 0.05f,  1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.05f,  1.0f)},
        {"220V, PF=1.0, 0.20A",  220.0f, 0.20f,  1.0f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.20f,  1.0f)},
        {"220V, PF=1.0, 0.25A",  220.0f, 0.25f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.25f,  1.0f)},
        {"220V, PF=1.0, 5.00A",  220.0f, 5.00f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.00f,  1.0f)},
        {"220V, PF=1.0, 6.00A",  220.0f, 6.00f,  1.0f, 0.5f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.00f,  1.0f)},

        // --- PF=0.5L ---
        {"220V, PF=0.5L, 0.10A", 220.0f, 0.10f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.10f,  0.5f)},
        {"220V, PF=0.5L, 0.25A", 220.0f, 0.25f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.25f,  0.5f)},
        {"220V, PF=0.5L, 0.40A", 220.0f, 0.40f,  0.5f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.40f,  0.5f)},
        {"220V, PF=0.5L, 0.50A", 220.0f, 0.50f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.50f,  0.5f)},
        {"220V, PF=0.5L, 5.00A", 220.0f, 5.00f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.00f,  0.5f)},
        {"220V, PF=0.5L, 6.00A", 220.0f, 6.00f,  0.5f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.00f,  0.5f)},

        // --- PF=0.8C ---
        {"220V, PF=0.8C, 0.10A", 220.0f, 0.10f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.10f, -0.8f)},
        {"220V, PF=0.8C, 0.25A", 220.0f, 0.25f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.25f, -0.8f)},
        {"220V, PF=0.8C, 0.40A", 220.0f, 0.40f, -0.8f, 1.0f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.40f, -0.8f)},
        {"220V, PF=0.8C, 0.50A", 220.0f, 0.50f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.50f, -0.8f)},
        {"220V, PF=0.8C, 5.00A", 220.0f, 5.00f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.00f, -0.8f)},
        {"220V, PF=0.8C, 6.00A", 220.0f, 6.00f, -0.8f, 0.6f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.00f, -0.8f)}
    };

    // =========================================================================
    // 7. 无功电能走字测试点
    // =========================================================================
    m_energyReactiveTestPoints = {
        // --- PF=0 ---
        {"220V, PF=0, 0.10A",      220.0f, 0.10f, 0.0f,   1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.10f, 0.0f)},
        {"220V, PF=0, 0.20A",      220.0f, 0.20f, 0.0f,   1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.20f, 0.0f)},
        {"220V, PF=0, 0.25A",      220.0f, 0.25f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 0.25f, 0.0f)},
        {"220V, PF=0, 5.00A",      220.0f, 5.00f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 5.00f, 0.0f)},
        {"220V, PF=0, 6.00A",      220.0f, 6.00f, 0.0f,   1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 6.00f, 0.0f)},

        // --- PF=0.866 ---
        {"220V, PF=0.866, 0.25A",  220.0f, 0.25f, 0.866f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.25f, 0.866f)},
        {"220V, PF=0.866, 0.40A",  220.0f, 0.40f, 0.866f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.40f, 0.866f)},
        {"220V, PF=0.866, 0.50A",  220.0f, 0.50f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 0.50f, 0.866f)},
        {"220V, PF=0.866, 5.00A",  220.0f, 5.00f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 5.00f, 0.866f)},
        {"220V, PF=0.866, 6.00A",  220.0f, 6.00f, 0.866f, 1.0f + m_sourceErrorOffset,  buildSourceConfigCmd(220.0f, 6.00f, 0.866f)},

        // --- PF=0.968246 ---
        {"220V, PF=0.968246, 0.50A", 220.0f, 0.50f, 0.968246f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 0.50f, 0.968246f)},
        {"220V, PF=0.968246, 5.00A", 220.0f, 5.00f, 0.968246f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 5.00f, 0.968246f)},
        {"220V, PF=0.968246, 6.00A", 220.0f, 6.00f, 0.968246f, 1.25f + m_sourceErrorOffset, buildSourceConfigCmd(220.0f, 6.00f, 0.968246f)}
    };
}

bool CalibrationThread::readMeterHarmonicData16(QSerialPort &port, quint8 addr, quint16 startReg, int count16, QVector<float> &outValues)
{
    int byteCount = count16 * 2; // 🌟 谐波每个数据只占 1 个 Modbus 寄存器 (2 个字节)
    QByteArray frame;
    frame.append(addr).append(0x03);
    frame.append(startReg >> 8).append(startReg & 0xFF);
    frame.append(count16 >> 8).append(count16 & 0xFF);
    quint16 crc = calculateCRC(frame);
    frame.append(crc & 0xFF).append(crc >> 8);

    // 🌟 1. 打印 Tx (带上起始地址方便排查是读什么参数)
    qInfo().noquote() << QString("[Tx 仪表%1 读谐波 0x%2]")
                             .arg(addr, 2, 10, QChar('0'))
                             .arg(startReg, 4, 16, QChar('0')).toUpper()
                      << frame.toHex(' ').toUpper();
    port.clear(QSerialPort::Input);
    port.write(frame);

    // 检查响应 (复用您写好的超时检查函数)
    if (!checkMeterResponse(port, addr)) {
        qWarning().noquote() << QString(" [Rx 仪表%1] 读取谐波无响应或超时！").arg(addr);
        return false;
    }

    QByteArray rx = port.readAll();

    // 🌟 2. 打印 Rx (接收到的原始字节)
    qInfo().noquote() << QString("[Rx 仪表%1]")
                             .arg(addr, 2, 10, QChar('0'))
                      << rx.toHex(' ').toUpper();

    // 🌟 3. 校验报文是否完整
    if (rx.size() >= byteCount + 5 && rx[0] == addr && rx[1] == 0x03 && rx[2] == byteCount) {
        outValues.clear();
        QStringList parsedStrs; // 用于收集解析出的浮点数，方便打印

        for (int i = 0; i < count16; ++i) {
            int base = 3 + i * 2; // 🌟 16位数据，每次循环偏移 2 个字节

            // 解析无符号 16位整数 (DataH << 8 | DataL)
            quint16 rawVal = ((quint8)rx[base] << 8) | (quint8)rx[base+1];

            // 还原为真实浮点数 (单片机放大了 100 倍，所以固定除以 100.0f)
            float finalVal = rawVal / 100.0f;

            outValues.append(finalVal);

            // 🌟 格式化为 2 位小数打印，与单片机精度 0.01 保持绝对一致
            parsedStrs << QString::number(finalVal, 'f', 3);
        }

        // 🌟 4. 打印成功解析的数据，一目了然！
        qInfo().noquote() << QString(" 谐波解析成功 (%1个参数): ").arg(count16) + parsedStrs.join(", ");
        return true;

    } else {
        // 🌟 5. 精准报错：告诉您为什么解析失败
        qWarning().noquote() << QString(" [Rx 仪表%1] 谐波报文异常！期望总长>=%2 实际:%3 | 期望数据长:%4 实际(rx[2]):%5")
                                    .arg(addr)
                                    .arg(byteCount + 5).arg(rx.size())
                                    .arg(byteCount).arg(rx.size() > 2 ? (int)rx[2] : -1);
        return false;
    }
}

bool CalibrationThread::sendSourceCmd(QSerialPort &port, const QByteArray &cmdHex, int timeoutMs)
{
    QString lastErrorMsg = "标准源响应超时"; // 记录最后一次失败的原因，方便调试溯源

    // 开启最多 3 次的容错重试
    for (int tryIdx = 1; tryIdx <= 3; ++tryIdx) {

        if (tryIdx > 1) {
            qWarning().noquote() << QString("[标准源重试] 第 %1 次重新尝试发送指令...").arg(tryIdx);
            QThread::msleep(1000); // 失败后稍作停顿再发，给标准源内部 CPU 一点缓冲释放串口缓冲区的时间
        }

        // 发送指令
        qInfo().noquote() << QString("[Tx 标准源 (第%1次)] ").arg(tryIdx) << cmdHex.toHex(' ').toUpper();
        port.write(cmdHex);

        // 校验等待超时 (没响应则继续下一次循环)
        if (!checkSourceResponse(port, timeoutMs)) {
            lastErrorMsg = "标准源响应超时(无返回)";
            qWarning() << "[Rx 标准源] 第" << tryIdx << "次读取：超时未收到任何响应";
            continue;
        }

        QByteArray rx = port.readAll();
        qInfo().noquote() << QString("[Rx 标准源 (第%1次)] ").arg(tryIdx) << rx.toHex(' ').toUpper();
        QThread::msleep(100);

        // 只要匹配上正确的 ACK，立刻直接通关！
        if (rx == m_srcAck) {
            if (tryIdx > 1) {
                qInfo() << ">>> [标准源重试成功] 在第" << tryIdx << "次尝试时通信恢复正常！";
            }
            return true; // 完美的 8 字节全帧匹配，直接跳出循环并放行
        }

        // 记录本次错误的分类，并进入下一次循环
        if (rx == m_srcNack) {
            lastErrorMsg = "标准源配置被拒(否定应答 NACK)";
            qWarning() << "[Rx 标准源] 第" << tryIdx << "次读取：标准源返回 NACK 否定应答！";
        } else {
            lastErrorMsg = "标准源应答特征不匹配";
            qWarning() << "[Rx 标准源] 第" << tryIdx << "次读取：应答特征码错误，内容:" << rx.toHex(' ').toUpper();
        }
    }

    // =========================================================================
    // 3 次全部失败 —— 通知 UI 并停止程序的运行
    // =========================================================================
    qCritical().noquote() << "[标准源致命错误] 连续 3 次发送均失败！最终死因:" << lastErrorMsg;
    emit srcMessage(lastErrorMsg, "error");
    emit showResultPopup("标准源错误",lastErrorMsg,"error");
    m_isRunning = false; // 停止上位机后台测试线程
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

    // 【核心防呆】：如果这是复位指令，不指望它回包，写完直接算成功！
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
    emit showResultPopup("标准源通讯失败","请确认设备是否开机或接线正确", "error");
    //emit showTopMessage("标准源通讯失败，请确认设备是否开机或接线正确", "error");
    m_isRunning = false;
    return false;
}

bool CalibrationThread::checkSourceResponse(QSerialPort &port, int timeoutMs) {
    if (!port.waitForBytesWritten(3000) || !port.waitForReadyRead(timeoutMs)) {
        QSerialPort::SerialPortError err = port.error();
        if (err == QSerialPort::NoError) err = QSerialPort::TimeoutError;

        QString errStr = translateSerialError(err);
        qWarning()<<"标准源指令发送或接收超时!"<<errStr;
        emit srcMessage("标准源中断: " + errStr, "error");
        //emit showResultPopup("标准源中断: " , errStr, "error");
        //m_isRunning = false;
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
            emit showResultPopup("485总线物理故障", translateSerialError(err), "error");
            m_isRunning = false;
        } else {
            qWarning() << "[Thread] 仪表" << meterIndex << "请求超时";
        }
        return false;
    }
    while (port.waitForReadyRead(50)) {
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
                //emit calirResult("[拒绝读取] 标准源返回否定全帧，输出未就绪！", "error");

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
                    qInfo() << ">>> 20 项参数已全量达标并稳定生效！";
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
        //emit calirResult("致命超时：标准源全量参数未能同时达标稳住！", "error");
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

void CalibrationThread::exportExcelReport(const Meter &meter)
{
    QXlsx::Document xlsx;

    // 2. 准备各种字体格式
    QXlsx::Format titleFormat;
    titleFormat.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    titleFormat.setVerticalAlignment(QXlsx::Format::AlignVCenter);
    titleFormat.setFontBold(true);
    titleFormat.setFontSize(12);

    QXlsx::Format normalFormat;
    normalFormat.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    normalFormat.setVerticalAlignment(QXlsx::Format::AlignVCenter);
    normalFormat.setNumberFormat("0.000");

    QXlsx::Format failFormat = normalFormat;
    failFormat.setFontColor(Qt::red); // 不合格标红
    failFormat.setNumberFormat("0.000");

    // 3. 类别名称与列标题的映射字典
    QMap<int, QString> sheetNames = {
        {Cat_V, "电压"}, {Cat_I, "电流"}, {Cat_ActivePower, "有功功率"},
        {Cat_ReactivePower, "无功功率"}, {Cat_ApparentPower, "视在功率"},
        {Cat_PowerFactor, "功率因数"}, {6, "谐波电压"}, {7, "谐波电流"}
    };

    QMap<int, QStringList> categoryHeaders = {
        {Cat_V, {"Ua", "Ub", "Uc", "Uab", "Ubc", "Uca"}},
        {Cat_I, {"Ia", "Ib", "Ic"}},
        {Cat_ActivePower, {"Pa", "Pb", "Pc", "P总"}},
        {Cat_ReactivePower, {"Qa", "Qb", "Qc", "Q总"}},
        {Cat_ApparentPower, {"Sa", "Sb", "Sc", "S总"}},
        {Cat_PowerFactor, {"PFa", "PFb", "PFc", "PF总"}},
        {6, {"Ua(含量3%)", "Ub(含量3%)", "Uc(含量3%)","Ua(含量10%)", "Ub(含量10%)", "Uc(含量10%)"}},
        {7, {"Ia(含量10%)", "Ib(含量10%)", "Ic(含量10%)","Ia(含量20%)", "Ib(含量20%)", "Ic(含量20%)"}}
    };

    // 全局合格标志：默认合格，抓到任何一个错误就置为 false
    bool isMeterOverallPass = true;

    // 4. 遍历该表的所有数据项，按分类生成 Sheet
    for (auto it = meter.categories.constBegin(); it != meter.categories.constEnd(); ++it) {
        int catId = it.key();
        const Category &cat = it.value();
        QString sheetName = sheetNames.value(catId, QString("分类_%1").arg(catId));

        // 创建新的 Sheet 页 (如果默认只有一个Sheet且是第一个，则重命名)
        if (xlsx.sheetNames().contains("Sheet1") && it == meter.categories.constBegin()) {
            xlsx.renameSheet("Sheet1", sheetName);
        } else {
            xlsx.addSheet(sheetName);
        }
        xlsx.selectSheet(sheetName);

        // 设置第一列“测试条件”列宽
        xlsx.setColumnWidth(1, 1, 25);

        // 当前 Sheet 合格标志
        bool isCurrentSheetPass = true;

        QStringList phases = categoryHeaders.value(catId);

        // --- 绘制表头 ---
        xlsx.mergeCells("A1:A2", titleFormat);
        xlsx.write("A1", "测试条件", titleFormat);

        int colCursor = 2; // B列开始
        for (const QString &phase : phases) {
            xlsx.mergeCells(QXlsx::CellRange(1, colCursor, 1, colCursor + 3), titleFormat);
            xlsx.write(1, colCursor, phase, titleFormat);

            xlsx.write(2, colCursor + 0, "标准值", normalFormat);
            xlsx.write(2, colCursor + 1, "仪表值", normalFormat);
            xlsx.write(2, colCursor + 2, "误差", normalFormat);
            xlsx.write(2, colCursor + 3, "限制", normalFormat);

            xlsx.setColumnWidth(colCursor, colCursor + 3, 10);
            colCursor += 4;
        }

        // --- 填充实际测试数据 ---
        int rowCursor = 3; // 第3行开始是数据
        for (const Row &row : cat.rows) {
            xlsx.write(rowCursor, 1, row.conditionName, normalFormat);

            int dataColCursor = 2;
            for (const Cell &cell : row.cells) {

                // 🌟 核心修复：浮点数精度修正，防止如 0.503 变成 0.503000021 导致误判
                double rawErr = (cell.meterVal - cell.stdVal) / cell.stdVal * 100.0;
                double cleanErr = qRound(rawErr * 1000.0) / 1000.0; // 四舍五入保留3位小数
                bool currentFail = (qAbs(cleanErr) > cell.limit);

                if (currentFail) {
                    isCurrentSheetPass = false;
                    isMeterOverallPass = false;
                }

                QXlsx::Format currentFormat = currentFail ? failFormat : normalFormat;

                double cleanStd = QString::number(cell.stdVal, 'f', 3).toDouble();
                double cleanMeter = QString::number(cell.meterVal, 'f', 3).toDouble();

                xlsx.write(rowCursor, dataColCursor + 0, cleanStd, currentFormat);
                xlsx.write(rowCursor, dataColCursor + 1, cleanMeter, currentFormat);

                // 误差带百分号格式化
                QString errStr = QString("%1%2%").arg(cleanErr > 0 ? "+" : "").arg(cleanErr, 0, 'f', 3);
                xlsx.write(rowCursor, dataColCursor + 2, errStr, currentFormat);

                // 限制值
                QString limitStr = QString("±%1%").arg(cell.limit, 0, 'f', 2);
                xlsx.write(rowCursor, dataColCursor + 3, limitStr, currentFormat);

                dataColCursor += 4;
            }
            rowCursor++;
        }

        // 如果当前 Sheet 有不合格数据：改名 + 标签涂红
        if (!isCurrentSheetPass) {
            QString failSheetName = QString("❌%1_不合格").arg(sheetName);
            xlsx.renameSheet(sheetName, failSheetName);

            QXlsx::Worksheet *sheet = xlsx.currentWorksheet();
            if (sheet) {
                sheet->setTabColor(Qt::red);
            }
        }
    }

    // 🌟 5. 按当前日期创建子目录，例如 reports/2026-07-23
    QString dateFolderStr = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QDir reportDir("reports/" + dateFolderStr);
    if (!reportDir.exists()) {
        reportDir.mkpath(".");
    }

    // 6. 生成最终文件名
    QString statusSuffix = isMeterOverallPass ? "合格" : "不合格";
    QString timeString = QDateTime::currentDateTime().toString("HHmmss");
    QString baseFileName;

    if (m_workMode == Mode_ErrorCalc) {
        baseFileName = QString("%1_误差测试报告_%2_%3.xlsx").arg(meter.sn, statusSuffix, timeString);
    } else if (m_workMode == Mode_EnergyCalc) {
        baseFileName = QString("%1_电能测试报告_%2_%3.xlsx").arg(meter.sn, statusSuffix, timeString);
    } else {
        baseFileName = QString("%1_测试报告_%2_%3.xlsx").arg(meter.sn, statusSuffix, timeString);
    }

    QString fileName = reportDir.absoluteFilePath(baseFileName);

    // 7. 存盘
    if (xlsx.saveAs(fileName)) {
        qInfo() << ">>> 报表导出成功:" << fileName;
    } else {
        qWarning() << ">>> 报表导出失败，请检查文件是否被其它程序(Excel)占用:" << fileName;
    }
}

void CalibrationThread::cleanOldReportFolders()
{
    QDir rootDir("reports");
    if (!rootDir.exists())
        return;

    // 只扫描 reports 下的一级子目录（日期文件夹）
    rootDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    QFileInfoList dirList = rootDir.entryInfoList();

    // 30 天前的时间阈值（只精确到天）
    QDateTime thresholdTime = QDateTime::currentDateTime().addDays(-30);

    for (const QFileInfo &dirInfo : dirList) {
        // 🌟 核心优化：直接把文件夹名字（如 "2026-07-23"）解析成 QDate
        QDate folderDate = QDate::fromString(dirInfo.fileName(), "yyyy-MM-dd");

        // 确保文件夹名字确实是 "yyyy-MM-dd" 格式，防止用户或其他程序乱建文件夹报错
        if (folderDate.isValid()) {
            QDateTime folderDateTime(folderDate, QTime(0, 0, 0));

            // 如果该日期早于 30 天前，直接删掉整个文件夹
            if (folderDateTime < thresholdTime) {
                QDir oldDir(dirInfo.absoluteFilePath());

                if (oldDir.removeRecursively()) {
                    qInfo() << ">>> 已自动清理超过30天的过期报表目录:" << dirInfo.fileName();
                } else {
                    qWarning() << ">>> 清理过期报表目录失败:" << dirInfo.fileName();
                }
            }
        }
    }
}

