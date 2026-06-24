// dump_handler.h
#ifndef DUMP_HANDLER_H
#define DUMP_HANDLER_H

#include <windows.h>
#include <dbghelp.h>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>

void cleanupOldDumpFiles()
{
    QString dumpPath = QCoreApplication::applicationDirPath() + "/crashes";
    QDir dir(dumpPath);

    if (!dir.exists()) return;


}

// 崩溃回调函数
LONG WINAPI exceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    // 创建 dump 文件夹（通常放在 AppData 或程序运行目录下）
    QString dumpPath = QCoreApplication::applicationDirPath() + "/crashes";
    QDir dir(dumpPath);
    if (!dir.exists()) {
        dir.mkpath(dumpPath);
    }
    // 设置过滤器：只扫描后缀为 .dmp 的物理文件，并排除 . 和 .. 目录
    dir.setNameFilters(QStringList() << "*.dmp");
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);

    // 核心大招：按文件修改时间从新到旧进行“时间轴物理排序”
    dir.setSorting(QDir::Time);

    QFileInfoList fileList = dir.entryInfoList();

    // 防火墙判定：如果数量大于 10 个，开始执行物理斩首
    if (fileList.size() > 10) {
        // fileList[0] 是最新的，fileList[fileList.size()-1] 是最老的
        // 从第 10 个索引开始，把后面所有陈旧的历史快照全部一刀切掉
        for (int i = 10; i < fileList.size(); ++i) {
            QDir().remove(fileList[i].absoluteFilePath()); // 物理毁灭最老的 .dmp，还硬盘清白
        }
    }

    // 命名 dump 文件
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString fileName = QString("%1/crash_%2.dmp").arg(dumpPath, timestamp);

    // 打开文件
    HANDLE hFile = CreateFileW(
        (const wchar_t*)fileName.utf16(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );

    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
        dumpInfo.ThreadId = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = exceptionInfo;
        dumpInfo.ClientPointers = FALSE;

        // 写入 MiniDump Windows内核级内存黑匣子快照落盘流
        BOOL success = MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            MiniDumpNormal, // 可根据需要选择 MiniDumpWithFullMemory 等
            &dumpInfo,
            NULL,
            NULL
            );

        CloseHandle(hFile);
    }


    // 弹窗提示或直接退出
    // QMessageBox::critical(nullptr, "Application Crash", "程序异常崩溃，已生成日志。");

    return EXCEPTION_EXECUTE_HANDLER;
}

// 注册异常捕获
void initCrashHandler() {
    // Windows系统级未捕获异常终极拦截器（上位机死后黑匣子防火墙）
    // exceptionFilter 自定义崩溃回调处理函数的指针
    SetUnhandledExceptionFilter(exceptionFilter);
}

#endif // DUMP_HANDLER_H
