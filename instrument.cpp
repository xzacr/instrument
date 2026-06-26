#include "instrument.h"

Instrument::Instrument(QObject *parent)
    : QObject{parent}
{
    m_rs232 = new RS232(this);
    m_rs485 = new RS485(this);
}

// 手动触发崩溃,测试dmp文件
void Instrument::triggerCpuCrash() {
    qInfo() << "[Test]" << " 正在执行【内核级】人工引爆 C++ 内存防线...";
    int* p = nullptr;
    *p = 12345; // 💣 故意制造空指针异常
}
