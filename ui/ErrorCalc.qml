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

    // =====================================================================
    // 🌟 2. 固定量产测试大纲表头 (必须与 C++ 初始化里的 string name 对应)
    // =====================================================================
    function getRows(cat) {
        // --- 0: 电压 / 1: 电流 ---
        if (cat === 0 || cat === 1) {
            return [
                "44V,  0.5A",
                "220V, 5.0A",
                "264V, 6.0A"
            ];
        }
        // --- 2: 有功功率 (51个点) ---
        if (cat === 2) {
            return [
                // 第一组：176V
                "176V, PF=1.0, 0.05A", "176V, PF=1.0, 0.20A", "176V, PF=1.0, 0.25A", "176V, PF=1.0, 5.00A", "176V, PF=1.0, 6.00A",
                "176V, PF=0.5L, 0.10A", "176V, PF=0.5L, 0.25A", "176V, PF=0.5L, 0.40A", "176V, PF=0.5L, 0.50A", "176V, PF=0.5L, 5.00A", "176V, PF=0.5L, 6.00A",
                "176V, PF=0.8C, 0.10A", "176V, PF=0.8C, 0.25A", "176V, PF=0.8C, 0.40A", "176V, PF=0.8C, 0.50A", "176V, PF=0.8C, 5.00A", "176V, PF=0.8C, 6.00A",
                // 第二组：220V
                "220V, PF=1.0, 0.05A", "220V, PF=1.0, 0.20A", "220V, PF=1.0, 0.25A", "220V, PF=1.0, 5.00A", "220V, PF=1.0, 6.00A",
                "220V, PF=0.5L, 0.10A", "220V, PF=0.5L, 0.25A", "220V, PF=0.5L, 0.40A", "220V, PF=0.5L, 0.50A", "220V, PF=0.5L, 5.00A", "220V, PF=0.5L, 6.00A",
                "220V, PF=0.8C, 0.10A", "220V, PF=0.8C, 0.25A", "220V, PF=0.8C, 0.40A", "220V, PF=0.8C, 0.50A", "220V, PF=0.8C, 5.00A", "220V, PF=0.8C, 6.00A",
                // 第三组：264V
                "264V, PF=1.0, 0.05A", "264V, PF=1.0, 0.20A", "264V, PF=1.0, 0.25A", "264V, PF=1.0, 5.00A", "264V, PF=1.0, 6.00A",
                "264V, PF=0.5L, 0.10A", "264V, PF=0.5L, 0.25A", "264V, PF=0.5L, 0.40A", "264V, PF=0.5L, 0.50A", "264V, PF=0.5L, 5.00A", "264V, PF=0.5L, 6.00A",
                "264V, PF=0.8C, 0.10A", "264V, PF=0.8C, 0.25A", "264V, PF=0.8C, 0.40A", "264V, PF=0.8C, 0.50A", "264V, PF=0.8C, 5.00A", "264V, PF=0.8C, 6.00A"
            ];
        }
        // --- 3: 无功功率 (39个点) ---
        if (cat === 3) {
            return [
                // 第一组：176V
                "176V, PF=0, 0.1A", "176V, PF=0, 0.2A", "176V, PF=0, 0.25A", "176V, PF=0, 5.0A", "176V, PF=0, 6.0A",
                "176V, PF=0.866, 0.25A", "176V, PF=0.866, 0.4A", "176V, PF=0.866, 0.5A", "176V, PF=0.866, 5.0A", "176V, PF=0.866, 6.0A",
                "176V, PF=0.968, 0.5A", "176V, PF=0.968, 5.0A", "176V, PF=0.968, 6.0A",
                // 第二组：220V
                "220V, PF=0, 0.1A", "220V, PF=0, 0.2A", "220V, PF=0, 0.25A", "220V, PF=0, 5.0A", "220V, PF=0, 6.0A",
                "220V, PF=0.866, 0.25A", "220V, PF=0.866, 0.4A", "220V, PF=0.866, 0.5A", "220V, PF=0.866, 5.0A", "220V, PF=0.866, 6.0A",
                "220V, PF=0.968, 0.5A", "220V, PF=0.968, 5.0A", "220V, PF=0.968, 6.0A",
                // 第三组：264V
                "264V, PF=0, 0.1A", "264V, PF=0, 0.2A", "264V, PF=0, 0.25A", "264V, PF=0, 5.0A", "264V, PF=0, 6.0A",
                "264V, PF=0.866, 0.25A", "264V, PF=0.866, 0.4A", "264V, PF=0.866, 0.5A", "264V, PF=0.866, 5.0A", "264V, PF=0.866, 6.0A",
                "264V, PF=0.968, 0.5A", "264V, PF=0.968, 5.0A", "264V, PF=0.968, 6.0A"
            ];
        }
        // --- 4: 视在功率 (15个点) ---
        if (cat === 4) {
            return [
                "176V, PF=1, 0.1A", "176V, PF=1, 0.2A", "176V, PF=1, 0.3A", "176V, PF=1, 5.0A", "176V, PF=1, 6.0A",
                "220V, PF=1, 0.1A", "220V, PF=1, 0.2A", "220V, PF=1, 0.3A", "220V, PF=1, 5.0A", "220V, PF=1, 6.0A",
                "264V, PF=1, 0.1A", "264V, PF=1, 0.2A", "264V, PF=1, 0.3A", "264V, PF=1, 5.0A", "264V, PF=1, 6.0A"
            ];
        }
        // --- 5: 功率因数 (36个点) ---
        if (cat === 5) {
            return [
                // 第一组：110V
                "110V, PF=0.5L, 0.5A", "110V, PF=0.5L, 2.5A", "110V, PF=0.5L, 5.0A", "110V, PF=0.5L, 6.0A",
                "110V, PF=1.0, 0.5A", "110V, PF=1.0, 2.5A", "110V, PF=1.0, 5.0A", "110V, PF=1.0, 6.0A",
                "110V, PF=0.8C, 0.5A", "110V, PF=0.8C, 2.5A", "110V, PF=0.8C, 5.0A", "110V, PF=0.8C, 6.0A",
                // 第二组：220V
                "220V, PF=0.5L, 0.5A", "220V, PF=0.5L, 2.5A", "220V, PF=0.5L, 5.0A", "220V, PF=0.5L, 6.0A",
                "220V, PF=1.0, 0.5A", "220V, PF=1.0, 2.5A", "220V, PF=1.0, 5.0A", "220V, PF=1.0, 6.0A",
                "220V, PF=0.8C, 0.5A", "220V, PF=0.8C, 2.5A", "220V, PF=0.8C, 5.0A", "220V, PF=0.8C, 6.0A",
                // 第三组：264V
                "264V, PF=0.5L, 0.5A", "264V, PF=0.5L, 2.5A", "264V, PF=0.5L, 5.0A", "264V, PF=0.5L, 6.0A",
                "264V, PF=1.0, 0.5A", "264V, PF=1.0, 2.5A", "264V, PF=1.0, 5.0A", "264V, PF=1.0, 6.0A",
                "264V, PF=0.8C, 0.5A", "264V, PF=0.8C, 2.5A", "264V, PF=0.8C, 5.0A", "264V, PF=0.8C, 6.0A"
            ];
        }
        // --- 6: 谐波电压 / 7: 谐波电流 (预留) ---
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

        function onUpdateErrorMeterStatus(meterIndex, statusEnum, desc) {
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
        function onAppendErrorRow(meterIndex, categoryIndex, rowName, rowCells) {
            //console.log("onAppendErrorRowwwwwwwwwwwwwwwwwwwwwwwwwwww")
            let data = meterDataStore;
            let catRows = data[meterIndex][categoryIndex];
            let foundIndex = -1;

            // --- 核心：严格通过名字精准替换，哪怕顺序变了也不会写错 ---
            for (let r = 0; r < catRows.length; r++) {
                if (catRows[r].header === rowName) {
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

            if (meterIndex === selectedMeterIndex) {
                // 1. 智能对偶归并：防止成对数据导致标签前后疯狂闪跳
                let targetCategory = categoryIndex;
                if (categoryIndex === 1) targetCategory = 0; // 收到电流(1) -> 稳在“电压(0)”标签
                if (categoryIndex === 7) targetCategory = 6; // 收到谐波电流(7) -> 稳在“谐波电压(6)”标签

                // 2. 如果正在测试的分类和当前看的不一样，自动把标签切过去！
                if (selectedCategory !== targetCategory) {
                    selectedCategory = targetCategory; // 这一步会自动触发 QML 视图重绘
                } else {
                    updateView(); // 标签没变，正常手动刷新当前表格的值
                }

                // 3. 延迟一帧让视图将新选中的标签表格渲染完毕，再精准对准到中间！
                Qt.callLater(function() {
                    resultListView.positionViewAtIndex(foundIndex, ListView.Center);
                });
            }
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
                    text: modelData
                    font.pixelSize: 18
                    font.bold: selectedCategory === index
                    Layout.preferredHeight: 50
                    Layout.preferredWidth: implicitWidth + 30
                    background: Rectangle {
                        color: selectedCategory === index ? themeColor : "#F0F2F5"
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: selectedCategory === index ? "#FFFFFF" : textMain
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: selectedCategory = index
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                id: errorTestBtn
                Layout.preferredHeight: 50
                Layout.preferredWidth: 160
                property bool isRunning: typeof ins !== "undefined" ? ins.isCalibrating : false

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