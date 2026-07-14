#include "calibrationthread.h"
#include <QDebug>
#include <QThread>
#include <QtMath>
#include <QElapsedTimer>

CalibrationThread::CalibrationThread(QObject *parent)
    : QThread(parent), m_isRunning(false) {
    
    // =========================================================================
    // 1. 电压电流测试点 (全部 0.5%)
    // =========================================================================
    m_viTestPoints = {
        {"44V,  0.5A",  44.0f, 0.5f, 1.0f, 0.5f, buildSourceConfigCmd(44.0f,  0.5f, 1.0f)},
        {"220V, 5.0A", 220.0f, 5.0f, 1.0f, 0.5f, buildSourceConfigCmd(220.0f, 5.0f, 1.0f)},
        {"264V, 6.0A", 264.0f, 6.0f, 1.0f, 0.5f, buildSourceConfigCmd(264.0f, 6.0f, 1.0f)}
    };

    // =========================================================================
    // 2. 有功功率测试点 (限值：1.0% / 0.6% / 0.5%)
    // =========================================================================
    m_activePowerTestPoints = {
       // --- 第一组：176V ---
       {"176V, PF=1.0, 0.05A",  176.0f, 0.05f,  1.0f, 1.0f, buildSourceConfigCmd(176.0f, 0.05f,  1.0f)}, // idx 0
       {"176V, PF=1.0, 0.20A",  176.0f, 0.20f,  1.0f, 1.0f, buildSourceConfigCmd(176.0f, 0.20f,  1.0f)}, // idx 1
       {"176V, PF=1.0, 0.25A",  176.0f, 0.25f,  1.0f, 0.5f, buildSourceConfigCmd(176.0f, 0.25f,  1.0f)}, // idx 2
       {"176V, PF=1.0, 5.00A",  176.0f, 5.00f,  1.0f, 0.5f, buildSourceConfigCmd(176.0f, 5.00f,  1.0f)}, // idx 3
       {"176V, PF=1.0, 6.00A",  176.0f, 6.00f,  1.0f, 0.5f, buildSourceConfigCmd(176.0f, 6.00f,  1.0f)}, // idx 4
       {"176V, PF=0.5L, 0.10A", 176.0f, 0.10f,  0.5f, 1.0f, buildSourceConfigCmd(176.0f, 0.10f,  0.5f)}, // idx 5
       {"176V, PF=0.5L, 0.25A", 176.0f, 0.25f,  0.5f, 1.0f, buildSourceConfigCmd(176.0f, 0.25f,  0.5f)}, // idx 6
       {"176V, PF=0.5L, 0.40A", 176.0f, 0.40f,  0.5f, 1.0f, buildSourceConfigCmd(176.0f, 0.40f,  0.5f)}, // idx 7
       {"176V, PF=0.5L, 0.50A", 176.0f, 0.50f,  0.5f, 0.6f, buildSourceConfigCmd(176.0f, 0.50f,  0.5f)}, // idx 8
       {"176V, PF=0.5L, 5.00A", 176.0f, 5.00f,  0.5f, 0.6f, buildSourceConfigCmd(176.0f, 5.00f,  0.5f)}, // idx 9
       {"176V, PF=0.5L, 6.00A", 176.0f, 6.00f,  0.5f, 0.6f, buildSourceConfigCmd(176.0f, 6.00f,  0.5f)}, // idx 10
       {"176V, PF=0.8C, 0.10A", 176.0f, 0.10f, -0.8f, 1.0f, buildSourceConfigCmd(176.0f, 0.10f, -0.8f)}, // idx 11
       {"176V, PF=0.8C, 0.25A", 176.0f, 0.25f, -0.8f, 1.0f, buildSourceConfigCmd(176.0f, 0.25f, -0.8f)}, // idx 12
       {"176V, PF=0.8C, 0.40A", 176.0f, 0.40f, -0.8f, 1.0f, buildSourceConfigCmd(176.0f, 0.40f, -0.8f)}, // idx 13
       {"176V, PF=0.8C, 0.50A", 176.0f, 0.50f, -0.8f, 0.6f, buildSourceConfigCmd(176.0f, 0.50f, -0.8f)}, // idx 14
       {"176V, PF=0.8C, 5.00A", 176.0f, 5.00f, -0.8f, 0.6f, buildSourceConfigCmd(176.0f, 5.00f, -0.8f)}, // idx 15
       {"176V, PF=0.8C, 6.00A", 176.0f, 6.00f, -0.8f, 0.6f, buildSourceConfigCmd(176.0f, 6.00f, -0.8f)}, // idx 16

       // --- 第二组：220V ---
       {"220V, PF=1.0, 0.05A",  220.0f, 0.05f,  1.0f, 1.0f, buildSourceConfigCmd(220.0f, 0.05f,  1.0f)},
       {"220V, PF=1.0, 0.20A",  220.0f, 0.20f,  1.0f, 1.0f, buildSourceConfigCmd(220.0f, 0.20f,  1.0f)},
       {"220V, PF=1.0, 0.25A",  220.0f, 0.25f,  1.0f, 0.5f, buildSourceConfigCmd(220.0f, 0.25f,  1.0f)},
       {"220V, PF=1.0, 5.00A",  220.0f, 5.00f,  1.0f, 0.5f, buildSourceConfigCmd(220.0f, 5.00f,  1.0f)},
       {"220V, PF=1.0, 6.00A",  220.0f, 6.00f,  1.0f, 0.5f, buildSourceConfigCmd(220.0f, 6.00f,  1.0f)},
       {"220V, PF=0.5L, 0.10A", 220.0f, 0.10f,  0.5f, 1.0f, buildSourceConfigCmd(220.0f, 0.10f,  0.5f)},
       {"220V, PF=0.5L, 0.25A", 220.0f, 0.25f,  0.5f, 1.0f, buildSourceConfigCmd(220.0f, 0.25f,  0.5f)},
       {"220V, PF=0.5L, 0.40A", 220.0f, 0.40f,  0.5f, 1.0f, buildSourceConfigCmd(220.0f, 0.40f,  0.5f)},
       {"220V, PF=0.5L, 0.50A", 220.0f, 0.50f,  0.5f, 0.6f, buildSourceConfigCmd(220.0f, 0.50f,  0.5f)},
       {"220V, PF=0.5L, 5.00A", 220.0f, 5.00f,  0.5f, 0.6f, buildSourceConfigCmd(220.0f, 5.00f,  0.5f)},
       {"220V, PF=0.5L, 6.00A", 220.0f, 6.00f,  0.5f, 0.6f, buildSourceConfigCmd(220.0f, 6.00f,  0.5f)},
       {"220V, PF=0.8C, 0.10A", 220.0f, 0.10f, -0.8f, 1.0f, buildSourceConfigCmd(220.0f, 0.10f, -0.8f)},
       {"220V, PF=0.8C, 0.25A", 220.0f, 0.25f, -0.8f, 1.0f, buildSourceConfigCmd(220.0f, 0.25f, -0.8f)},
       {"220V, PF=0.8C, 0.40A", 220.0f, 0.40f, -0.8f, 1.0f, buildSourceConfigCmd(220.0f, 0.40f, -0.8f)},
       {"220V, PF=0.8C, 0.50A", 220.0f, 0.50f, -0.8f, 0.6f, buildSourceConfigCmd(220.0f, 0.50f, -0.8f)},
       {"220V, PF=0.8C, 5.00A", 220.0f, 5.00f, -0.8f, 0.6f, buildSourceConfigCmd(220.0f, 5.00f, -0.8f)},
       {"220V, PF=0.8C, 6.00A", 220.0f, 6.00f, -0.8f, 0.6f, buildSourceConfigCmd(220.0f, 6.00f, -0.8f)},

       // --- 第三组：264V ---
       {"264V, PF=1.0, 0.05A",  264.0f, 0.05f,  1.0f, 1.0f, buildSourceConfigCmd(264.0f, 0.05f,  1.0f)},
       {"264V, PF=1.0, 0.20A",  264.0f, 0.20f,  1.0f, 1.0f, buildSourceConfigCmd(264.0f, 0.20f,  1.0f)},
       {"264V, PF=1.0, 0.25A",  264.0f, 0.25f,  1.0f, 0.5f, buildSourceConfigCmd(264.0f, 0.25f,  1.0f)},
       {"264V, PF=1.0, 5.00A",  264.0f, 5.00f,  1.0f, 0.5f, buildSourceConfigCmd(264.0f, 5.00f,  1.0f)},
       {"264V, PF=1.0, 6.00A",  264.0f, 6.00f,  1.0f, 0.5f, buildSourceConfigCmd(264.0f, 6.00f,  1.0f)},
       {"264V, PF=0.5L, 0.10A", 264.0f, 0.10f,  0.5f, 1.0f, buildSourceConfigCmd(264.0f, 0.10f,  0.5f)},
       {"264V, PF=0.5L, 0.25A", 264.0f, 0.25f,  0.5f, 1.0f, buildSourceConfigCmd(264.0f, 0.25f,  0.5f)},
       {"264V, PF=0.5L, 0.40A", 264.0f, 0.40f,  0.5f, 1.0f, buildSourceConfigCmd(264.0f, 0.40f,  0.5f)},
       {"264V, PF=0.5L, 0.50A", 264.0f, 0.50f,  0.5f, 0.6f, buildSourceConfigCmd(264.0f, 0.50f,  0.5f)},
       {"264V, PF=0.5L, 5.00A", 264.0f, 5.00f,  0.5f, 0.6f, buildSourceConfigCmd(264.0f, 5.00f,  0.5f)},
       {"264V, PF=0.5L, 6.00A", 264.0f, 6.00f,  0.5f, 0.6f, buildSourceConfigCmd(264.0f, 6.00f,  0.5f)},
       {"264V, PF=0.8C, 0.10A", 264.0f, 0.10f, -0.8f, 1.0f, buildSourceConfigCmd(264.0f, 0.10f, -0.8f)},
       {"264V, PF=0.8C, 0.25A", 264.0f, 0.25f, -0.8f, 1.0f, buildSourceConfigCmd(264.0f, 0.25f, -0.8f)},
       {"264V, PF=0.8C, 0.40A", 264.0f, 0.40f, -0.8f, 1.0f, buildSourceConfigCmd(264.0f, 0.40f, -0.8f)},
       {"264V, PF=0.8C, 0.50A", 264.0f, 0.50f, -0.8f, 0.6f, buildSourceConfigCmd(264.0f, 0.50f, -0.8f)},
       {"264V, PF=0.8C, 5.00A", 264.0f, 5.00f, -0.8f, 0.6f, buildSourceConfigCmd(264.0f, 5.00f, -0.8f)},
       {"264V, PF=0.8C, 6.00A", 264.0f, 6.00f, -0.8f, 0.6f, buildSourceConfigCmd(264.0f, 6.00f, -0.8f)},
       };

    // =========================================================================
    // 3. 无功功率测试点 (限值：0.625% / 0.50%)
    // =========================================================================
    m_reactivePowerTestPoints = {
     // --- 第一组：176V ---
     {"176V, PF=0, 0.1A",      176.0f, 0.10f, 0.0f,   1.25f, buildSourceConfigCmd(176.0f, 0.10f, 0.0f)}, // idx 0
     {"176V, PF=0, 0.2A",      176.0f, 0.20f, 0.0f,   1.25f, buildSourceConfigCmd(176.0f, 0.20f, 0.0f)}, // idx 1
     {"176V, PF=0, 0.25A",     176.0f, 0.25f, 0.0f,   1.0f,   buildSourceConfigCmd(176.0f, 0.25f, 0.0f)}, // idx 2
     {"176V, PF=0, 5.0A",      176.0f, 5.00f, 0.0f,   1.0f,   buildSourceConfigCmd(176.0f, 5.00f, 0.0f)}, // idx 3
     {"176V, PF=0, 6.0A",      176.0f, 6.00f, 0.0f,   1.0f,   buildSourceConfigCmd(176.0f, 6.00f, 0.0f)}, // idx 4
     {"176V, PF=0.866, 0.25A", 176.0f, 0.25f, 0.866f, 1.25f, buildSourceConfigCmd(176.0f, 0.25f, 0.866f)}, // idx 5
     {"176V, PF=0.866, 0.4A",  176.0f, 0.40f, 0.866f, 1.25f, buildSourceConfigCmd(176.0f, 0.40f, 0.866f)}, // idx 6
     {"176V, PF=0.866, 0.5A",  176.0f, 0.50f, 0.866f, 1.0f,   buildSourceConfigCmd(176.0f, 0.50f, 0.866f)}, // idx 7
     {"176V, PF=0.866, 5.0A",  176.0f, 5.00f, 0.866f, 1.0f,   buildSourceConfigCmd(176.0f, 5.00f, 0.866f)}, // idx 8
     {"176V, PF=0.866, 6.0A",  176.0f, 6.00f, 0.866f, 1.0f,   buildSourceConfigCmd(176.0f, 6.00f, 0.866f)}, // idx 9
     {"176V, PF=0.968, 0.5A",  176.0f, 0.50f, 0.968f, 1.25f, buildSourceConfigCmd(176.0f, 0.50f, 0.968f)}, // idx 10
     {"176V, PF=0.968, 5.0A",  176.0f, 5.00f, 0.968f, 1.25f, buildSourceConfigCmd(176.0f, 5.00f, 0.968f)}, // idx 11
     {"176V, PF=0.968, 6.0A",  176.0f, 6.00f, 0.968f, 1.25f, buildSourceConfigCmd(176.0f, 6.00f, 0.968f)}, // idx 12

     // --- 第二组：220V ---
     {"220V, PF=0, 0.1A",      220.0f, 0.10f, 0.0f,   1.25f, buildSourceConfigCmd(220.0f, 0.10f, 0.0f)},
     {"220V, PF=0, 0.2A",      220.0f, 0.20f, 0.0f,   1.25f, buildSourceConfigCmd(220.0f, 0.20f, 0.0f)},
     {"220V, PF=0, 0.25A",     220.0f, 0.25f, 0.0f,   1.0f,   buildSourceConfigCmd(220.0f, 0.25f, 0.0f)},
     {"220V, PF=0, 5.0A",      220.0f, 5.00f, 0.0f,   1.0f,   buildSourceConfigCmd(220.0f, 5.00f, 0.0f)},
     {"220V, PF=0, 6.0A",      220.0f, 6.00f, 0.0f,   1.0f,   buildSourceConfigCmd(220.0f, 6.00f, 0.0f)},
     {"220V, PF=0.866, 0.25A", 220.0f, 0.25f, 0.866f, 1.25f, buildSourceConfigCmd(220.0f, 0.25f, 0.866f)},
     {"220V, PF=0.866, 0.4A",  220.0f, 0.40f, 0.866f, 1.25f, buildSourceConfigCmd(220.0f, 0.40f, 0.866f)},
     {"220V, PF=0.866, 0.5A",  220.0f, 0.50f, 0.866f, 1.0f,   buildSourceConfigCmd(220.0f, 0.50f, 0.866f)},
     {"220V, PF=0.866, 5.0A",  220.0f, 5.00f, 0.866f, 1.0f,   buildSourceConfigCmd(220.0f, 5.00f, 0.866f)},
     {"220V, PF=0.866, 6.0A",  220.0f, 6.00f, 0.866f, 1.0f,   buildSourceConfigCmd(220.0f, 6.00f, 0.866f)},
     {"220V, PF=0.968, 0.5A",  220.0f, 0.50f, 0.968f, 1.25f, buildSourceConfigCmd(220.0f, 0.50f, 0.968f)},
     {"220V, PF=0.968, 5.0A",  220.0f, 5.00f, 0.968f, 1.25f, buildSourceConfigCmd(220.0f, 5.00f, 0.968f)},
     {"220V, PF=0.968, 6.0A",  220.0f, 6.00f, 0.968f, 1.25f, buildSourceConfigCmd(220.0f, 6.00f, 0.968f)},

     // --- 第三组：264V ---
     {"264V, PF=0, 0.1A",      264.0f, 0.10f, 0.0f,   1.25f, buildSourceConfigCmd(264.0f, 0.10f, 0.0f)},
     {"264V, PF=0, 0.2A",      264.0f, 0.20f, 0.0f,   1.25f, buildSourceConfigCmd(264.0f, 0.20f, 0.0f)},
     {"264V, PF=0, 0.25A",     264.0f, 0.25f, 0.0f,   1.0f,   buildSourceConfigCmd(264.0f, 0.25f, 0.0f)},
     {"264V, PF=0, 5.0A",      264.0f, 5.00f, 0.0f,   1.0f,   buildSourceConfigCmd(264.0f, 5.00f, 0.0f)},
     {"264V, PF=0, 6.0A",      264.0f, 6.00f, 0.0f,   1.0f,   buildSourceConfigCmd(264.0f, 6.00f, 0.0f)},
     {"264V, PF=0.866, 0.25A", 264.0f, 0.25f, 0.866f, 1.25f, buildSourceConfigCmd(264.0f, 0.25f, 0.866f)},
     {"264V, PF=0.866, 0.4A",  264.0f, 0.40f, 0.866f, 1.25f, buildSourceConfigCmd(264.0f, 0.40f, 0.866f)},
     {"264V, PF=0.866, 0.5A",  264.0f, 0.50f, 0.866f, 1.0f,   buildSourceConfigCmd(264.0f, 0.50f, 0.866f)},
     {"264V, PF=0.866, 5.0A",  264.0f, 5.00f, 0.866f, 1.0f,   buildSourceConfigCmd(264.0f, 5.00f, 0.866f)},
     {"264V, PF=0.866, 6.0A",  264.0f, 6.00f, 0.866f, 1.0f,   buildSourceConfigCmd(264.0f, 6.00f, 0.866f)},
     {"264V, PF=0.968, 0.5A",  264.0f, 0.50f, 0.968f, 1.25f, buildSourceConfigCmd(264.0f, 0.50f, 0.968f)},
     {"264V, PF=0.968, 5.0A",  264.0f, 5.00f, 0.968f, 1.25f, buildSourceConfigCmd(264.0f, 5.00f, 0.968f)},
     {"264V, PF=0.968, 6.0A",  264.0f, 6.00f, 0.968f, 1.25f, buildSourceConfigCmd(264.0f, 6.00f, 0.968f)},
     };

    // =========================================================================
    // 4. 视在功率测试点 (限值：1.000% / 0.50%)
    // =========================================================================
    m_apparentPowerTestPoints = {
     // --- 176V 组 ---
     {"176V, PF=1, 0.1A", 176.0f, 0.1f, 1.0f, 1.0f, buildSourceConfigCmd(176.0f, 0.1f, 1.0f)}, // idx 0
     {"176V, PF=1, 0.2A", 176.0f, 0.2f, 1.0f, 1.0f, buildSourceConfigCmd(176.0f, 0.2f, 1.0f)}, // idx 1
     {"176V, PF=1, 0.3A", 176.0f, 0.3f, 1.0f, 0.5f, buildSourceConfigCmd(176.0f, 0.3f, 1.0f)}, // idx 2
     {"176V, PF=1, 5.0A", 176.0f, 5.0f, 1.0f, 0.5f, buildSourceConfigCmd(176.0f, 5.0f, 1.0f)}, // idx 3
     {"176V, PF=1, 6.0A", 176.0f, 6.0f, 1.0f, 0.5f, buildSourceConfigCmd(176.0f, 6.0f, 1.0f)}, // idx 4

     // --- 220V 组 ---
     {"220V, PF=1, 0.1A", 220.0f, 0.1f, 1.0f, 1.0f, buildSourceConfigCmd(220.0f, 0.1f, 1.0f)},
     {"220V, PF=1, 0.2A", 220.0f, 0.2f, 1.0f, 1.0f, buildSourceConfigCmd(220.0f, 0.2f, 1.0f)},
     {"220V, PF=1, 0.3A", 220.0f, 0.3f, 1.0f, 0.5f, buildSourceConfigCmd(220.0f, 0.3f, 1.0f)},
     {"220V, PF=1, 5.0A", 220.0f, 5.0f, 1.0f, 0.5f, buildSourceConfigCmd(220.0f, 5.0f, 1.0f)},
     {"220V, PF=1, 6.0A", 220.0f, 6.0f, 1.0f, 0.5f, buildSourceConfigCmd(220.0f, 6.0f, 1.0f)},

     // --- 264V 组 ---
     {"264V, PF=1, 0.1A", 264.0f, 0.1f, 1.0f, 1.0f, buildSourceConfigCmd(264.0f, 0.1f, 1.0f)},
     {"264V, PF=1, 0.2A", 264.0f, 0.2f, 1.0f, 1.0f, buildSourceConfigCmd(264.0f, 0.2f, 1.0f)},
     {"264V, PF=1, 0.3A", 264.0f, 0.3f, 1.0f, 0.5f, buildSourceConfigCmd(264.0f, 0.3f, 1.0f)},
     {"264V, PF=1, 5.0A", 264.0f, 5.0f, 1.0f, 0.5f, buildSourceConfigCmd(264.0f, 5.0f, 1.0f)},
     {"264V, PF=1, 6.0A", 264.0f, 6.0f, 1.0f, 0.5f, buildSourceConfigCmd(264.0f, 6.0f, 1.0f)},
     };

    m_powerFactorTestPoints = {
       // --- 第一组：110V (12个点) ---
       {"110V, PF=0.5L, 0.5A", 110.0f, 0.5f,  0.5f, 0.5f, buildSourceConfigCmd(110.0f, 0.5f,  0.5f)},
       {"110V, PF=0.5L, 2.5A", 110.0f, 2.5f,  0.5f, 0.5f, buildSourceConfigCmd(110.0f, 2.5f,  0.5f)},
       {"110V, PF=0.5L, 5.0A", 110.0f, 5.0f,  0.5f, 0.5f, buildSourceConfigCmd(110.0f, 5.0f,  0.5f)},
       {"110V, PF=0.5L, 6.0A", 110.0f, 6.0f,  0.5f, 0.5f, buildSourceConfigCmd(110.0f, 6.0f,  0.5f)},
       {"110V, PF=1.0, 0.5A",  110.0f, 0.5f,  1.0f, 0.5f, buildSourceConfigCmd(110.0f, 0.5f,  1.0f)},
       {"110V, PF=1.0, 2.5A",  110.0f, 2.5f,  1.0f, 0.5f, buildSourceConfigCmd(110.0f, 2.5f,  1.0f)},
       {"110V, PF=1.0, 5.0A",  110.0f, 5.0f,  1.0f, 0.5f, buildSourceConfigCmd(110.0f, 5.0f,  1.0f)},
       {"110V, PF=1.0, 6.0A",  110.0f, 6.0f,  1.0f, 0.5f, buildSourceConfigCmd(110.0f, 6.0f,  1.0f)},
       {"110V, PF=0.8C, 0.5A", 110.0f, 0.5f, -0.8f, 0.5f, buildSourceConfigCmd(110.0f, 0.5f, -0.8f)},
       {"110V, PF=0.8C, 2.5A", 110.0f, 2.5f, -0.8f, 0.5f, buildSourceConfigCmd(110.0f, 2.5f, -0.8f)},
       {"110V, PF=0.8C, 5.0A", 110.0f, 5.0f, -0.8f, 0.5f, buildSourceConfigCmd(110.0f, 5.0f, -0.8f)},
       {"110V, PF=0.8C, 6.0A", 110.0f, 6.0f, -0.8f, 0.5f, buildSourceConfigCmd(110.0f, 6.0f, -0.8f)},

       // --- 第二组：220V (12个点) ---
       {"220V, PF=0.5L, 0.5A", 220.0f, 0.5f,  0.5f, 0.5f, buildSourceConfigCmd(220.0f, 0.5f,  0.5f)},
       {"220V, PF=0.5L, 2.5A", 220.0f, 2.5f,  0.5f, 0.5f, buildSourceConfigCmd(220.0f, 2.5f,  0.5f)},
       {"220V, PF=0.5L, 5.0A", 220.0f, 5.0f,  0.5f, 0.5f, buildSourceConfigCmd(220.0f, 5.0f,  0.5f)},
       {"220V, PF=0.5L, 6.0A", 220.0f, 6.0f,  0.5f, 0.5f, buildSourceConfigCmd(220.0f, 6.0f,  0.5f)},
       {"220V, PF=1.0, 0.5A",  220.0f, 0.5f,  1.0f, 0.5f, buildSourceConfigCmd(220.0f, 0.5f,  1.0f)},
       {"220V, PF=1.0, 2.5A",  220.0f, 2.5f,  1.0f, 0.5f, buildSourceConfigCmd(220.0f, 2.5f,  1.0f)},
       {"220V, PF=1.0, 5.0A",  220.0f, 5.0f,  1.0f, 0.5f, buildSourceConfigCmd(220.0f, 5.0f,  1.0f)},
       {"220V, PF=1.0, 6.0A",  220.0f, 6.0f,  1.0f, 0.5f, buildSourceConfigCmd(220.0f, 6.0f,  1.0f)},
       {"220V, PF=0.8C, 0.5A", 220.0f, 0.5f, -0.8f, 0.5f, buildSourceConfigCmd(220.0f, 0.5f, -0.8f)},
       {"220V, PF=0.8C, 2.5A", 220.0f, 2.5f, -0.8f, 0.5f, buildSourceConfigCmd(220.0f, 2.5f, -0.8f)},
       {"220V, PF=0.8C, 5.0A", 220.0f, 5.0f, -0.8f, 0.5f, buildSourceConfigCmd(220.0f, 5.0f, -0.8f)},
       {"220V, PF=0.8C, 6.0A", 220.0f, 6.0f, -0.8f, 0.5f, buildSourceConfigCmd(220.0f, 6.0f, -0.8f)},

       // --- 第三组：264V (12个点) ---
       {"264V, PF=0.5L, 0.5A", 264.0f, 0.5f,  0.5f, 0.5f, buildSourceConfigCmd(264.0f, 0.5f,  0.5f)},
       {"264V, PF=0.5L, 2.5A", 264.0f, 2.5f,  0.5f, 0.5f, buildSourceConfigCmd(264.0f, 2.5f,  0.5f)},
       {"264V, PF=0.5L, 5.0A", 264.0f, 5.0f,  0.5f, 0.5f, buildSourceConfigCmd(264.0f, 5.0f,  0.5f)},
       {"264V, PF=0.5L, 6.0A", 264.0f, 6.0f,  0.5f, 0.5f, buildSourceConfigCmd(264.0f, 6.0f,  0.5f)},
       {"264V, PF=1.0, 0.5A",  264.0f, 0.5f,  1.0f, 0.5f, buildSourceConfigCmd(264.0f, 0.5f,  1.0f)},
       {"264V, PF=1.0, 2.5A",  264.0f, 2.5f,  1.0f, 0.5f, buildSourceConfigCmd(264.0f, 2.5f,  1.0f)},
       {"264V, PF=1.0, 5.0A",  264.0f, 5.0f,  1.0f, 0.5f, buildSourceConfigCmd(264.0f, 5.0f,  1.0f)},
       {"264V, PF=1.0, 6.0A",  264.0f, 6.0f,  1.0f, 0.5f, buildSourceConfigCmd(264.0f, 6.0f,  1.0f)},
       {"264V, PF=0.8C, 0.5A", 264.0f, 0.5f, -0.8f, 0.5f, buildSourceConfigCmd(264.0f, 0.5f, -0.8f)},
       {"264V, PF=0.8C, 2.5A", 264.0f, 2.5f, -0.8f, 0.5f, buildSourceConfigCmd(264.0f, 2.5f, -0.8f)},
       {"264V, PF=0.8C, 5.0A", 264.0f, 5.0f, -0.8f, 0.5f, buildSourceConfigCmd(264.0f, 5.0f, -0.8f)},
       {"264V, PF=0.8C, 6.0A", 264.0f, 6.0f, -0.8f, 0.5f, buildSourceConfigCmd(264.0f, 6.0f, -0.8f)},
       };
    m_energyActiveTestPoints = {
        // --- PF=1.0 (5个点) ---
        {"220V, PF=1.0, 0.05A",  220.0f, 0.05f,  1.0f, 1.0f, buildSourceConfigCmd(220.0f, 0.05f,  1.0f)},
        {"220V, PF=1.0, 0.20A",  220.0f, 0.20f,  1.0f, 1.0f, buildSourceConfigCmd(220.0f, 0.20f,  1.0f)},
        {"220V, PF=1.0, 0.25A",  220.0f, 0.25f,  1.0f, 0.5f, buildSourceConfigCmd(220.0f, 0.25f,  1.0f)},
        {"220V, PF=1.0, 5.00A",  220.0f, 5.00f,  1.0f, 0.5f, buildSourceConfigCmd(220.0f, 5.00f,  1.0f)},
        {"220V, PF=1.0, 6.00A",  220.0f, 6.00f,  1.0f, 0.5f, buildSourceConfigCmd(220.0f, 6.00f,  1.0f)},

        // --- PF=0.5L (6个点) ---
        {"220V, PF=0.5L, 0.10A", 220.0f, 0.10f,  0.5f, 1.0f, buildSourceConfigCmd(220.0f, 0.10f,  0.5f)},
        {"220V, PF=0.5L, 0.25A", 220.0f, 0.25f,  0.5f, 1.0f, buildSourceConfigCmd(220.0f, 0.25f,  0.5f)},
        {"220V, PF=0.5L, 0.40A", 220.0f, 0.40f,  0.5f, 1.0f, buildSourceConfigCmd(220.0f, 0.40f,  0.5f)},
        {"220V, PF=0.5L, 0.50A", 220.0f, 0.50f,  0.5f, 0.6f, buildSourceConfigCmd(220.0f, 0.50f,  0.5f)},
        {"220V, PF=0.5L, 5.00A", 220.0f, 5.00f,  0.5f, 0.6f, buildSourceConfigCmd(220.0f, 5.00f,  0.5f)},
        {"220V, PF=0.5L, 6.00A", 220.0f, 6.00f,  0.5f, 0.6f, buildSourceConfigCmd(220.0f, 6.00f,  0.5f)},

        // --- PF=0.8C (6个点) ---
        {"220V, PF=0.8C, 0.10A", 220.0f, 0.10f, -0.8f, 1.0f, buildSourceConfigCmd(220.0f, 0.10f, -0.8f)},
        {"220V, PF=0.8C, 0.25A", 220.0f, 0.25f, -0.8f, 1.0f, buildSourceConfigCmd(220.0f, 0.25f, -0.8f)},
        {"220V, PF=0.8C, 0.40A", 220.0f, 0.40f, -0.8f, 1.0f, buildSourceConfigCmd(220.0f, 0.40f, -0.8f)},
        {"220V, PF=0.8C, 0.50A", 220.0f, 0.50f, -0.8f, 0.6f, buildSourceConfigCmd(220.0f, 0.50f, -0.8f)},
        {"220V, PF=0.8C, 5.00A", 220.0f, 5.00f, -0.8f, 0.6f, buildSourceConfigCmd(220.0f, 5.00f, -0.8f)},
        {"220V, PF=0.8C, 6.00A", 220.0f, 6.00f, -0.8f, 0.6f, buildSourceConfigCmd(220.0f, 6.00f, -0.8f)}
    };

    m_energyReactiveTestPoints = {
        // --- PF=0 (5个点) ---
        {"220V, PF=0, 0.10A",      220.0f, 0.10f, 0.0f,   1.25f, buildSourceConfigCmd(220.0f, 0.10f, 0.0f)},
        {"220V, PF=0, 0.20A",      220.0f, 0.20f, 0.0f,   1.25f, buildSourceConfigCmd(220.0f, 0.20f, 0.0f)},
        {"220V, PF=0, 0.25A",      220.0f, 0.25f, 0.0f,   1.0f,  buildSourceConfigCmd(220.0f, 0.25f, 0.0f)},
        {"220V, PF=0, 5.00A",      220.0f, 5.00f, 0.0f,   1.0f,  buildSourceConfigCmd(220.0f, 5.00f, 0.0f)},
        {"220V, PF=0, 6.00A",      220.0f, 6.00f, 0.0f,   1.0f,  buildSourceConfigCmd(220.0f, 6.00f, 0.0f)},

        // --- PF=0.866 (5个点) ---
        {"220V, PF=0.866, 0.25A",  220.0f, 0.25f, 0.866f, 1.25f, buildSourceConfigCmd(220.0f, 0.25f, 0.866f)},
        {"220V, PF=0.866, 0.40A",  220.0f, 0.40f, 0.866f, 1.25f, buildSourceConfigCmd(220.0f, 0.40f, 0.866f)},
        {"220V, PF=0.866, 0.50A",  220.0f, 0.50f, 0.866f, 1.0f,  buildSourceConfigCmd(220.0f, 0.50f, 0.866f)},
        {"220V, PF=0.866, 5.00A",  220.0f, 5.00f, 0.866f, 1.0f,  buildSourceConfigCmd(220.0f, 5.00f, 0.866f)},
        {"220V, PF=0.866, 6.00A",  220.0f, 6.00f, 0.866f, 1.0f,  buildSourceConfigCmd(220.0f, 6.00f, 0.866f)},

        // --- PF=0.968246 (3个点) ---
        {"220V, PF=0.968246, 0.50A", 220.0f, 0.50f, 0.968246f, 1.25f, buildSourceConfigCmd(220.0f, 0.50f, 0.968246f)},
        {"220V, PF=0.968246, 5.00A", 220.0f, 5.00f, 0.968246f, 1.25f, buildSourceConfigCmd(220.0f, 5.00f, 0.968246f)},
        {"220V, PF=0.968246, 6.00A", 220.0f, 6.00f, 0.968246f, 1.25f, buildSourceConfigCmd(220.0f, 6.00f, 0.968246f)}
    };
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
    // bool isAllSuccess = true;

    // // 1. 组装展示标题
    // QString title = isAllSuccess ? "测试全部通过" : "测试未完成 / 超差";

    // // 2. 组装详细内容 (C++ 里直接用 \n 换行，QML 能够完美识别)
    // QString msg = QString("共选中 %1 台被测仪表\n通过：%2 台  |  失败：%3 台\n\n%4")
    //                   .arg(5)
    //                   .arg(5)
    //                   .arg(0)
    //                   .arg(isAllSuccess ? "所有数据已校验完毕！" : "请检查失败仪表的通信或接线！");

    // // 3. 确定弹窗类型
    // QString type = isAllSuccess ? "success" : "error";

    // // 🌟 4. 发射信号！
    // emit showResultPopup(title, msg, type);
    // return;
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
        if (!runEnergyCalcFlow(srcPort, meterPort, meters, aliveCount)) {
            goto ABORT_PROCESS;
        }
    }

    // 全部通关或彻底结束！
    if (m_isRunning) {
        qInfo() << "====== 流程圆满结束，有效仪表数：" << aliveCount << " ======";
        emit showTopMessage(QString("测试流程执行完毕，成功仪表数 %1 台").arg(aliveCount), "info");
        goto SUCCESS_EXIT;
    }

