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
        Step_Reset0   = 0,  // 等待复位
        Step_Unlock  = 1,  // 解除写保护
        Step_Prepare = 2,  // 校准准备
        Step_VI_10   = 3,  // 电压电流校准 (PF=1.0)
        Step_VI_05   = 4,  // 电压电流校准 (PF=0.5)
        Step_Save    = 5,  // 参数固化保存
        Step_Reset   = 6   // 等待复位
    };
    enum StepState{
        State_Wait      = 0,  // 等待中
        State_Running   = 1,  // 正在执行
        State_Success   = 2,  // 成功
        State_Failed    = 3,  // 失败
    };
    enum WorkMode {
        Mode_FullAuto = 0,
        Mode_ErrorCalc = 1,
        Mode_EnergyCalc = 2
    };
    enum ErrorCardStatus {
        Error_Idle    = 0, // 灰色：未启用
        Error_Pass    = 1, // 绿色：全部合格
        Error_Fail    = 2, // 红色：存在不合格项
        Error_Running = 3, // 蓝色：正在测试中
        Error_Timeout = 4, // 黄色：通讯超时
        Error_Stop = 5     // 黄色：通讯超时
    };
    enum CategoryType {
        Cat_V = 0,             // 0: 电压
        Cat_I = 1,             // 1: 电流
        Cat_ActivePower = 2,   // 2: 有功功率
        Cat_ReactivePower = 3, // 3: 无功功率
        Cat_ApparentPower = 4, // 4: 视在功率
        Cat_PowerFactor = 5,   // 5: 功率因数
        Cat_HarmonicV = 6,     // 6: 谐波电压
        Cat_HarmonicI = 7      // 7: 谐波电流
    };
    // 测试条件
    struct TestPoint {
        QString name;
        float tgtV;
        float tgtI;
        float tgtPF;
        float limit;
        QByteArray srcCmd;
    };
    // 数据单元格 (保留所有数据供Excel用，但界面只用误差)
    struct Cell {
        float stdVal;   // 理论基准值
        float meterVal; // 仪表实测值
        float err;      // 误差 %
        bool isFail;    // 是否超标
        float limit;    // 限值     (存下来给Excel用)
    };

    // 表格的一行 (例如："220V, 5A, PF=1.0" 这一行的各种误差)
    struct Row {
        QString conditionName;   // 左侧固定的工况名称
        QVector<Cell> cells; // 右侧动态的数据列集合
    };

    // 某一类别的完整表格 (例如：整个"电压"表，包含多行)
    struct Category {
        int categoryIndex;      // 类别标识 (0=电压, 1=电流, 2=有功功率...)
        QVector<Row> rows;  // 表格里的所有行
    };

    // 单台仪表的全部参数与数据记录
    struct Meter {
        int uiIndex;      // QML 界面上的索引 (0~4)
        quint8 address;   // 物理 Modbus 地址 (1~255)
        QString sn;       // 出厂编号
        bool isEnabled;   // 用户是否勾选了启用
        bool isAlive;     // 淘汰标志位：true表示存活，false表示通信失败已淘汰

        bool hasFail;                   // 只要这张表有任何一项超标，整表亮红灯
        QMap<int, Category> categories; // 按类别(0,1,2...)分类存放的表格集合
    };

    // 标准源参数快照
    struct SourceParams {
        // 1. 电压 (Ua, Ub, Uc, Uab, Ubc, Uca)
        float U[6] = {0};
        // 2. 电流 (Ia, Ib, Ic)
        float I[3] = {0};
        // 3. 有功功率 (Pa, Pb, Pc, P0总)
        float P[4] = {0};
        // 4. 无功功率 (Qa, Qb, Qc, Q0总)
        float Q[4] = {0};
        // 5. 视在功率 (Sa, Sb, Sc, S0总)
        float S[4] = {0};
        // 6. 功率因数 (PFa, PFb, PFc, PF0总)
        float PF[4] = {0};
        // 7. 频率
        float freq = 0;
        // 8. 相位 (Phi_Ub, Phi_Uc, Phi_Ia, Phi_Ib, Phi_Ic)(Ua基准固定为0)
        float Phi[5] = {0};

        // // 极速判断当前标准源是不是已经处于“零输出/停止”状态！
        // bool isOutputZero() const {
        //     // 只要三相电压都小于 2V 且 电流都小于 0.01A，就认定物理上已经关停
        //     return (U[0] < 2.0f && U[1] < 2.0f && U[2] < 2.0f &&
        //             I[0] < 0.01f && I[1] < 0.01f && I[2] < 0.01f);
        // }
    };

    explicit CalibrationThread(QObject *parent = nullptr);
    ~CalibrationThread() override;

    // 接收界面的配置
    void setConfig(int mode, const QString &srcPort, int srcBaud, const QString &meterPort, int meterBaud, const QVariantList &meterConfigs);
    // 停止校准
    void stopCalibration();
    // 设置标准源误差
    void setSourceErrorOffset(float val);
    // 重载测试工况表( 修改标准源误差时调用 )
    void reloadTestPoints(void);

