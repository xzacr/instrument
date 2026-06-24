#include "meter.h"

Meter::Meter(QObject *parent)
    : QObject{parent}
{}

// 手动触发崩溃,测试dmp文件
void Meter::triggerCpuCrash() {
    qInfo() << "[Test]" << " 正在执行【内核级】人工引爆 C++ 内存防线...";
    int* p = nullptr;
    *p = 12345; // 💣 故意制造空指针异常
}