ABORT_PROCESS:
    qWarning() << ">>> ABORT_PROCESS 流程异常中断！正在向物理总线追发强停命令...";
    if (m_isManualStop) {
        for (const auto &m : std::as_const(meters)) {
            if (m.isEnabled) {
                // 强制将卡片刷回灰色(Error_Idle)空闲状态
                emit updateErrorMeterStatus(m.uiIndex, Error_Stop, "操作员手动停止");
                emit showResultPopup("操作员手动停止","","error");
            }
        }
    }else{
        emit showResultPopup("流程异常中断","","error");
    }

SUCCESS_EXIT:
    qInfo() << "正在停止标准源...";
    emit srcMessage("正在停止标准源...", "success");
    aliveCount = 0;
    int fail = 0;
    for (auto &meter : meters) {
        if(meter.isEnabled){
            if (meter.hasFail){
                fail++;
            }else{
                aliveCount++;
            }
        }
    }
    if(fail == 0)
        emit showResultPopup("测试全部完成!",QString("成功 %1 台, 失败 %2 台").arg(aliveCount).arg(fail),"success");
    else
        emit showResultPopup("测试全部完成!",QString("成功 %1 台, 失败 %2 台").arg(aliveCount).arg(fail),"error");
    if (sendSourceCmd(srcPort, m_stopCmd, 6000)){
        qInfo().noquote() << "[Rx 源强停确认]" << srcPort.readAll().toHex(' ').toUpper();
        emit srcMessage("Stop / 已停止", "success");
    }else{
        qWarning("标准源停止失败");
    }
}

