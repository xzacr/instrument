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

    // 表头列名：需要跟 C++ 下发的单元格数量严格对应
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

    // 初始化/清空所有数据的函数
    function clearAllData() {
        let fresh = [];
        for (let i = 0; i < 5; i++) {
            let cats = [];
            for (let c = 0; c < 8; c++) cats.push([]);
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
            // 通过 concat 浅拷贝生成新数组，强制 QML 刷新 ListView
            currentTableModel = [].concat(meterDataStore[selectedMeterIndex][selectedCategory]);
        }
    }

    onSelectedMeterIndexChanged: updateView()
    onSelectedCategoryChanged: updateView()
    Component.onCompleted: clearAllData()

    // 🌟 全局信号监听中枢
    Connections {
        target: ins

        // 监听测试启动状态，一旦启动，直接清空旧图表
        function onIsCalibratingChanged() {
            if (ins.isCalibrating) {
                clearAllData();
            }
        }

        // 接收顶部 5 个卡片的状态更新
        function onUpdateErrorMeterStatus(meterIndex, statusEnum, desc) {
            let statusStr = "IDLE";
            if (statusEnum === 0) statusStr = "IDLE";
            else if (statusEnum === 1) statusStr = "PASS";
            else if (statusEnum === 2) statusStr = "FAIL";
            else if (statusEnum === 3) statusStr = "RUNNING";
            else if (statusEnum === 4) statusStr = "TIMEOUT"; //     解析新增的掉线状态

            meterStateModel.setProperty(meterIndex, "status", statusStr);
            meterStateModel.setProperty(meterIndex, "desc", desc);
        }

        // 接收底层计算好的动态行数据
        function onAppendErrorRow(meterIndex, categoryIndex, rowName, rowCells) {
            // 🌟 1. 在更新数据前，先保存滚动条状态 (只在当前视图一致时才处理)
            let isAtBottom = false;
            let oldContentY = 0;
            let isCurrentView = (meterIndex === selectedMeterIndex && categoryIndex === selectedCategory);

            if (isCurrentView) {
                oldContentY = resultListView.contentY;
                // 如果内容高度已经超过了可视高度，才需要严格计算是否在底部
                if (resultListView.contentHeight > resultListView.height) {
                    isAtBottom = (resultListView.contentY >= resultListView.contentHeight - resultListView.height - 10);
                } else {
                    // 还没填满一页时，默认当做在底部，保持追踪
                    isAtBottom = true;
                }
            }

            // ==========================================
            // 2. 执行您原有的数据更新逻辑
            let data = meterDataStore;
            data[meterIndex][categoryIndex].push({
                "header": rowName,
                "cells": rowCells
            });
            meterDataStore = data;

            // ==========================================
            // 3. 刷新表格并执行智能滚动
            if (isCurrentView) {
                updateView(); // 触发界面重绘

                // 必须用 Qt.callLater，等 updateView 把新 UI 渲染出来后再调滚动条
                Qt.callLater(function() {
                    if (isAtBottom) {
                        // 【模式 A】：自动向下追踪最新数据
                        resultListView.positionViewAtEnd();
                    } else {
                        // 【模式 B】：锁死在您正在看的那行历史数据
                        if (resultListView.contentHeight > resultListView.height) {
                            resultListView.contentY = oldContentY;
                        }
                    }
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
                                            status === "TIMEOUT" ? "#F57C00" : // 经典警告橙黄
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
                                                    stateIcon.rotation = 0 // 动画一旦停止，立刻把角度归零
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

                        // 如果 config 不是 null，说明校验通过了
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

                // === 真实动态数据渲染区 ===
                ListView {
                    id: resultListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    interactive: true
                    boundsBehavior: Flickable.StopAtBounds

                    // 绑定为我们的局部动态数组
                    model: currentTableModel

                    ScrollBar.vertical: ScrollBar {
                        active: true; policy: ScrollBar.AlwaysOn
                    }

                    delegate: RowLayout {
                        width: ListView.view.width
                        height: 40
                        spacing: 0

                        property var rowData: modelData
                        // 🌟 保存当前行号，防止被内层的 Repeater 的 index 覆盖
                        property int rowIdx: index

                        // 1. 核心判断：当前行的索引是否等于 ListView 记录的“当前选中索引”
                        property bool isSelected: resultListView.currentIndex === rowIdx

                        // 2. 悬停监听
                        HoverHandler { id: rowHover }

                        // 3. 点击监听
                        TapHandler {
                            onTapped: {
                                resultListView.currentIndex = rowIdx
                            }
                        }

                        // 🌟 4. 颜色逻辑（优先级：选中 > 悬停 > 斑马纹）
                        property color rowBgColor: {
                            if (isSelected) return "#CCE8FF"       // 第一优先级：选中时，显示较深的选中蓝
                            if (rowHover.hovered) return "#E6F7FF" // 第二优先级：悬停时，显示较浅的悬停蓝
                            return rowIdx % 2 === 0 ? "#FFFFFF" : "#FAFAFA" // 第三优先级：默认斑马纹
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

                        // 真实测量数据列
                        Repeater {
                            model: rowData.cells
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                height: parent.height
                                color: rowBgColor // 同步使用外层算好的行背景色
                                border.color: "#E0E0E0"
                                border.width: 1

                                Label {
                                    anchors.centerIn: parent
                                    // 直接拿C++传来的 errStr，如果是undefined就显示 -
                                    text: modelData.errStr !== undefined ? modelData.errStr : "-"
                                    font.bold: true
                                    font.pixelSize: 16
                                    // 根据 C++ 的 isFail 字段判断是否标红
                                    color: modelData.isFail ? "#F44336" : "#388E3C"
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}