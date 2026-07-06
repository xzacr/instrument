#ifndef CALIBRATIONTHREAD_H
#define CALIBRATIONTHREAD_H

#include <QThread>
#include <QSerialPort>
#include <QVariantMap>

class CalibrationThread : public QThread
{
    Q_OBJECT
public:
    enum CalibStep{
        Step_Unlock  = 0,  // 解除写保护
        Step_Prepare = 1,  // 校准准备
        Step_VI_10   = 2,  // 电压电流校准 (PF=1.0)
        Step_VI_05   = 3,  // 电压电流校准 (PF=0.5)
        Step_Save    = 4,  // 参数固化保存
        Step_Reset   = 5   // 等待复位
    };
    enum StepState{
        State_Wait      = 0,  // 等待中
        State_Running   = 1,  // 正在执行
        State_Success   = 2,  // 成功
        State_Failed    = 3,  // 失败
    };
    enum WorkMode {
        Mode_FullAuto = 0,
        Mode_ErrorCalc = 1
    };
    struct MeterTask {
        int uiIndex;      // QML 界面上的索引 (0~4)
        quint8 address;   // 物理 Modbus 地址 (1~5)
        bool isEnabled;   // 🌟 新增：用户是否勾选了启用
        bool isAlive;     // 淘汰标志位：true表示存活继续，false表示这台表已挂
    };
    explicit CalibrationThread(QObject *parent = nullptr);
    ~CalibrationThread() override;

    // 接收界面的配置
    void setConfig(int mode, const QString &srcPort, int srcBaud, const QString &meterPort, int meterBaud, const QVariantList &meterConfigs);
    void stopCalibration();

signals:
    // 发送给主界面弹窗的跨线程信号
    void showTopMessage(const QString &msg, const QString &type);
    void srcMessage(const QString &msg, const QString &type);
    void meterStepStatusChanged(int meterIndex, int stepIndex, int status);
    void calirResult(const QString &msg, const QString &type);
    //void meterMessage(const QString &msg, const QString &type,const int sn);

protected:
    void run() override;

private slots:
    // 专门处理串口底层物理断开/挂起的槽函数
    void onSourcePortError(QSerialPort::SerialPortError error);
    void onMeterPortError(QSerialPort::SerialPortError error);

private:
    // 将 run 逻辑抽离成独立的子流水线
    void runCalibrationFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<MeterTask> &meters, int &aliveCount);
    // 专门用于测误差的独立流水线
    void runErrorCalcFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<MeterTask> &meters, int &aliveCount);


    // 标准源开机握手
    bool handshakeSource(QSerialPort &port);
    // 标准源：发送并死等指定的应答
    bool sendSourceCmd(QSerialPort &port, const QByteArray &cmdHex, int timeoutMs = 1000);


    // 翻译机
    QString translateSerialError(QSerialPort::SerialPortError error);

    // 检查标准源（支持动态配置超时时间，默认 1000 毫秒）
    bool checkSourceResponse(QSerialPort &port, int timeoutMs = 1000);
    // 检查仪表（容忍超时，不容忍拔线）
    bool checkMeterResponse(QSerialPort &port, int meterIndex);



    // 仪表：Modbus RTU CRC16 校验计算
    quint16 calculateCRC(const QByteArray &data);

    // 仪表：向指定寄存器写一个值 (06命令)
    bool writeMeterReg(QSerialPort &port, quint8 addr, quint16 reg, quint16 value, bool expectAck = true);
    // 仪表：读取指定寄存器的值 (03命令)
    bool readMeterReg(QSerialPort &port, quint8 addr, quint16 reg, quint16 &outValue);

    // 仪表：【最核心】轮询死等某个寄存器变成你想要的那个值
    bool waitMeterState(QSerialPort &port, quint8 addr, quint16 reg, quint16 targetState, int timeoutMs = 15000);

    // 🌟 参数升级：直接传入目标 PF (1.0f 或 0.5f)，函数内部自动计算三相目标角度！
    bool waitSourceStable(QSerialPort &port, float targetPF, int timeoutMs);

    QString m_srcPortName;
    int m_srcBaud;
    QString m_meterPortName;
    int m_meterBaud;

    std::atomic<bool> m_isRunning;

    // =====================================================================
    // 核心配置与固定报文（写在头文件，杜绝 goto 编译变量跳过报错）
    // =====================================================================
    const quint16 m_regWriteProtect = 0x1FFF;
    const quint16 m_regState = 0x2000;

    // 正应答和否应答
    const QByteArray m_srcAck    = QByteArray::fromHex("68 08 00 68 80 10 90 16");
    const QByteArray m_srcNack   = QByteArray::fromHex("68 08 00 68 80 80 00 16");

    // 握手 启动 停止命令
    const QByteArray m_handshakeCmd = QByteArray::fromHex("68 0C 00 68 00 84 00 00 00 00 84 16");
    const QByteArray m_startCmd     = QByteArray::fromHex("68 1A 00 68 00 03 18 01 00 19 01 00 1A 01 00 1B 01 00 1C 01 00 1D 01 00 A8 16");
    const QByteArray m_stopCmd      = QByteArray::fromHex("68 1D 00 68 00 04 1F 01 00 20 01 00 21 01 00 22 01 00 23 01 00 24 01 00 25 01 00 F9 16");

    // 220V 5A 1.0PF 配置帧
    const QByteArray m_cfgCmd1 = QByteArray::fromHex("68 6C 00 68 00 92 01 00 00 5C 43 02 00 00 00 00 26 55 00 00 00 03 00 00 5C 43 04 00 00 70 43 27 55 00 00 00 05 00 00 5C 43 06 00 00 F0 42 28 55 00 00 00 07 00 00 A0 40 08 00 00 00 00 29 55 00 00 00 09 00 00 A0 40 0A 00 00 70 43 2A 55 00 00 00 0B 00 00 A0 40 0C 00 00 F0 42 2B 55 00 00 00 0E 00 00 48 42 0F 00 00 48 42 49 16");
    // 220V 5A 0.5PF 配置帧
    const QByteArray m_cfgCmd2 = QByteArray::fromHex("68 6C 00 68 00 92 01 00 00 5C 43 02 00 00 00 00 26 55 00 00 00 03 00 00 5C 43 04 00 00 70 43 27 55 00 00 00 05 00 00 5C 43 06 00 00 F0 42 28 55 00 00 00 07 00 00 A0 40 08 00 00 96 43 29 55 00 00 00 09 00 00 A0 40 0A 00 00 34 43 2A 55 00 00 00 0B 00 00 A0 40 0C 00 00 70 42 2B 55 00 00 00 0E 00 00 48 42 0F 00 00 48 42 66 16");
    // 六通道全量高精度查询帧
    const QByteArray m_queryAllCmd = QByteArray::fromHex("68 6C 00 68 00 91 01 00 00 00 00 02 00 00 00 00 26 00 00 00 00 03 00 00 00 00 04 00 00 00 00 27 00 00 00 00 05 00 00 00 00 06 00 00 00 00 28 00 00 00 00 07 00 00 00 00 08 00 00 00 00 29 00 00 00 00 09 00 00 00 00 0A 00 00 00 00 2A 00 00 00 00 0B 00 00 00 00 0C 00 00 00 00 2B 00 00 00 00 0E 00 00 00 00 0F 00 00 00 00 EF 16");


    QList<MeterTask> m_meterTasks;
    // 新增一个成员变量保存当前模式
    WorkMode m_workMode = Mode_FullAuto;
};

#endif // CALIBRATIONTHREAD_H