// -------------------------------------------------------------------------
// 抽离出的纯校准流水线 (注意将原来的 goto ABORT_PROCESS 全部换成了 return)
// -------------------------------------------------------------------------
bool CalibrationThread::runCalibrationFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, int &aliveCount)
{
    qInfo() << ">>> 开始批量下发 [复位指令]...";

    for (auto &meter : meters) {
        if (!meter.isAlive) continue;
        //emit meterStepStatusChanged(meter.uiIndex, Step_Reset, State_Running);
        writeMeterReg(meterPort, meter.address, m_regReset1, 1);
        QThread::msleep(500);
    }

    qInfo() << ">>> 所有表复位已下发，总线静默 1500ms 等待单片机重启...";
    QThread::msleep(2000);
    // =========================================================
    // 阶段一：标准源输出 220V 5A 1.0PF
    // =========================================================
    qDebug("1. 下发 220V 5A 1.0PF 配置...");
    if (!sendSourceCmd(srcPort, m_cfgCmd1)) return false;

    qDebug("2. 启动标准源输出...");
    emit srcMessage("220V/5A/PF=1.0 等待源稳定...", "info");
    if (!sendSourceCmd(srcPort, m_startCmd, 6000)) return false;

    // QThread::msleep(300);

    // qInfo() << "正在全通道监测物理输出，验证三相配置...";
    // if (!waitSourceStable(srcPort, 1.0f, 1000)) return false;
    qDebug("3. 等待源和仪表稳定...");
    QThread::msleep(5000);

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
    if (!sendSourceCmd(srcPort, m_cfgCmd2)) return false;

    qDebug("8. 启动标准源输出...");
    emit srcMessage("220V/5A/PF=0.5 等待源稳定...", "info");
    if (!sendSourceCmd(srcPort, m_startCmd, 6000)) return false;

    // QThread::msleep(300);

    // qInfo() << "正在全通道监测物理输出，验证三相全量配置(0.5PF)...";
    // if (!waitSourceStable(srcPort, 0.5f, 1000)) return false;

    QThread::msleep(5000);
    qDebug("3. 等待源和仪表稳定...");
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
    aliveCount = 0;
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
    return true;
}

