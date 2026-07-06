import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Item {
    id: errorCalcPage

    property color themeColor: "#1976D2"
    property color textMain: "#333333"
    property color textSub: "#606266"
    readonly property int mode_ErrorCalc: 0

    property int selectedMeterIndex: 1
    property int selectedCategory: 0

    property var categories: [
        "电压", "电流", "有功功率", "无功功率",
        "视在功率", "功率因数", "谐波电压", "谐波电流"
    ]

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

    function getRowCount(cat) {
        if (cat === 0 || cat === 1) return 3
        if (cat === 2) return 51
        if (cat === 3) return 39
        if (cat === 4) return 15
        if (cat === 5) return 36
        if (cat === 6 || cat === 7) return 30
        return 0
    }

    function getRowHeader(cat, rowIdx) {
        if (cat === 0) return ["20% (44V)", "100% (220V)", "120% (264V)"][rowIdx % 3]
        if (cat === 1) return ["10% (0.5A)", "100% (5A)", "120% (6A)"][rowIdx % 3]

        let voltages = ["176V", "220V", "264V"];

        if (cat === 2) {
            let v = voltages[Math.floor(rowIdx / 17) % 3] || "220V";
            let subIdx = rowIdx % 17;
            if (subIdx < 5) {
                return v + ", PF=1.0, " + ["0.05A", "0.2A", "0.25A", "5.0A", "6.0A"][subIdx];
            } else if (subIdx < 11) {
                return v + ", PF=0.5L, " + ["0.1A", "0.25A", "0.4A", "0.5A", "5.0A", "6.0A"][subIdx - 5];
            } else {
                return v + ", PF=0.8C, " + ["0.1A", "0.25A", "0.4A", "0.5A", "5.0A", "6.0A"][subIdx - 11];
            }
        }

        if (cat === 3) {
            let v = voltages[Math.floor(rowIdx / 13) % 3] || "220V";
            let subIdx = rowIdx % 13;
            if (subIdx < 5) {
                return v + ", PF=0, " + ["0.1A", "0.2A", "0.25A", "5.0A", "6.0A"][subIdx];
            } else if (subIdx < 10) {
                return v + ", PF=0.866, " + ["0.25A", "0.4A", "0.5A", "5.0A", "6.0A"][subIdx - 5];
            } else {
                return v + ", PF=0.968, " + ["0.5A", "5.0A", "6.0A"][subIdx - 10];
            }
        }

        if (cat === 4) {
            let v = voltages[Math.floor(rowIdx / 5) % 3] || "220V";
            let subIdx = rowIdx % 5;
            return v + ", PF=1, " + ["0.1A", "0.2A", "0.3A", "5.0A", "6.0A"][subIdx];
        }

        if (cat === 5) {
            let pfVolts = ["110V", "220V", "264V"];
            let pfs = ["PF=0.5L", "PF=1.0", "PF=0.8C"];
            let currents = ["0.5A", "2.5A", "5.0A", "6.0A"];

            let v = pfVolts[Math.floor(rowIdx / 12) % 3];
            let pf = pfs[Math.floor((rowIdx % 12) / 4)];
            let c = currents[rowIdx % 4];

            return v + ", " + pf + ", " + c;
        }

        if (cat === 6 || cat === 7) return "50Hz/220V/5A, " + (rowIdx + 2) + "次谐波"

        return ""
    }

    function getLimit(cat, rowIdx, colIdx) {
        if (cat === 0 || cat === 1) return "0.5%";

        if (cat === 2) {
            let subIdx = rowIdx % 17;
            let isOnePercent = (
                subIdx === 0 || subIdx === 1 ||
                subIdx === 5 || subIdx === 6 || subIdx === 7 ||
                subIdx === 11 || subIdx === 12 || subIdx === 13
            );
            if (isOnePercent) return "1.0%";

            let isZeroSixPercent = (
                subIdx === 8 || subIdx === 9 || subIdx === 10 ||
                subIdx === 14 || subIdx === 15 || subIdx === 16
            );
            if (isZeroSixPercent) return "0.6%";

            return "0.5%";
        }

        if (cat === 3) {
            let subIdx = rowIdx % 13;
            let isZeroSixTwoFive = (
                subIdx === 0 || subIdx === 1 ||
                subIdx === 5 || subIdx === 6 ||
                subIdx === 10 || subIdx === 11 || subIdx === 12
            );
            if (isZeroSixTwoFive) return "0.625%";
            return "0.50%";
        }

        if (cat === 4) {
            let subIdx = rowIdx % 5;
            if (subIdx === 0 || subIdx === 1) return "1.000%";
            return "0.50%";
        }

        if (cat === 5) return "0.5%";

        if (cat === 6) {
            return colIdx < 3 ? "0.15%" : "5.00%";
        }
        if (cat === 7) {
            return colIdx < 3 ? "0.50%" : "5.00%";
        }

        return "0.5%";
    }

    property var meterStates: [
        { title: "1#仪表", status: "PASS", desc: "全部合格" },
        { title: "2#仪表", status: "FAIL", desc: "A相电压超标" },
        { title: "3#仪表", status: "IDLE", desc: "未启用" },
        { title: "4#仪表", status: "PASS", desc: "全部合格" },
        { title: "5#仪表", status: "RUNNING", desc: "读取3次谐波..." }
    ]

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
                model: 5
                delegate: Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    property string status: meterStates[index].status
                    property color bgColor: status === "PASS" ? "#4CAF50" :
                                            status === "FAIL" ? "#F44336" :
                                            status === "RUNNING" ? "#2196F3" : "#9E9E9E"

                    property bool isSelected: selectedMeterIndex === index

                    MouseArea {
                        id: cardMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: selectedMeterIndex = index
                    }

                    // 悬浮弹起距离动画
                    property real currentMargin: isSelected ? 4 : (cardMouseArea.containsMouse ? 8 : 12)
                    Behavior on currentMargin { NumberAnimation { duration: 250; easing.type: Easing.OutBack } }

                    // 合并主卡片和倒三角
                    Item {
                        id: shapeSource
                        anchors.fill: parent
                        anchors.margins: currentMargin
                        visible: false // 仅供 MultiEffect 使用

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

                    // 3. 干净锐利的光影层 (不再忽明忽暗，稳定渲染)
                    MultiEffect {
                        source: shapeSource
                        anchors.fill: shapeSource
                        shadowEnabled: true
                        // 泛出稳定的同色光
                        shadowColor: bgColor
                        // 固定的高品质模糊
                        shadowBlur: 1.0
                        // 提亮
                        shadowOpacity: 0.6
                        // 投影拉长体现浮起
                        shadowVerticalOffset:  8

                        Behavior on shadowOpacity { NumberAnimation { duration: 250 } }
                        Behavior on shadowVerticalOffset { NumberAnimation { duration: 250 } }
                        Behavior on shadowColor { ColorAnimation { duration: 250 } }
                    }

                    // 4. 真实可见UI层
                    Item {
                        anchors.fill: parent
                        anchors.margins: currentMargin

                        // 倒三角底层
                        Rectangle {
                            visible: isSelected
                            width: 20; height: 20
                            rotation: 45
                            color: bgColor
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: mainVisCard.bottom
                        }

                        // 主可视卡片
                        Rectangle {
                            id: mainVisCard
                            anchors.fill: parent
                            anchors.bottomMargin: 14
                            radius: 8
                            color: bgColor
                            border.color: isSelected ? Qt.lighter(bgColor, 1.2) : "transparent"
                            border.width: isSelected ? 1 : 0
                            clip: true

                            // 顶部深色条
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
                                        text: meterStates[index].title
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 16
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        id: stateIcon
                                        text: status === "FAIL" ? "❗" : (status === "PASS" ? "✔" : (status === "RUNNING" ? "⚙" : "○"))
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 16

                                        RotationAnimation {
                                            target: stateIcon
                                            loops: Animation.Infinite
                                            from: 0; to: 360
                                            duration: 1500
                                            running: status === "RUNNING"
                                        }
                                    }
                                }
                            }

                            // 大字状态
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
                                    text: status === "PASS" ? "合格" : status === "FAIL" ? "不合格" : status === "IDLE" ? "未启用" : "正在测试"
                                    color: "white"
                                    font.pixelSize: 18
                                    font.bold: true
                                    Layout.alignment: Qt.AlignCenter
                                }
                            }

                            // 底部小字
                            Label {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottomMargin: 10
                                text: meterStates[index].desc
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
                    font.pixelSize: 20
                    font.bold: selectedCategory === index

                    Layout.preferredHeight: 50
                    Layout.preferredWidth: implicitWidth + 40

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

            // 弹簧：占据剩余所有空间，把后面的按钮硬推到最右侧
            Item { Layout.fillWidth: true }

            // 二合一启动/停止按钮
            Button {
                id: errorTestBtn
                Layout.preferredHeight: 50
                Layout.preferredWidth: 160

                // 监听底层的运行状态
                property bool isRunning: typeof ins !== "undefined" ? ins.isCalibrating : false

                background: Rectangle {
                    // 运行时显示红色警示，闲置时显示主题蓝色，按下时略微变暗
                    color: parent.pressed ? Qt.darker((errorTestBtn.isRunning ? "#F44336" : themeColor), 1.1) : (errorTestBtn.isRunning ? "#F44336" : themeColor)
                    radius: 6
                }

                contentItem: Text {
                    // 根据状态切换文本
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
                        // TODO: 在这里调用底层的 C++ 方法，比如传入 Mode=1 代表单测误差
                        ins.startTask(mode_ErrorCalc);
                        console.log("触发开始误差测试！")
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

                RowLayout {
                    Layout.fillWidth: true
                    height: 45
                    spacing: 0

                    Rectangle {
                        width: 240
                        height: parent.height
                        color: "#F5F7FA"
                        border.color: "#C0C0C0"
                        border.width: 1
                        Label {
                            anchors.centerIn: parent
                            text: "测试条件"
                            font.bold: true
                            font.pixelSize: 16
                            color: textMain
                        }
                    }

                    Repeater {
                        model: getColumns(selectedCategory)
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: parent.height
                            color: "#F5F7FA"
                            border.color: "#C0C0C0"
                            border.width: 1
                            Label {
                                anchors.centerIn: parent
                                text: modelData
                                font.bold: true
                                font.pixelSize: 16
                                color: textMain
                            }
                        }
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOn

                    ListView {
                        width: parent.width
                        model: getRowCount(selectedCategory)

                        delegate: RowLayout {
                            width: ListView.view.width
                            height: 40
                            spacing: 0

                            property int rowIdx: index
                            visible: true

                            Rectangle {
                                width: 240
                                height: parent.height
                                color: rowIdx % 2 === 0 ? "#FFFFFF" : "#FAFAFA"
                                border.color: "#E0E0E0"
                                border.width: 1
                                Label {
                                    anchors.centerIn: parent
                                    text: getRowHeader(selectedCategory, rowIdx)
                                    font.bold: true
                                    font.pixelSize: 14
                                    color: textMain
                                }
                            }

                            Repeater {
                                model: getColumns(selectedCategory).length
                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    height: parent.height
                                    border.color: "#E0E0E0"
                                    border.width: 1

                                    color: rowIdx % 2 === 0 ? "#FFFFFF" : "#FAFAFA"

                                    Label {
                                        anchors.centerIn: parent
                                        text: getLimit(selectedCategory, rowIdx, model.index)
                                        font.bold: true
                                        font.pixelSize: 16
                                        color: "#388E3C"
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
