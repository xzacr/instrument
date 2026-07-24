import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Item {
    id: errorCalcPage

    property color themeColor: "#1976D2"
    property color textMain: "#333333"
    property color textSub: "#606266"
    readonly property int mode_ErrorCalc: 1 // 对应C++的 Mode_ErrorCalc

    property int selectedMeterIndex: 0
    property int selectedCategory: 0

    // 接收底层推送的真实数据源：5台仪表，每台8个分类的动态行
    property var meterDataStore: []
    property var currentTableModel: [] // 绑定给 ListView 的当前视图数据

    property var categories: [
        "电压", "电流", "有功功率", "无功功率",
        "视在功率", "功率因数", "谐波电压", "谐波电流"
    ]

    property int failTrigger: 0

    function hasCategoryFail(catIdx) {
        let dummy = failTrigger; // 绑定信号灯，确保数据一来立马重新计算！
        if (!meterDataStore || meterDataStore.length === 0) return false;
        let rows = meterDataStore[selectedMeterIndex][catIdx];
        // 只要该分类下任意一行的任意一格 isFail 为 true，就返回 true
        return rows ? rows.some(r => r.cells.some(c => c.isFail)) : false;
    }
    // =====================================================================
    // 🌟 1. 表头列名映射 (需要和 C++ rowCells 下发的数量严格匹配)
    // =====================================================================
    function getColumns(cat) {
        if (cat === 0) return ["Ua", "Ub", "Uc", "Uab", "Ubc", "Uac"]
        if (cat === 1) return ["Ia", "Ib", "Ic"]
        if (cat === 2) return ["Pa", "Pb", "Pc", "P总"]
        if (cat === 3) return ["Qa", "Qb", "Qc", "Q总"]
        if (cat === 4) return ["Sa", "Sb", "Sc", "S总"]
        if (cat === 5) return ["PFa", "PFb", "PFc", "PF总"]
        if (cat === 6) return ["Ua(含3%)", "Ub(含3%)", "Uc(含3%)", "Ua(含10%)", "Ub(含10%)", "Uc(含10%)"]
        if (cat === 7) return ["Ia(含10%)", "Ib(含10%)", "Ic(含10%)", "Ia(含20%)", "Ib(含20%)", "Ic(含20%)"]
        return []
    }

    function getRows(cat) {
        // 🌟 1. 动态获取系统设置里的标准源误差补偿值 (默认按 0.0 算，防呆保护)
        let offset = (typeof syssettings !== "undefined" && syssettings.sourceErrorOffset !== undefined)
                     ? syssettings.sourceErrorOffset : 0.0;

        // 🌟 2. 核心辅助计算器：基础限值 + 动态补偿，完美去除末尾多余0
        // 效果：(1.0, 0.1) -> ", 1.1%"; (1.25, 0.1) -> ", 1.35%"
        let calc = function(base) {
            let val = base + offset;
            return ", " + val.toFixed(2).replace(/0$/, "") + "%";
        };

        // --- 0: 电压 / 1: 电流 (基础限值 0.5%) ---
        if (cat === 0 || cat === 1) {
            let l05 = calc(0.5);
            return [
                "44V,  0.5A" + l05,
                "220V, 5.0A" + l05,
                "264V, 6.0A" + l05
            ];
        }

        // --- 2: 有功功率 (51个点) ---
        if (cat === 2) {
            let l05 = calc(0.5);
            let l06 = calc(0.6);
            let l10 = calc(1.0);
            return [
                // 第一组：176V
                "176V, PF=1.0, 0.05A" + l10, "176V, PF=1.0, 0.20A" + l10, "176V, PF=1.0, 0.25A" + l05, "176V, PF=1.0, 5.00A" + l05, "176V, PF=1.0, 6.00A" + l05,
                "176V, PF=0.5L, 0.10A" + l10, "176V, PF=0.5L, 0.25A" + l10, "176V, PF=0.5L, 0.40A" + l10, "176V, PF=0.5L, 0.50A" + l06, "176V, PF=0.5L, 5.00A" + l06, "176V, PF=0.5L, 6.00A" + l06,
                "176V, PF=0.8C, 0.10A" + l10, "176V, PF=0.8C, 0.25A" + l10, "176V, PF=0.8C, 0.40A" + l10, "176V, PF=0.8C, 0.50A" + l06, "176V, PF=0.8C, 5.00A" + l06, "176V, PF=0.8C, 6.00A" + l06,
                // 第二组：220V
                "220V, PF=1.0, 0.05A" + l10, "220V, PF=1.0, 0.20A" + l10, "220V, PF=1.0, 0.25A" + l05, "220V, PF=1.0, 5.00A" + l05, "220V, PF=1.0, 6.00A" + l05,
                "220V, PF=0.5L, 0.10A" + l10, "220V, PF=0.5L, 0.25A" + l10, "220V, PF=0.5L, 0.40A" + l10, "220V, PF=0.5L, 0.50A" + l06, "220V, PF=0.5L, 5.00A" + l06, "220V, PF=0.5L, 6.00A" + l06,
                "220V, PF=0.8C, 0.10A" + l10, "220V, PF=0.8C, 0.25A" + l10, "220V, PF=0.8C, 0.40A" + l10, "220V, PF=0.8C, 0.50A" + l06, "220V, PF=0.8C, 5.00A" + l06, "220V, PF=0.8C, 6.00A" + l06,
                // 第三组：264V
                "264V, PF=1.0, 0.05A" + l10, "264V, PF=1.0, 0.20A" + l10, "264V, PF=1.0, 0.25A" + l05, "264V, PF=1.0, 5.00A" + l05, "264V, PF=1.0, 6.00A" + l05,
                "264V, PF=0.5L, 0.10A" + l10, "264V, PF=0.5L, 0.25A" + l10, "264V, PF=0.5L, 0.40A" + l10, "264V, PF=0.5L, 0.50A" + l06, "264V, PF=0.5L, 5.00A" + l06, "264V, PF=0.5L, 6.00A" + l06,
                "264V, PF=0.8C, 0.10A" + l10, "264V, PF=0.8C, 0.25A" + l10, "264V, PF=0.8C, 0.40A" + l10, "264V, PF=0.8C, 0.50A" + l06, "264V, PF=0.8C, 5.00A" + l06, "264V, PF=0.8C, 6.00A" + l06
            ];
        }

        // --- 3: 无功功率 (39个点) ---
        if (cat === 3) {
            let l10 = calc(1.0);
            let l125 = calc(1.25);
            return [
                // 第一组：176V
                "176V, PF=0, 0.1A" + l125, "176V, PF=0, 0.2A" + l125, "176V, PF=0, 0.25A" + l10, "176V, PF=0, 5.0A" + l10, "176V, PF=0, 6.0A" + l10,
                "176V, PF=0.866, 0.25A" + l125, "176V, PF=0.866, 0.4A" + l125, "176V, PF=0.866, 0.5A" + l10, "176V, PF=0.866, 5.0A" + l10, "176V, PF=0.866, 6.0A" + l10,
                "176V, PF=0.968, 0.5A" + l125, "176V, PF=0.968, 5.0A" + l125, "176V, PF=0.968, 6.0A" + l125,
                // 第二组：220V
                "220V, PF=0, 0.1A" + l125, "220V, PF=0, 0.2A" + l125, "220V, PF=0, 0.25A" + l10, "220V, PF=0, 5.0A" + l10, "220V, PF=0, 6.0A" + l10,
                "220V, PF=0.866, 0.25A" + l125, "220V, PF=0.866, 0.4A" + l125, "220V, PF=0.866, 0.5A" + l10, "220V, PF=0.866, 5.0A" + l10, "220V, PF=0.866, 6.0A" + l10,
                "220V, PF=0.968, 0.5A" + l125, "220V, PF=0.968, 5.0A" + l125, "220V, PF=0.968, 6.0A" + l125,
                // 第三组：264V
                "264V, PF=0, 0.1A" + l125, "264V, PF=0, 0.2A" + l125, "264V, PF=0, 0.25A" + l10, "264V, PF=0, 5.0A" + l10, "264V, PF=0, 6.0A" + l10,
                "264V, PF=0.866, 0.25A" + l125, "264V, PF=0.866, 0.4A" + l125, "264V, PF=0.866, 0.5A" + l10, "264V, PF=0.866, 5.0A" + l10, "264V, PF=0.866, 6.0A" + l10,
                "264V, PF=0.968, 0.5A" + l125, "264V, PF=0.968, 5.0A" + l125, "264V, PF=0.968, 6.0A" + l125
            ];
        }

        // --- 4: 视在功率 (15个点) ---
        if (cat === 4) {
            let l05 = calc(0.5);
            let l10 = calc(1.0);
            return [
                "176V, PF=1, 0.1A" + l10, "176V, PF=1, 0.2A" + l10, "176V, PF=1, 0.3A" + l05, "176V, PF=1, 5.0A" + l05, "176V, PF=1, 6.0A" + l05,
                "220V, PF=1, 0.1A" + l10, "220V, PF=1, 0.2A" + l10, "220V, PF=1, 0.3A" + l05, "220V, PF=1, 5.0A" + l05, "220V, PF=1, 6.0A" + l05,
                "264V, PF=1, 0.1A" + l10, "264V, PF=1, 0.2A" + l10, "264V, PF=1, 0.3A" + l05, "264V, PF=1, 5.0A" + l05, "264V, PF=1, 6.0A" + l05
            ];
        }

        // --- 5: 功率因数 (36个点，基础限值全为 0.5%) ---
        if (cat === 5) {
            let l05 = calc(0.5);
            return [
                // 第一组：110V
                "110V, PF=0.5L, 0.5A" + l05, "110V, PF=0.5L, 2.5A" + l05, "110V, PF=0.5L, 5.0A" + l05, "110V, PF=0.5L, 6.0A" + l05,
                "110V, PF=1.0, 0.5A" + l05, "110V, PF=1.0, 2.5A" + l05, "110V, PF=1.0, 5.0A" + l05, "110V, PF=1.0, 6.0A" + l05,
                "110V, PF=0.8C, 0.5A" + l05, "110V, PF=0.8C, 2.5A" + l05, "110V, PF=0.8C, 5.0A" + l05, "110V, PF=0.8C, 6.0A" + l05,
                // 第二组：220V
                "220V, PF=0.5L, 0.5A" + l05, "220V, PF=0.5L, 2.5A" + l05, "220V, PF=0.5L, 5.0A" + l05, "220V, PF=0.5L, 6.0A" + l05,
                "220V, PF=1.0, 0.5A" + l05, "220V, PF=1.0, 2.5A" + l05, "220V, PF=1.0, 5.0A" + l05, "220V, PF=1.0, 6.0A" + l05,
                "220V, PF=0.8C, 0.5A" + l05, "220V, PF=0.8C, 2.5A" + l05, "220V, PF=0.8C, 5.0A" + l05, "220V, PF=0.8C, 6.0A" + l05,
                // 第三组：264V
                "264V, PF=0.5L, 0.5A" + l05, "264V, PF=0.5L, 2.5A" + l05, "264V, PF=0.5L, 5.0A" + l05, "264V, PF=0.5L, 6.0A" + l05,
                "264V, PF=1.0, 0.5A" + l05, "264V, PF=1.0, 2.5A" + l05, "264V, PF=1.0, 5.0A" + l05, "264V, PF=1.0, 6.0A" + l05,
                "264V, PF=0.8C, 0.5A" + l05, "264V, PF=0.8C, 2.5A" + l05, "264V, PF=0.8C, 5.0A" + l05, "264V, PF=0.8C, 6.0A" + l05
            ];
        }

        // --- 6: 谐波电压 / 7: 谐波电流 ---
        if (cat === 6 || cat === 7) {
            return [
                "220V/5A, 2次谐波", "220V/5A, 3次谐波", "220V/5A, 5次谐波",
                "220V/5A, 7次谐波", "220V/5A, 11次谐波", "220V/5A, 13次谐波"
            ];
        }
        return [];
    }

    // =====================================================================
    // 🌟 3. 全量预加载大纲（默认赋 '-'）
    // =====================================================================
    function clearAllData() {
        let fresh = [];
        for (let i = 0; i < 5; i++) {
            let cats = [];
            for (let c = 0; c < 8; c++) {
                let catRows = [];
                let rowNames = getRows(c);
                let colCount = getColumns(c).length;

                for (let r = 0; r < rowNames.length; r++) {
                    let emptyCells = [];
                    for (let col = 0; col < colCount; col++) {
                        emptyCells.push({ "errStr": "-", "isFail": false });
                    }
                    catRows.push({
                        "header": rowNames[r],
                        "cells": emptyCells
                    });
                }
                cats.push(catRows);
            }
            fresh.push(cats);

            // 重置顶部卡片状态
            meterStateModel.setProperty(i, "status", "IDLE");
            meterStateModel.setProperty(i, "desc", "等待测试");
        }
        meterDataStore = fresh;
        updateView();
    }

    // 触发 ListView 刷新
    function updateView() {
        if (meterDataStore.length > 0) {
            // 浅拷贝触发 QML 视图数据变更信号
            currentTableModel = [].concat(meterDataStore[selectedMeterIndex][selectedCategory]);
        }
    }

    function refreshHeaders() {
        if (!meterDataStore || meterDataStore.length === 0) {
            clearAllData();
            return;
        }

        let data = meterDataStore;
        // 遍历所有 5 台仪表、8 个分类
        for (let i = 0; i < data.length; i++) {
            for (let c = 0; c < data[i].length; c++) {
                // 重新调用 getRows(c)，此时它会算出最新的百分比文本
                let newRowNames = getRows(c);
                for (let r = 0; r < data[i][c].length; r++) {
                    if (r < newRowNames.length) {
                        // 🌟 核心：只替换表头 header！绝不触碰 cells 里的实测数据！
                        data[i][c][r].header = newRowNames[r];
                    }
                }
            }
        }
        meterDataStore = data;
        updateView(); // 强行触发 QML 视图重绘，表格里的文字瞬间更新！
    }

    onSelectedMeterIndexChanged: updateView()
    onSelectedCategoryChanged: updateView()
    Component.onCompleted: clearAllData()

    // 🌟 全局信号监听中枢
    Connections {
        target: ins

        function onIsCalibratingChanged() {
            if (ins.isCalibrating) {
                clearAllData();
            }
        }

        function onUpdateErrorMeterStatus(page,meterIndex, statusEnum, desc) {
            if(page !== mode_ErrorCalc) return;
            let statusStr = "IDLE";
            if (statusEnum === 0) statusStr = "IDLE";
            else if (statusEnum === 1) statusStr = "PASS";
            else if (statusEnum === 2) statusStr = "FAIL";
            else if (statusEnum === 3) statusStr = "RUNNING";
            else if (statusEnum === 4) statusStr = "TIMEOUT";
            else if (statusEnum === 4) statusStr = "STOP";

            meterStateModel.setProperty(meterIndex, "status", statusStr);
            meterStateModel.setProperty(meterIndex, "desc", desc);
        }

        // =====================================================================
        // 🌟 4. 极致精简：原地精确替换 + 智能位置滚动
        // =====================================================================
        function onAppendErrorRow(page,meterIndex, categoryIndex, rowName, rowCells) {
            //console.log("onAppendErrorRowwwwwwwwwwwwwwwwwwwwwwwwwwww")
            if(page !== mode_ErrorCalc) return;
            let data = meterDataStore;
            let catRows = data[meterIndex][categoryIndex];
            let foundIndex = -1;

            // --- 核心：严格通过名字精准替换，哪怕顺序变了也不会写错 ---
            for (let r = 0; r < catRows.length; r++) {
                //if (catRows[r].header === rowName) {
                if (catRows[r].header.startsWith(rowName)) {
                    catRows[r].cells = rowCells;
                    foundIndex = r;
                    break;
                }
            }

            // 防呆保护：如果下发的工况名在大纲里找不到，直接末尾追加
            if (foundIndex === -1) {
                catRows.push({ "header": rowName, "cells": rowCells });
                foundIndex = catRows.length - 1;
            }

            meterDataStore = data;
            failTrigger++;

            // 1. 判断操作员现在是否正停留在表格“最底部” (留出 10 像素的容差)
            let isAtBottom = (resultListView.contentY + resultListView.height >= resultListView.contentHeight - 10);

            // 2. 记住当前位置
            let savedContentY = resultListView.contentY;

            if (meterIndex === selectedMeterIndex) {
                // 1. 智能对偶归并：防止成对数据导致标签前后疯狂闪跳
                let targetCategory = categoryIndex;
                if (categoryIndex === 1) targetCategory = 0; // 收到电流(1) -> 稳在“电压(0)”标签
                if (categoryIndex === 7) targetCategory = 6; // 收到谐波电流(7) -> 稳在“谐波电压(6)”标签

                // 2. 如果正在测试的分类和当前看的不一样，自动把标签切过去！
                if (selectedCategory !== targetCategory) {
                    //selectedCategory = targetCategory; // 这一步会自动触发 QML 视图重绘
                } else {
                    updateView(); // 标签没变，正常手动刷新当前表格的值
                }

                // // 3. 延迟一帧让视图将新选中的标签表格渲染完毕，再精准对准到中间！
                // Qt.callLater(function() {
                //     resultListView.positionViewAtIndex(foundIndex, ListView.Center);
                // });
            }
            // 4. 智能分支判断
            Qt.callLater(function() {
                if (!isAtBottom) {
                    // 🛡️【历史查阅模式】：如果用户刚才在翻看顶部或中间的旧数据，
                    // 强行锁死视窗，死记当前位置，绝对不跳动！
                    resultListView.contentY = savedContentY;
                } else {
                    // 🚀【实时监控模式】：如果用户刚才本来就在盯着最底部，
                    // 那新出一条数据，就顺滑地自动帮他滚到最后一行！
                    resultListView.positionViewAtEnd();
                    // 如果是 TableView，可以用：
                }
            });
        }
    }

    // 动态卡片模型
    ListModel {
        id: meterStateModel
        ListElement { title: "1#仪表"; status: "IDLE"; desc: "等待测试" }
        ListElement { title: "2#仪表"; status: "IDLE"; desc: "等待测试" }
        ListElement { title: "3#仪表"; status: "IDLE"; desc: "等待测试" }
        ListElement { title: "4#仪表"; status: "IDLE"; desc: "等待测试" }
        ListElement { title: "5#仪表"; status: "IDLE"; desc: "等待测试" }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 15

        // ==========================================
        // 顶部：带有悬浮放大的仪表看板
        // ==========================================
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 180
            Layout.maximumHeight: 180
            spacing: 20

            Repeater {
                model: meterStateModel
                delegate: Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    property color bgColor: status === "PASS" ? "#4CAF50" :
                                            status === "FAIL" ? "#F44336" :
                                            status === "TIMEOUT" ? "#F57C00" :
                                            status === "STOP" ? "#F57C00" :
                                            status === "RUNNING" ? "#2196F3" : "#9E9E9E"

                    property bool isSelected: selectedMeterIndex === index

                    MouseArea {
                        id: cardMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: selectedMeterIndex = index
                    }

                    property real currentMargin: isSelected ? 4 : (cardMouseArea.containsMouse ? 8 : 12)
                    Behavior on currentMargin { NumberAnimation { duration: 250; easing.type: Easing.OutBack } }

                    Item {
                        id: shapeSource
                        anchors.fill: parent
                        anchors.margins: currentMargin
                        visible: false

                        Rectangle {
                            id: mainShadowShape
                            anchors.fill: parent
                            anchors.bottomMargin: 14
                            radius: 8
                            color: bgColor
                        }

                        Rectangle {
                            visible: isSelected
                            width: 20; height: 20
                            rotation: 45
                            color: bgColor
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: mainShadowShape.bottom
                        }
                    }

                    MultiEffect {
                        source: shapeSource
                        anchors.fill: shapeSource
                        shadowEnabled: true
                        shadowColor: bgColor
                        shadowBlur: 1.0
                        shadowOpacity: 0.6
                        shadowVerticalOffset:  8
                        Behavior on shadowOpacity { NumberAnimation { duration: 250 } }
                        Behavior on shadowVerticalOffset { NumberAnimation { duration: 250 } }
                        Behavior on shadowColor { ColorAnimation { duration: 250 } }
                    }

                    Item {
                        anchors.fill: parent
                        anchors.margins: currentMargin

                        Rectangle {
                            visible: isSelected
                            width: 20; height: 20
                            rotation: 45
                            color: bgColor
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: mainVisCard.bottom
                        }

                        Rectangle {
                            id: mainVisCard
                            anchors.fill: parent
                            anchors.bottomMargin: 14
                            radius: 8
                            color: bgColor
                            border.color: isSelected ? Qt.lighter(bgColor, 1.2) : "transparent"
                            border.width: isSelected ? 1 : 0
                            clip: true

                            Rectangle {
                                width: parent.width
                                height: 35
                                color: Qt.darker(bgColor, 1.15)
                                radius: 8
                                Rectangle {
                                    width: parent.width; height: 8;
                                    anchors.bottom: parent.bottom; color: parent.color
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    Label {
                                        text: title
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 16
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        id: stateIcon
                                        text: status === "FAIL" ? "❗" :
                                              status === "TIMEOUT" ? "⚠" :
                                              status === "STOP" ? "⚠" :
                                              status === "PASS" ? "✔" :
                                              status === "RUNNING" ? "⚙" : "○"
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 16
                                        RotationAnimation {
                                            target: stateIcon
                                            loops: Animation.Infinite
                                            from: 0; to: 360
                                            duration: 1500
                                            running: status === "RUNNING"
                                            onRunningChanged: {
                                                if (!running) {
                                                    stateIcon.rotation = 0
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 0
                                Label {
                                    text: status
                                    color: "white"
                                    font.pixelSize: 42
                                    font.bold: true
                                    font.letterSpacing: 2
                                    Layout.alignment: Qt.AlignCenter
                                }
                                Label {
                                    text: status === "PASS" ? "合格" :
                                          status === "FAIL" ? "不合格" :
                                          status === "TIMEOUT" ? "已掉线" :
                                          status === "STOP" ? "已停止" :
                                          status === "IDLE" ? "未启用" : "正在测试"
                                    color: "white"
                                    font.pixelSize: 18
                                    font.bold: true
                                    Layout.alignment: Qt.AlignCenter
                                }
                            }

                            Label {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottomMargin: 10
                                text: desc
                                color: "white"
                                font.pixelSize: 14
                                horizontalAlignment: Text.AlignHCenter
                                opacity: 0.9
                            }
                        }
                    }
                }
            }
        }

        // ==========================================
        // 中层：大尺寸导航按钮
        // ==========================================
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Repeater {
                model: categories
                delegate: Button {
                    property bool isCatFail: hasCategoryFail(index)

                    text: modelData
                    // 🌟 1. 字体加大！选中时 20px 粗体，未选中 18px 粗体，绝对清晰大方！
                    font.pixelSize: selectedCategory === index ? 20 : 18
                    font.bold: true

                    // 🌟 2. 按钮稍微加高一点到 54px，跟大的字号更搭
                    Layout.preferredHeight: 54
                    Layout.preferredWidth: implicitWidth + 40 // 左右多留点空白，更大气

                    background: Rectangle {
                        radius: 6

                        // 🌟 3. 核心修复：未选中时改用“纯白 #FFFFFF”！
                        // 这样在灰色的应用底色下，每一个未选中的按钮都是一个立体的白色方块！
                        color: {
                            if (selectedCategory === index) return isCatFail ? "#D32F2F" : themeColor;
                            return isCatFail ? "#FFEBEE" : "#FFFFFF"; // <--- 看这里，从 #F0F2F5 改成了 #FFFFFF
                        }

                        // 🌟 4. 加上精致边框：选中时无边框；没选中时用 #DCDFE6 勾边
                        border.color: {
                            if (selectedCategory === index) return "transparent";
                            return isCatFail ? "#F44336" : "#DCDFE6";
                        }
                        border.width: selectedCategory === index ? 0 : 1

                        // 鼠标悬浮时的反馈动画
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }

                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        // 选中时白字；没选中且报错是红字；没选中正常是深黑字 (#303133)
                        color: {
                            if (selectedCategory === index) return "#FFFFFF";
                            return isCatFail ? "#D32F2F" : "#303133";
                        }
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter

                        Behavior on color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: selectedCategory = index
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                id: errorTestBtn
                Layout.preferredHeight: 50
                Layout.preferredWidth: 160
                property bool isRunning: ins ? ins.isCalibrating : false

                background: Rectangle {
                    color: parent.pressed ? Qt.darker((errorTestBtn.isRunning ? "#F44336" : themeColor), 1.1) : (errorTestBtn.isRunning ? "#F44336" : themeColor)
                    radius: 6
                }
                contentItem: Text {
                    text: errorTestBtn.isRunning ? "停止测试" : "开始测试"
                    font.bold: true
                    font.pixelSize: 18
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    if (isRunning) {
                        if (typeof ins !== "undefined") {
                            loadingPopup.show("正在停止...");
                            ins.stopCalibration();
                        }
                    } else {
                        let config = calibPage.validateAndGetConfig();
                        if (config) {
                            ins.startTask(
                                mode_ErrorCalc,
                                config.srcPort,
                                config.srcBaud,
                                config.meterPort,
                                config.meterBaud,
                                config.meters
                            );
                            console.log("触发开始误差测试！")
                        }
                    }
                }
            }
        }

        // ==========================================
        // 底层：动态表头 + 真实大纲渲染区
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "white"
            border.color: "#C0C0C0"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // === 动态表头 ===
                RowLayout {
                    Layout.fillWidth: true
                    height: 45
                    spacing: 0

                    Rectangle {
                        width: 240; height: parent.height
                        color: "#F5F7FA"; border.color: "#C0C0C0"; border.width: 1
                        Label {
                            anchors.centerIn: parent
                            text: "测试条件"
                            font.bold: true; font.pixelSize: 16; color: textMain
                        }
                    }
                    Repeater {
                        model: getColumns(selectedCategory)
                        delegate: Rectangle {
                            Layout.fillWidth: true; height: parent.height
                            color: "#F5F7FA"; border.color: "#C0C0C0"; border.width: 1
                            Label {
                                anchors.centerIn: parent
                                text: modelData
                                font.bold: true; font.pixelSize: 16; color: textMain
                            }
                        }
                    }
                }

                // === 真实数据渲染区 ===
                ListView {
                    id: resultListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    interactive: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: currentTableModel

                    ScrollBar.vertical: ScrollBar {
                        active: true; policy: ScrollBar.AlwaysOn
                    }

                    delegate: RowLayout {
                        width: ListView.view.width
                        height: 40
                        spacing: 0

                        property var rowData: modelData
                        property int rowIdx: index
                        property bool isSelected: resultListView.currentIndex === rowIdx

                        HoverHandler { id: rowHover }

                        TapHandler {
                            onTapped: {
                                resultListView.currentIndex = rowIdx
                            }
                        }

                        property color rowBgColor: {
                            if (isSelected) return "#CCE8FF"
                            if (rowHover.hovered) return "#E6F7FF"
                            return rowIdx % 2 === 0 ? "#FFFFFF" : "#FAFAFA"
                        }

                        // 测试条件列
                        Rectangle {
                            width: 240; height: parent.height
                            color: rowBgColor; border.color: "#E0E0E0"; border.width: 1
                            Label {
                                anchors.centerIn: parent
                                text: rowData.header
                                font.bold: true; font.pixelSize: 14; color: textMain
                            }
                        }

                        // 测量结果单元格
                        Repeater {
                            model: rowData.cells
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                height: parent.height
                                color: rowBgColor
                                border.color: "#E0E0E0"
                                border.width: 1

                                Label {
                                    anchors.centerIn: parent
                                    text: modelData.errStr !== undefined ? modelData.errStr : "-"
                                    font.bold: true
                                    font.pixelSize: 16
                                    // 🌟 核心调色：是 '-' 时静静显灰，测到数据后变绿/红，极其夺目！
                                    color: {
                                        if (modelData.errStr === "-" || modelData.errStr === undefined) return "#909399";
                                        return modelData.isFail ? "#F44336" : "#388E3C";
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}