// =========================================================================
// 主入口：误差计算大循环
// =========================================================================
bool CalibrationThread::runErrorCalcFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, int &aliveCount)
{
    qInfo() << "====== 正在启动 误差测试系统 ======";

    for (auto &meter : meters) {
        if (!meter.isEnabled) continue;

        emit updateErrorMeterStatus(meter.uiIndex, Error_Running, "正在测试...");
    }


    //1. 执行电压电流测试 (地址 0x1018, 9个参数)
    qInfo() << "====== 1. 执行电压电流测试 ======";
    if (!runTestCategory(srcPort, meterPort, Cat_V, 0x1018, 9, m_viTestPoints, meters, aliveCount)) {
        return false;
    }

    // 2. 执行有功功率测试
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

    // 4. 结算状态
    int pass = 0, fail = 0;
    for (auto &meter : meters) {
        if (meter.isEnabled && meter.isAlive) {
            if (meter.hasFail) {
                fail++;
                emit updateErrorMeterStatus(meter.uiIndex, Error_Fail, "有超标项");
            } else {
                pass++;
                emit updateErrorMeterStatus(meter.uiIndex, Error_Pass, "全部合格");
            }
        }
    }

    qDebug("误差测试完毕: %d 台合格, %d 台超标, %d 台未启用或超时",pass,fail,5-pass-fail);

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
    quint16 len = data.size() + 8; // 长度包含地址、命令和数据
    frame.append(len & 0xFF).append((len >> 8) & 0xFF);
    frame.append(0x68).append((char)0x00);
    frame.append((char)0x81).append((char)0xAA); // 0x81 AA 写电能参数
    frame.append(data);

    quint8 cs = 0;
    for (int i = 4; i < frame.size(); ++i) cs += (quint8)frame[i];
    frame.append(cs).append(0x16);

    return frame;
}