signals:
    // 发送给主界面弹窗的跨线程信号
    // 测试结果信号
    void showResultPopup(QString title, QString msg, QString type);
    // 顶部悬浮通知栏
    void showTopMessage(const QString &msg, const QString &type);
    // 标准源状态 (基本没啥用)
    void srcMessage(const QString &msg, const QString &type);
    // 校准步骤
    void meterStepStatusChanged(int meterIndex, int stepIndex, int status);
    // 5个仪表卡片状态
    // status: 0=IDLE(灰), 1=PASS(绿), 2=FAIL(红), 3=RUNNING(蓝)
    void updateErrorMeterStatus(int page,int meterIndex, int status, const QString &msg);

    // 专门用于表格行追加
    void appendErrorRow(int page, int meterIndex, int categoryIndex, const QString &rowName, const QVariantList &rowCells);

protected:
    void run() override;

private slots:
    // 专门处理串口底层物理断开/挂起的槽函数
    void onSourcePortError(QSerialPort::SerialPortError error);
    void onMeterPortError(QSerialPort::SerialPortError error);

private:
    // 电能走字专属流程与指令
    bool runEnergyCalcFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, int &aliveCount);
    // 执行单一电能(有功/无功)走字测试的通用函数
    bool runEnergyCategory(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, const QList<TestPoint> &testPoints, int categoryIdx, bool isActive);

    QByteArray buildEnergyClearCmd(int checkType); // 构造 0x81 AA 清零命令(1:有功, 0:无功)
    bool readStandardEnergy(QSerialPort &srcPort, float &outActiveEnergy, float &outReactiveEnergy); // 解析 0x81 55



    bool readMeterHarmonicData16(QSerialPort &port, quint8 addr, quint16 startReg, int count16, QVector<float> &outValues);
    // 🌟 谐波专属主流程
    bool runHarmonicsFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, int &aliveCount);

    // 🌟 构造标准源的 0x27 (超集谐波写) 命令
    QByteArray buildHarmonicCmd(int targetOrder, float vRatio, float iRatio);
    // 🌟 1. 通用误差计算与 QML 数据打包工具
    QVariantMap calcErrAndMakeMap(uint8_t addr, const QString &phaseName, float std, float meas, Cell &outCell, float limit,const QString &conditionName);

    //bool processActivePowerData(Meter &meter, const TestPoint &pt,const SourceParams stdParams, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry);

    // 1. 电压/电流 (注意：同时需要传入 vol 和 cur 两套缓存)
    bool processVoltageCurrentData(Meter &meter, const TestPoint &pt, const QVector<float> &viData, Row &volRow, QVariantList &volQmlCells, Row &curRow, QVariantList &curQmlCells, bool isLastTry);

    // 2. 无功功率
    bool processReactivePowerData(Meter &meter, const TestPoint &pt, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry);

    // 3. 视在功率
    bool processApparentPowerData(Meter &meter, const TestPoint &pt, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry);

    // 4. 功率因数
    bool processPowerFactorData(Meter &meter, const TestPoint &pt, const QVector<float> &rawData, Row &row, QVariantList &qmlCells, bool isLastTry);
    // 🌟 3. 万能执行引擎（负责切源、等待、读取，读完后回调处理函数）
    // 为了不使用复杂的 std::function 或 Lambda，我们用一个“类别枚举”来让引擎自己判断调哪个处理函数

    bool runTestCategory(QSerialPort &srcPort, QSerialPort &meterPort, CategoryType catType, uint16_t startAddr, int regCount, const QList<TestPoint> &testPoints, QList<Meter> &meters,int &aliveCount);    // 🌟 万能 32 位连续读取函数：只要传入起始地址和参数个数，通吃电压、电流、功率、谐波！
    bool readMeterData(QSerialPort &port, quint8 addr, quint16 startReg, int count32, QVector<float> &outValues, float divider = 10.0f, bool isSigned = false);
    // 动态组装函数：传入目标电压、电流、功率因数(如 1.0, 0.5)
    QByteArray buildSourceConfigCmd(float v, float i, float pf);
    // 将 run 逻辑抽离成独立的子流水线
    bool runCalibrationFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, int &aliveCount);
    // 专门用于测误差的独立流水线
    bool runErrorCalcFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, int &aliveCount);


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
    const quint16 m_regReset1 = 0x1008;
    const quint16 m_regEnergyClear = 0x1009;


    const QByteArray m_enragyclear1    = QByteArray::fromHex("68 08 00 68 00 83 83 16");
    // 正应答和否应答
    const QByteArray m_srcAck    = QByteArray::fromHex("A1 01 55");
    const QByteArray m_srcNack   = QByteArray::fromHex("68 08 00 68 80 80 00 16");

    // 握手 启动 停止命令
    const QByteArray m_handshakeCmd = QByteArray::fromHex("A1 01 04 D1 10 01 E2");
    const QByteArray m_startCmd     = QByteArray::fromHex("A1 01 04 DC 00 01 DD");
    const QByteArray m_stopCmd      = QByteArray::fromHex("A1 01 04 DC 00 00 DC");
    const QByteArray m_readAllCmd   = QByteArray::fromHex("A1 01 02 A0 A0");

    const QByteArray m_readFreqCmd          = QByteArray::fromHex("A1 01 03 F0 00 F0"); // F0: 读实时频率
    const QByteArray m_readActivePowerCmd   = QByteArray::fromHex("A1 01 03 F1 00 F1"); // F1: 读有功功率
    const QByteArray m_readReactivePowerCmd = QByteArray::fromHex("A1 01 03 F2 00 F2"); // F2: 读无功功率
    const QByteArray m_readApparentPowerCmd = QByteArray::fromHex("A1 01 03 F3 00 F3"); // F3: 读视在功率
    const QByteArray m_readPowerFactorCmd   = QByteArray::fromHex("A1 01 03 F4 00 F4"); // F4: 读功率因数
    const QByteArray m_readVolCurCmd        = QByteArray::fromHex("A1 01 03 F6 00 F6"); // F6: 读电压电流
    const QByteArray m_readPhaseCmd         = QByteArray::fromHex("A1 01 03 F5 00 F5"); // F5: 读相位夹角

    const QByteArray m_harmonicLoadAllCmd  = QByteArray::fromHex("A1 01 04 D3 25 00 F8"); // D3: 6通道同时加载谐波
    const QByteArray m_harmonicClearAllCmd = QByteArray::fromHex("A1 01 03 DB 00 DB");    // DB: 清除6通道谐波
    const QByteArray m_harmonicCloseLoopCmd= QByteArray::fromHex("A1 01 03 C6 00 C6");    // C6: 谐波高频闭环一次

    // =========================================================================
    // STR3060A 电能走字
    // =========================================================================
    const QByteArray m_switchToEnergyUI_Cmd = QByteArray::fromHex("A1 01 03 C0 22 E2"); // 切进电能测试界面 (Md=0x22)
    const QByteArray m_switchToSourceUI_Cmd = QByteArray::fromHex("A1 01 03 C0 27 E7"); // 测完切回常规主界面 (Md=0x27)
    const QByteArray m_startEnergyCmd       = QByteArray::fromHex("A1 01 03 CB 01 CC"); // 开始电能累计
    const QByteArray m_stopEnergyCmd        = QByteArray::fromHex("A1 01 03 CB 00 CB"); // 停止电能累计
    const QByteArray m_readEnergyCmd        = QByteArray::fromHex("A1 01 03 FC 00 FC");



    // 220V 5A 1.0PF 配置帧
    const QByteArray m_cfgCmd1 = QByteArray::fromHex("68 6C 00 68 00 92 01 00 00 5C 43 02 00 00 00 00 26 55 00 00 00 03 00 00 5C 43 04 00 00 70 43 27 55 00 00 00 05 00 00 5C 43 06 00 00 F0 42 28 55 00 00 00 07 00 00 A0 40 08 00 00 00 00 29 55 00 00 00 09 00 00 A0 40 0A 00 00 70 43 2A 55 00 00 00 0B 00 00 A0 40 0C 00 00 F0 42 2B 55 00 00 00 0E 00 00 48 42 0F 00 00 48 42 49 16");
    // 220V 5A 0.5PF 配置帧
    const QByteArray m_cfgCmd2 = QByteArray::fromHex("68 6C 00 68 00 92 01 00 00 5C 43 02 00 00 00 00 26 55 00 00 00 03 00 00 5C 43 04 00 00 70 43 27 55 00 00 00 05 00 00 5C 43 06 00 00 F0 42 28 55 00 00 00 07 00 00 A0 40 08 00 00 96 43 29 55 00 00 00 09 00 00 A0 40 0A 00 00 34 43 2A 55 00 00 00 0B 00 00 A0 40 0C 00 00 70 42 2B 55 00 00 00 0E 00 00 48 42 0F 00 00 48 42 66 16");
    // 六通道全量高精度查询帧
    const QByteArray m_queryAllCmd = QByteArray::fromHex("68 6C 00 68 00 91 01 00 00 00 00 02 00 00 00 00 26 00 00 00 00 03 00 00 00 00 04 00 00 00 00 27 00 00 00 00 05 00 00 00 00 06 00 00 00 00 28 00 00 00 00 07 00 00 00 00 08 00 00 00 00 29 00 00 00 00 09 00 00 00 00 0A 00 00 00 00 2A 00 00 00 00 0B 00 00 00 00 0C 00 00 00 00 2B 00 00 00 00 0E 00 00 00 00 0F 00 00 00 00 EF 16");
    // 0x28 启动全体谐波 (Ua,Ub,Uc,Ia,Ib,Ic = 55 55 55 55 55 55)
    const QByteArray startAllHarmonicCmd = QByteArray::fromHex("68 0E 00 68 00 28 55 55 55 55 55 55 26 16");
    const QByteArray m_queryStatusCmd = QByteArray::fromHex("68 08 00 68 00 82 82 16");
    const QByteArray m_queryEnergyCmd = QByteArray::fromHex("68 09 00 68 00 81 55 D6 16");


    QList<TestPoint> m_viTestPoints;
    QList<TestPoint> m_activePowerTestPoints;
    QList<TestPoint> m_reactivePowerTestPoints;
    QList<TestPoint> m_apparentPowerTestPoints;
    QList<TestPoint> m_powerFactorTestPoints;
    QList<TestPoint> m_energyActiveTestPoints;
    QList<TestPoint> m_energyReactiveTestPoints;

    QElapsedTimer m_testTimer;
    QList<Meter> m_meters;
    // 新增一个成员变量保存当前模式
    WorkMode m_workMode = Mode_FullAuto;
    std::atomic<bool> m_isManualStop{false}; // 🌟 新增：专门记录是否是人为点击的停止
    float m_sourceErrorOffset = 0.1f; // 默认 0.1

    QByteArray buildSTRCmd(quint8 cmd, const QByteArray &data);
    // 🌟 通用压缩 BCD 转换：可精准控制小数位数(decPlaces)与生成的总字节长度(byteLen)
    QByteArray valToBCD(double val, int decPlaces, int byteLen = 4);
    float parseSTRFloat(const char *data);
    bool setSourceConfig(QSerialPort &port, float vol, float cur, float pf);
    // 🌟 一键读取标准源当前所有实时参数 (发送 A0，自动解析 7 帧数据)
    bool readSourceAllParams(QSerialPort &port, SourceParams &outParams);
    bool safeStopSource(QSerialPort &port);
    bool safeStartSource(QSerialPort &port);

    bool processActivePowerData(Meter &meter, const TestPoint &pt, const SourceParams &stdParams, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry);
    bool processReactivePowerData(Meter &meter, const TestPoint &pt, const SourceParams &stdParams, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry);
    bool processApparentPowerData(Meter &meter, const TestPoint &pt, const SourceParams &stdParams, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry);
    bool processPowerFactorData(Meter &meter, const TestPoint &pt, const SourceParams &stdParams, const QVector<float> &rawData, Row &row, QVariantList &qmlCells, bool isLastTry);
    bool processVoltageCurrentData(Meter &meter, const TestPoint &pt, const SourceParams &stdParams, const QVector<float> &viData, Row &volRow, QVariantList &volQmlCells, Row &curRow, QVariantList &curQmlCells, bool isLastTry);
    bool sendSTRHarmonicConfig(QSerialPort &port, quint8 ch, int order, float contentPct, float phaseDeg);

    bool readSourceFreq(QSerialPort &port, SourceParams &outParams);          // F0: 读频率
    bool readSourceActivePower(QSerialPort &port, SourceParams &outParams);   // F1: 读有功功率
    bool readSourceReactivePower(QSerialPort &port, SourceParams &outParams); // F2: 读无功功率
    bool readSourceApparentPower(QSerialPort &port, SourceParams &outParams); // F3: 读视在功率
    bool readSourcePowerFactor(QSerialPort &port, SourceParams &outParams);   // F4: 读功率因数
    bool readSourceVoltageCurrent(QSerialPort &port, SourceParams &outParams);// F6: 读电压与电流
    bool readSourcePhase(QSerialPort &port, SourceParams &outParams);
    bool readSourceParamsByCategory(QSerialPort &port, CategoryType catType, SourceParams &outParams);

    bool scanAndFindEnergyInterfaceCode(QSerialPort &port);

    void exportExcelReport(const Meter &meter);

    SourceParams m_sourceParams;

    void cleanOldReportFolders();
};

#endif // CALIBRATIONTHREAD_H