bool CalibrationThread::readStandardEnergy(QSerialPort &srcPort, float &outActiveEnergy, float &outReactiveEnergy)
{
    // 发送 0x81 55 查询
    srcPort.write(m_queryEnergyCmd);
    if (!checkSourceResponse(srcPort, 1500)) return false;

    QByteArray rx = srcPort.readAll();
    // 校验头部和指令类型
    if (rx.size() >= 489 && rx[0] == 0x68 && (quint8)rx[5] == 0x81 && (quint8)rx[6] == 0x55) {
        // YMData[0] 起始索引为: 头(7字节) + YMPara(256字节) = 263
        int baseIdx = 263;

        // 按照结构体偏移量提取 Ep(有功电能) 和 Eq(无功电能)
        memcpy(&outActiveEnergy, rx.constData() + baseIdx + 16, 4);   // offset 16 is Ep
        memcpy(&outReactiveEnergy, rx.constData() + baseIdx + 20, 4); // offset 20 is Eq
        return true;
    }
    return false;
}

bool CalibrationThread::runEnergyCategory(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, const QList<TestPoint> &testPoints, int categoryIdx, bool isActive)
{
    for (const auto &pt : testPoints) {
        if (!m_isRunning) return false;

        qInfo() << ">>> 准备测试工况:" << pt.name << " 精度要求:" << pt.limit << "%";

        // 1. 下发源电压电流配置，并启动
        if (!sendSourceCmd(srcPort, pt.srcCmd)) return false;
        QThread::msleep(300);
        if (!sendSourceCmd(srcPort, m_startCmd, 6000)) return false;

        qDebug(">>> 等待标准源输出稳定 (10秒)...");
        for (int i = 0; i < 10; ++i) {
            if (!m_isRunning) return false;
            QThread::msleep(1000);
        }

        // 2. [核心动作] 砸出清零大报文！
        int checkType = isActive ? 1 : 0;
        QByteArray clearCmd = buildEnergyClearCmd(checkType);
        qInfo() << ">>> 下发标准源清零报文...";
        if (!sendSourceCmd(srcPort, clearCmd, 2000)) return false;

        // 3. 批量清零仪表
        for (auto &m : meters) {
            if (m.isEnabled && m.isAlive) {
                emit updateErrorMeterStatus(m.uiIndex, Error_Running, "正在清零...");
                //clearMeterEnergy(meterPort, m.address);
            }
        }

        // 4. 进入 1秒轮询走字大循环 (暂定跑 20 秒出成绩，可根据现场实际情况改大)
        int testSeconds = 20;
        for (int sec = 1; sec <= testSeconds; ++sec) {
            if (!m_isRunning) return false;

            // 发个 0x82 心跳维持状态
            srcPort.write(m_queryStatusCmd);
            checkSourceResponse(srcPort, 200);

            // 读标准源电能
            float stdActive = 0, stdReactive = 0;
            float stdEnergy = 0;
            if (readStandardEnergy(srcPort, stdActive, stdReactive)) {
                stdEnergy = isActive ? stdActive : stdReactive;
            }

            // 读每块表并推送给 QML
            for (auto &m : meters) {
                if (!m.isEnabled || !m.isAlive) continue;

                emit updateErrorMeterStatus(m.uiIndex, Error_Running, QString("走字中: %1s / %2s").arg(sec).arg(testSeconds));

                float meterEnergy = 0;
                //readMeterEnergy(meterPort, m.address, meterEnergy, isActive);

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
                emit appendErrorRow(m.uiIndex, categoryIdx, pt.name, qmlCells);

                // 如果是本工况测试的最后一秒，最终判定生死
                if (sec == testSeconds && isFail) {
                    m.hasFail = true;
                }
            }

            QThread::msleep(1000); // 严格等待一秒
        }
    }
    return true;
}

bool CalibrationThread::runEnergyCalcFlow(QSerialPort &srcPort, QSerialPort &meterPort, QList<Meter> &meters, int &aliveCount)
{
    qInfo() << "====== 正在启动 [电能走字] 测试系统 ======";

    // 1. 跑有功 (category 0)
    qInfo() << "=== 启动有功电能走字测试 ===";
    if (!runEnergyCategory(srcPort, meterPort, meters, m_energyActiveTestPoints, 0, true)) return false;

    // 2. 跑无功 (category 1)
    qInfo() << "=== 启动无功电能走字测试 ===";
    if (!runEnergyCategory(srcPort, meterPort, meters, m_energyReactiveTestPoints, 1, false)) return false;

    // 结算
    aliveCount = 0;
    int pass = 0, fail = 0;
    for (auto &meter : meters) {
        if (meter.isEnabled && meter.isAlive) {
            aliveCount++;
            if (meter.hasFail) {
                fail++;
                emit updateErrorMeterStatus(meter.uiIndex, Error_Fail, "有超标项");
            } else {
                pass++;
                emit updateErrorMeterStatus(meter.uiIndex, Error_Pass, "全部合格");
            }
        }
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

    qInfo() << ">>> 开始批量执行 [模式初始化]，模式码:" << 7;
    aliveCount = 0;
    for (auto &meter : meters) {
        if (!meter.isAlive || !meter.isEnabled) continue; // 必须是存活的表才操作

        writeMeterReg(meterPort, meter.address, m_regWriteProtect, 7);
        // 校验模式是否写入成功
        if (waitMeterState(meterPort, meter.address, m_regWriteProtect, 7, 2000)) {
            aliveCount++; // 只有写入成功的表，才算活着，才进入下一步测试
        } else {
            // 写入失败
            meter.isAlive = false;
            emit updateErrorMeterStatus(meter.uiIndex, Error_Timeout, "模式设置超时");
        }
    }

    // 防呆：如果全军覆没，立刻终止，绝不跑空
    if (aliveCount == 0) {
        emit showTopMessage("全部仪表模式设置失败，测试终止", "error");
        return false;
    }

    for (int h = 2; h <= 31; ++h) {
        if(h != 2 && h != 5 && h != 7 && h != 11) continue;
        QString rowName = QString("220V/5A, %1次谐波").arg(h);
        qInfo() << "\n 正在测试:" << rowName;

        QMap<int, QVariantList> volCellsMap;
        QMap<int, QVariantList> curCellsMap;

        // 1. 准备专用的 Row 容器，用于存放数据和超差状态
        QMap<int, Row> volRowMap;
        QMap<int, Row> curRowMap;

        quint16 regAddr = 0x3030 + (h - 2) * 6; // 计算点表地址

        for (auto &m : meters) {
            if (m.isAlive) {
                emit updateErrorMeterStatus(m.uiIndex, Error_Running, "正在测: " + rowName);

                // 2. 为存活的表初始化 6 个空格子，准备接收数据
                volRowMap[m.uiIndex].conditionName = rowName;
                volRowMap[m.uiIndex].cells.resize(6);

                curRowMap[m.uiIndex].conditionName = rowName;
                curRowMap[m.uiIndex].cells.resize(6);
            }
        }

        // ========================================================
        // 步骤 A：低含量组 (电压 3%, 电流 10%)
        // ========================================================
        qInfo() << "  -> 执行低含量配置 (V:3%, I:10%)...";

        qDebug("1. 下发 220V 5A 1.0PF 配置...");
        if (!sendSourceCmd(srcPort, m_cfgCmd1)) return false;
        QThread::msleep(300);

        qDebug("2. 启动标准源基波输出...");
        if (!sendSourceCmd(srcPort, m_startCmd, 6000)) return false;
        QThread::msleep(300);

        qDebug("3. 下发 %d 次谐波参数 (V:3%%, I:10%%)...", h);
        if (!sendSourceCmd(srcPort, buildHarmonicCmd(h, 3.0f, 10.0f))) return false;
        QThread::msleep(300);

        qDebug("4. 启动全体谐波输出 (40 秒延时)...");
        if (!sendSourceCmd(srcPort, startAllHarmonicCmd, 40000)) return false;
        if (!m_isRunning) return false;

        QThread::msleep(10000);

        // 读表
        aliveCount = 0;
        for (auto &meter : meters) {
            if (!meter.isAlive) continue;
            QVector<float> rawA;
            if (readMeterHarmonicData16(meterPort, meter.address, regAddr, 6, rawA)) {
                aliveCount++;
                // 3. 取消临时变量 c，直接存入 volRowMap 的前 3 个格子，同步记录超差状态
                volCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Ua", 3.0f, rawA[0], volRowMap[meter.uiIndex].cells[0], 0.15f, rowName);
                volCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Ub", 3.0f, rawA[1], volRowMap[meter.uiIndex].cells[1], 0.15f, rowName);
                volCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Uc", 3.0f, rawA[2], volRowMap[meter.uiIndex].cells[2], 0.15f, rowName);

                curCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Ia", 10.0f, rawA[3], curRowMap[meter.uiIndex].cells[0], 0.5f, rowName);
                curCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Ib", 10.0f, rawA[4], curRowMap[meter.uiIndex].cells[1], 0.5f, rowName);
                curCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Ic", 10.0f, rawA[5], curRowMap[meter.uiIndex].cells[2], 0.5f, rowName);
            }else{
                meter.isAlive = false;
                emit updateErrorMeterStatus(meter.uiIndex, Error_Timeout, "通讯失败");
            }
            QThread::msleep(100);
        }
        if (!m_isRunning) return false;
        if (aliveCount == 0) {
            qWarning() << "所有被测表计均已掉线，终止谐波测试！";
            return false;
        }

        // ========================================================
        // 步骤 B：高含量组 (电压 10%, 电流 20%)
        // ========================================================
        qInfo() << "  -> 执行高含量配置 (V:10%, I:20%)...";

        qDebug("1. 下发 220V 5A 1.0PF 配置...");
        if (!sendSourceCmd(srcPort, m_cfgCmd1)) return false;
        QThread::msleep(300);

        qDebug("2. 启动标准源基波输出...");
        if (!sendSourceCmd(srcPort, m_startCmd, 6000)) return false;
        QThread::msleep(300);

        qDebug("3. 下发 %d 次谐波参数 (V:10%%, I:20%%)...", h);
        if (!sendSourceCmd(srcPort, buildHarmonicCmd(h, 10.0f, 20.0f))) return false;
        QThread::msleep(300);

        qDebug("4. 启动全体谐波输出 (40 秒延时)...");
        if (!sendSourceCmd(srcPort, startAllHarmonicCmd, 40000)) return false;
        if (!m_isRunning) return false;

        QThread::msleep(10000);

        // 读表
        aliveCount = 0;
        for (auto &meter : meters) {
            if (!meter.isAlive) continue;
            QVector<float> rawB;
            if (readMeterHarmonicData16(meterPort, meter.address, regAddr, 6, rawB)) {
                aliveCount++;
                // 4. 将高含量数据存入 volRowMap 的后 3 个格子
                volCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Ua", 10.0f, rawB[0], volRowMap[meter.uiIndex].cells[3], 5.0f, rowName);
                volCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Ub", 10.0f, rawB[1], volRowMap[meter.uiIndex].cells[4], 5.0f, rowName);
                volCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Uc", 10.0f, rawB[2], volRowMap[meter.uiIndex].cells[5], 5.0f, rowName);

                curCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Ia", 20.0f, rawB[3], curRowMap[meter.uiIndex].cells[3], 5.0f, rowName);
                curCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Ib", 20.0f, rawB[4], curRowMap[meter.uiIndex].cells[4], 5.0f, rowName);
                curCellsMap[meter.uiIndex] << calcErrAndMakeMap(meter.address, "Ic", 20.0f, rawB[5], curRowMap[meter.uiIndex].cells[5], 5.0f, rowName);
            }else{
                meter.isAlive = false;
                emit updateErrorMeterStatus(meter.uiIndex, Error_Timeout, "通讯失败");
            }
            QThread::msleep(100);
        }
        if (!m_isRunning) return false;
        if (aliveCount == 0) {
            qWarning() << "所有被测表计均已掉线，终止谐波测试！";
            return false;
        }

        // ========================================================
        // 步骤 C：超标判定、数据落盘与界面刷新
        // ========================================================
        for (auto &meter : meters) {
            if (!meter.isAlive) continue;
            if (volCellsMap[meter.uiIndex].size() == 6 && curCellsMap[meter.uiIndex].size() == 6) {

                // 5. 遍历检查是否有一项超差，有则给表计盖上 Fail 戳
                for (const auto& c : std::as_const(volRowMap[meter.uiIndex].cells)) if (c.isFail) meter.hasFail = true;
                for (const auto& c : std::as_const(curRowMap[meter.uiIndex].cells)) if (c.isFail) meter.hasFail = true;

                // 6. 把这两行数据，永久写入内存（Cat 6 是电压谐波，Cat 7 是电流谐波）
                meter.categories[6].rows.append(volRowMap[meter.uiIndex]);
                meter.categories[7].rows.append(curRowMap[meter.uiIndex]);

                // 7. 发送信号，刷新前端 QML 界面
                emit appendErrorRow(meter.uiIndex, 6, rowName, volCellsMap[meter.uiIndex]); // Cat 6:电压谐波
                emit appendErrorRow(meter.uiIndex, 7, rowName, curCellsMap[meter.uiIndex]); // Cat 7:电流谐波
            } else {
                emit updateErrorMeterStatus(meter.uiIndex, Error_Fail, "读取谐波失败");
                meter.hasFail = true;
            }
        }
    }

    qDebug("谐波测试结束");

    return true;
}

// =========================================================================
// 独立工具函数：计算误差，打印日志，并打包成 QML 能用的格式
// =========================================================================
QVariantMap CalibrationThread::calcErrAndMakeMap(uint8_t addr, const QString &phaseName, float std, float meas, Cell &outCell, float limit,const QString &conditionName)
{
    if(conditionName.contains("谐波")){
        outCell.err = (std > 0.001f) ? meas - std : 0.0f;
        outCell.isFail = (qAbs(outCell.err) > limit);
    }else{
        outCell.err = (std > 0.001f) ? ((meas - std) / std * 100.0f) : 0.0f;
        outCell.isFail = (qAbs(outCell.err) > limit);
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
bool CalibrationThread::processVoltageCurrentData(Meter &meter, const TestPoint &pt, const QVector<float> &viData, Row &volRow, QVariantList &volQmlCells, Row &curRow, QVariantList &curQmlCells, bool isLastTry)
{
    qInfo().noquote() << QString("\n=== 仪表地址[%1] 工况[%2] 数据明细 ===").arg(meter.address).arg(pt.name);

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

    // 相电压
    updateVolCell(0, "Ua", pt.tgtV, viData[0], pt.limit);
    updateVolCell(1, "Ub", pt.tgtV, viData[1], pt.limit);
    updateVolCell(2, "Uc", pt.tgtV, viData[2], pt.limit);
    // 线电压
    float tgtLineV = pt.tgtV * 1.73205f;
    updateVolCell(3, "Uab", tgtLineV, viData[6], pt.limit);
    updateVolCell(4, "Ubc", tgtLineV, viData[7], pt.limit);
    updateVolCell(5, "Uca", tgtLineV, viData[8], pt.limit);

    bool isVolPass = true;
    for (const auto& c : std::as_const(volRow.cells)) {
        if (c.isFail) { isVolPass = false; break; }
    }

    if (isVolPass || isLastTry) {
        if (!isVolPass) meter.hasFail = true;
        meter.categories[Cat_V].rows.append(volRow);
        emit appendErrorRow(meter.uiIndex, Cat_V, pt.name, volQmlCells);
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

    updateCurCell(0, "Ia", pt.tgtI, viData[3], pt.limit);
    updateCurCell(1, "Ib", pt.tgtI, viData[4], pt.limit);
    updateCurCell(2, "Ic", pt.tgtI, viData[5], pt.limit);

    bool isCurPass = true;
    for (const auto& c : std::as_const(curRow.cells)) {
        if (c.isFail) { isCurPass = false; break; }
    }

    if (isCurPass || isLastTry) {
        if (!isCurPass) meter.hasFail = true;
        meter.categories[Cat_I].rows.append(curRow);
        emit appendErrorRow(meter.uiIndex, Cat_I, pt.name, curQmlCells);
    }

    // 🌟 只有当电压和电流这两排格子全绿了，才算本次测试点完美通过！
    return isVolPass && isCurPass;
}

// =========================================================================
// 2. 专项处理：无功功率 (Q) - 支持单项锁死与延迟推UI
// =========================================================================
bool CalibrationThread::processReactivePowerData(Meter &meter, const TestPoint &pt, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry)
{
    // 1. 理论值计算：Q = U * I * sin(arccos(|PF|))
    float absPf = qAbs(pt.tgtPF);
    if (absPf > 1.0f) absPf = 1.0f;
    float stdQ = pt.tgtV * pt.tgtI * qSin(qAcos(absPf));

    // 容性(C, PF为负)时，无功功率通常定义为负值
    if (pt.tgtPF < 0) {
        stdQ = -stdQ;
    }
    float stdQTotal = stdQ * 3.0f;

    // 🌟 2. 只有第一次进入时才初始化缓存结构（默认全判 Fail，占好位）
    if (row.cells.isEmpty()) {
        row.conditionName = pt.name;
        row.cells.resize(4);
        for(int i = 0; i < 4; i++) {
            row.cells[i].isFail = true;
            qmlCells.append(QVariantMap());
        }
    }

    // 🌟 3. 闭包函数：针对单个格子进行“补考”更新
    auto updateCell = [&](int idx, const QString &phase, float std, float meas, float limit) {
        if (!row.cells[idx].isFail) return; // 成绩锁死保护

        Cell tempCell;
        QVariantMap tempMap = calcErrAndMakeMap(meter.address, phase, std, meas, tempCell, limit, pt.name);

        if (!tempCell.isFail || isLastTry) {
            row.cells[idx] = tempCell;
            qmlCells[idx] = tempMap;
        }
    };

    // 4. 对 4 个格子分别进行补考尝试
    updateCell(0, "Qa", stdQ, pData[0], pt.limit);
    updateCell(1, "Qb", stdQ, pData[1], pt.limit);
    updateCell(2, "Qc", stdQ, pData[2], pt.limit);
    updateCell(3, "Q总", stdQTotal, pData[3], pt.limit);

    // 🌟 5. 裁判环节
    bool isRowPass = true;
    for (const auto& c : std::as_const(row.cells)) {
        if (c.isFail) { isRowPass = false; break; }
    }

    // 🌟 6. 拦截器：全绿灯，或最后一次机会用完，才真正落盘并推给 UI
    if (isRowPass || isLastTry) {
        if (!isRowPass) meter.hasFail = true;
        meter.categories[Cat_ReactivePower].rows.append(row);
        emit appendErrorRow(meter.uiIndex, Cat_ReactivePower, pt.name, qmlCells);
    }

    return isRowPass;
}

// =========================================================================
// 3. 专项处理：视在功率 (S) - 支持单项锁死与延迟推UI
// =========================================================================
bool CalibrationThread::processApparentPowerData(Meter &meter, const TestPoint &pt, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry)
{
    // 1. 理论值计算：S = U * I
    float stdS = pt.tgtV * pt.tgtI;
    float stdSTotal = stdS * 3.0f;

    // 🌟 2. 只有第一次进入时才初始化缓存结构（默认全判 Fail，占好位）
    if (row.cells.isEmpty()) {
        row.conditionName = pt.name;
        row.cells.resize(4);
        for(int i = 0; i < 4; i++) {
            row.cells[i].isFail = true;
            qmlCells.append(QVariantMap());
        }
    }

    // 🌟 3. 闭包函数：针对单个格子进行“补考”更新
    auto updateCell = [&](int idx, const QString &phase, float std, float meas, float limit) {
        if (!row.cells[idx].isFail) return; // 成绩锁死保护

        Cell tempCell;
        QVariantMap tempMap = calcErrAndMakeMap(meter.address, phase, std, meas, tempCell, limit, pt.name);

        if (!tempCell.isFail || isLastTry) {
            row.cells[idx] = tempCell;
            qmlCells[idx] = tempMap;
        }
    };

    // 4. 对 4 个格子分别进行补考尝试
    updateCell(0, "Sa", stdS, pData[0], pt.limit);
    updateCell(1, "Sb", stdS, pData[1], pt.limit);
    updateCell(2, "Sc", stdS, pData[2], pt.limit);
    updateCell(3, "S总", stdSTotal, pData[3], pt.limit);

    // 🌟 5. 裁判环节
    bool isRowPass = true;
    for (const auto& c : std::as_const(row.cells)) {
        if (c.isFail) { isRowPass = false; break; }
    }

    // 🌟 6. 拦截器：全绿灯，或最后一次机会用完，才真正落盘并推给 UI
    if (isRowPass || isLastTry) {
        if (!isRowPass) meter.hasFail = true;
        meter.categories[Cat_ApparentPower].rows.append(row);
        emit appendErrorRow(meter.uiIndex, Cat_ApparentPower, pt.name, qmlCells);
    }

    return isRowPass;
}

// =========================================================================
// 4. 专项处理：功率因数 (PF) - 支持单项锁死与延迟推UI
// =========================================================================
bool CalibrationThread::processPowerFactorData(Meter &meter, const TestPoint &pt, const QVector<float> &rawData, Row &row, QVariantList &qmlCells, bool isLastTry)
{
    float stdPF = pt.tgtPF; // 理论值直接从 pt 中提取 (例如 1.0, 0.5, -0.8)

    // 🌟 2. 只有第一次进入时才初始化缓存结构（默认全判 Fail，占好位）
    if (row.cells.isEmpty()) {
        row.conditionName = pt.name;
        row.cells.resize(4);
        for(int i = 0; i < 4; i++) {
            row.cells[i].isFail = true;
            qmlCells.append(QVariantMap());
        }
    }

    // 🌟 3. 闭包函数：针对单个格子进行“补考”更新
    auto updateCell = [&](int idx, const QString &phase, float std, float meas, float limit) {
        if (!row.cells[idx].isFail) return; // 成绩锁死保护

        Cell tempCell;
        QVariantMap tempMap = calcErrAndMakeMap(meter.address, phase, std, meas, tempCell, limit, pt.name);

        if (!tempCell.isFail || isLastTry) {
            row.cells[idx] = tempCell;
            qmlCells[idx] = tempMap;
        }
    };

    // 4. 对 4 个格子分别进行补考尝试
    updateCell(0, "PFa", stdPF, rawData[0], pt.limit);
    updateCell(1, "PFb", stdPF, rawData[1], pt.limit);
    updateCell(2, "PFc", stdPF, rawData[2], pt.limit);
    updateCell(3, "PF总", stdPF, rawData[3], pt.limit);

    // 🌟 5. 裁判环节
    bool isRowPass = true;
    for (const auto& c : std::as_const(row.cells)) {
        if (c.isFail) { isRowPass = false; break; }
    }

    // 🌟 6. 拦截器：全绿灯，或最后一次机会用完，才真正落盘并推给 UI
    if (isRowPass || isLastTry) {
        if (!isRowPass) meter.hasFail = true;
        meter.categories[Cat_PowerFactor].rows.append(row);
        emit appendErrorRow(meter.uiIndex, Cat_PowerFactor, pt.name, qmlCells);
    }

    return isRowPass;
}

// =========================================================================
// 专项处理：有功功率 (P) - 具备单项成绩锁死与延迟落盘功能
// =========================================================================
bool CalibrationThread::processActivePowerData(Meter &meter, const TestPoint &pt, const QVector<float> &pData, Row &row, QVariantList &qmlCells, bool isLastTry)
{
    // 1. 理论值计算
    float stdP = pt.tgtV * pt.tgtI * qAbs(pt.tgtPF);
    float stdPTotal = stdP * 3.0f;

    // 🌟 2. 只有第一次进入时才初始化缓存结构（默认全判 Fail，占好位）
    if (row.cells.isEmpty()) {
        row.conditionName = pt.name;
        row.cells.resize(4);
        for(int i = 0; i < 4; i++) {
            row.cells[i].isFail = true;
            qmlCells.append(QVariantMap());
        }
    }

    // 🌟 3. 闭包函数：针对单个格子进行“补考”更新
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

    // 4. 对 4 个格子分别进行补考尝试
    updateCell(0, "Pa", stdP, pData[0], pt.limit);
    updateCell(1, "Pb", stdP, pData[1], pt.limit);
    updateCell(2, "Pc", stdP, pData[2], pt.limit);
    updateCell(3, "P总", stdPTotal, pData[3], pt.limit);

    // 🌟 5. 裁判环节：检查这一行这 4 个格子是否全部凑齐绿灯了？
    bool isRowPass = true;
    for (const auto& c : std::as_const(row.cells)) {
        if (c.isFail) { isRowPass = false; break; }
    }

    // 🌟 6. 拦截器：全绿灯，或最后一次机会用完，才真正落盘并推给 UI
    if (isRowPass || isLastTry) {
        if (!isRowPass) meter.hasFail = true; // 3次都没过，给表计判死刑
        meter.categories[Cat_ActivePower].rows.append(row);
        emit appendErrorRow(meter.uiIndex, Cat_ActivePower, pt.name, qmlCells);
    }

    // 7. 返回这行是否已及格，好让主循环知道这块表不用再重测了
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
            emit updateErrorMeterStatus(meter.uiIndex, Error_Timeout, "模式设置超时");
        }
    }

    if (aliveCount == 0) {
        emit showTopMessage("全部仪表模式设置失败，测试终止", "error");
        return false;
    }

    QElapsedTimer timer;
    float oldPF = 0,newPF = 0;
    for (int step = 0; step < testPoints.size(); ++step) {
        TestPoint pt = testPoints[step];
        oldPF = newPF;
        newPF = pt.tgtPF;

        timer.start();
        if (!sendSourceCmd(srcPort, pt.srcCmd)) return false;
        QThread::msleep(300);
        if (!sendSourceCmd(srcPort, m_startCmd, 6000)) return false;
        qint64 elapsed = timer.elapsed();
        qInfo().noquote() << QString("[稳定分析] 工况[%1] 标准源准备就绪耗时: %2 ms").arg(pt.name).arg(elapsed);

        // 稳定等待
        qDebug("正在等待标准源和仪表内部采样稳定...");
        for (int i = 0; i < 20; ++i) {
            if (!m_isRunning) return false;
            QThread::msleep(100);
        }
        if(oldPF != newPF){
            qDebug("为不同的PF增加3s延时");
            for (int i = 0; i < 30; ++i) {
                if (!m_isRunning) return false;
                QThread::msleep(100);
            }
        }

        // ========================================================
        // 🌟 核心：跨 3 次循环的“缓存背包”
        // ========================================================
        QMap<int, Row> rowCacheMap;
        QMap<int, QVariantList> qmlCacheMap;

        // 🌟 专门给电压/电流准备的第二套电流缓存（因为一次读取会同时产生电压和电流两行数据）
        QMap<int, Row> curRowCacheMap;
        QMap<int, QVariantList> curQmlCacheMap;

        QMap<int, bool> meterPassedMap; // 记录某块表是否已经拼图成功

        // 🌟 开始最多 3 次的重试循环
        for (int tryIdx = 1; tryIdx <= 3; ++tryIdx) {
            bool isLastTry = (tryIdx == 3);

            if (tryIdx > 1) {
                qDebug("第 %d 次补充读取误差...", tryIdx);
                QThread::msleep(1500); // 重读前给仪表一点时间刷新采样
            }

            for (auto &meter : meters) {
                if (!meter.isEnabled || !meter.isAlive) continue;

                // 🌟 如果这块表之前已经考及格了，直接跳过免考！
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

                // 🌟 更新 UI：带上当前是第几次读取
                emit updateErrorMeterStatus(meter.uiIndex, Error_Running, statusMsg + QString(": %1").arg(pt.name));

                QVector<float> rawData;

                // 读取仪表
                if (readMeterData(meterPort, meter.address, startAddr, regCount, rawData, currentDivider, isSigned)) {
                    bool isPass = false;

                    // ========================================================
                    // 🌟 终极全量分发：让所有电参量全部享受 3 次重试 + 单格成绩锁死！
                    // ========================================================
                    if (catType == Cat_ActivePower) {
                        isPass = processActivePowerData(meter, pt, rawData, rowCacheMap[meter.uiIndex], qmlCacheMap[meter.uiIndex], isLastTry);
                    }
                    else if (catType == Cat_ReactivePower) {
                        isPass = processReactivePowerData(meter, pt, rawData, rowCacheMap[meter.uiIndex], qmlCacheMap[meter.uiIndex], isLastTry);
                    }
                    else if (catType == Cat_ApparentPower) {
                        isPass = processApparentPowerData(meter, pt, rawData, rowCacheMap[meter.uiIndex], qmlCacheMap[meter.uiIndex], isLastTry);
                    }
                    else if (catType == Cat_PowerFactor) {
                        isPass = processPowerFactorData(meter, pt, rawData, rowCacheMap[meter.uiIndex], qmlCacheMap[meter.uiIndex], isLastTry);
                    }
                    else if (catType == Cat_V || catType == Cat_I) {
                        // 🌟 电压和电流同时传入两套缓存，确保两排格子都能独立锁死好成绩！
                        isPass = processVoltageCurrentData(meter, pt, rawData,
                                                           rowCacheMap[meter.uiIndex], qmlCacheMap[meter.uiIndex],
                                                           curRowCacheMap[meter.uiIndex], curQmlCacheMap[meter.uiIndex],
                                                           isLastTry);
                    }

                    if (isPass) {
                        meterPassedMap[meter.uiIndex] = true; // 这块表凑齐满分了，标记通关
                    }

                } else {
                    // 🌟 读失败处理：只有最后一次重试还读不出来，才真正判掉线
                    if (isLastTry) {
                        meter.isAlive = false;
                        emit updateErrorMeterStatus(meter.uiIndex, Error_Timeout, "通讯失败");
                    }
                }
            } // end for(meters)

            // ========================================================
            // 🌟 事后考勤盘点：查一查是不是活着的表都已经满分了？
            // ========================================================
            bool allPassed = true;
            for (auto &meter : meters) {
                // 只要有一块表还活着、被勾选了，且还没考及格，就说明全班还没满分
                if (meter.isEnabled && meter.isAlive && !meterPassedMap[meter.uiIndex]) {
                    allPassed = false;
                    break;
                }
            }
            // 🌟 提前结束条件：如果本点所有活着的表均已及格，无需继续后面的重读
            if (allPassed) {
                qDebug("本测试点所有仪表均已及格，提前进入下一测试工况！");
                break;
            }
            if (!m_isRunning) return false;
        } // end for(tryIdx)

        // ========================================================
        // 盘点：经历完 3 轮折磨后，还有几块表活着？
        // ========================================================
        aliveCount = 0;
        for (auto &meter : meters) {
            if (meter.isEnabled && meter.isAlive) aliveCount++;
        }

        if (aliveCount == 0) {
            emit showTopMessage("仪表全部失败掉线，测试终止", "error");
            return false;
        }
    } // end for(testPoints)

    return true;
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

    // 🌟 1. 打印 Tx (带上起始地址方便排查是读什么参数)
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

    // 🌟 2. 打印 Rx (接收到的原始字节)
    qInfo().noquote() << QString("[Rx 仪表%1]")
                             .arg(addr, 2, 10, QChar('0'))
                      << rx.toHex(' ').toUpper();

    int byteCount = regCount * 2; // 纯数据域的字节数

    // 🌟 3. 校验报文是否完整
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
        // 🌟 5. 精准报错：告诉您为什么解析失败
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

    // 🌟 开启最多 3 次的打地鼠容错重试
    for (int tryIdx = 1; tryIdx <= 3; ++tryIdx) {

        if (tryIdx > 1) {
            qWarning().noquote() << QString("[标准源重试] 第 %1 次重新尝试发送指令...").arg(tryIdx);
            QThread::msleep(1000); // 🌟 失败后稍作停顿再发，给标准源内部 CPU 一点缓冲释放串口缓冲区的时间
        }

        // 1. 发送指令
        qInfo().noquote() << QString("[Tx 标准源 (第%1次)] ").arg(tryIdx) << cmdHex.toHex(' ').toUpper();
        port.write(cmdHex);

        // 2. 校验等待超时 (没响应则继续下一次循环)
        if (!checkSourceResponse(port, timeoutMs)) {
            lastErrorMsg = "标准源响应超时(无返回)";
            qWarning() << "[Rx 标准源] 第" << tryIdx << "次读取：超时未收到任何响应";
            continue;
        }

        QByteArray rx = port.readAll();
        qInfo().noquote() << QString("[Rx 标准源 (第%1次)] ").arg(tryIdx) << rx.toHex(' ').toUpper();
        QThread::msleep(100);

        // 3. 黄金校验点：只要匹配上正确的 ACK，立刻直接通关！
        if (rx == m_srcAck) {
            if (tryIdx > 1) {
                qInfo() << ">>> [标准源重试成功] 在第" << tryIdx << "次尝试时通信恢复正常！";
            }
            return true; // 完美的 8 字节全帧匹配，直接跳出循环并放行
        }

        // 4. 记录本次错误的分类，并进入下一次循环
        if (rx == m_srcNack) {
            lastErrorMsg = "标准源配置被拒(否定应答 NACK)";
            qWarning() << "[Rx 标准源] 第" << tryIdx << "次读取：标准源返回 NACK 否定应答！";
        } else {
            lastErrorMsg = "标准源应答特征不匹配";
            qWarning() << "[Rx 标准源] 第" << tryIdx << "次读取：应答特征码错误，内容:" << rx.toHex(' ').toUpper();
        }
    }

    // =========================================================================
    // 🌟 3 次全部失败 —— 终审判死刑，通知 UI 并停止整台设备的运行
    // =========================================================================
    qCritical().noquote() << "[标准源致命错误] 连续 3 次发送均失败！最终死因:" << lastErrorMsg;
    emit srcMessage(lastErrorMsg, "error");
    emit showResultPopup("标准源错误",lastErrorMsg,"error");
    m_isRunning = false; // 强行拉下手刹，停止上位机后台测试线程
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
    emit showResultPopup(QString("仪表 %1 错误").arg(addr),QString("等待状态机寄存器变更为 %1 失败").arg(targetState),"error");
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
        qWarning()<<"标准源指令发送或接收超时!";
        emit srcMessage("标准源中断: " + errStr, "error");
        emit showResultPopup("标准源中断: " , errStr, "error");
        //emit calirResult("标准源中断: " + errStr, "error");
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

