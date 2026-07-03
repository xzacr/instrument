import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/* STREAMING_CHUNK: Defining root window and core color palette */
Rectangle {
id: root
width: 1200
height: 800
color: "#F5F7FA" // 整体浅灰背景，契合主界面风格

// --- 颜色主题定义 ---
readonly property color themeColor: "#009688" // 深青色(启动按钮颜色)
readonly property color cardBg: "#FFFFFF"
readonly property color borderColor: "#E4E7ED"
readonly property color textMain: "#303133"
readonly property color textSub: "#909399"
readonly property color colorPass: "#67C23A" // 绿色
readonly property color colorFail: "#F56C6C" // 红色
readonly property color colorIdle: "#DCDFE6" // 灰色
readonly property color colorRunning: "#409EFF" // 蓝色

// --- 全局状态变量 ---
property int activeMeterIndex: 0 // 当前选中的仪表 (0-4)
property int activeCategoryIndex: 0 // 当前选中的参数类别 (0:电压, 1:电流, 2:有功, 3:无功, 4:视在, 5:功率因数, 6:谐波)
property bool showFailOnly: false // 仅显示异常过滤

// 类别定义
property var categories: ["电压", "电流", "有功功率", "无功功率", "视在功率", "功率因数", "高次谐波"]

/* STREAMING_CHUNK: Initializing mock data generator for 5 meters */
// --- 模拟后台数据模型 ---
// 实际开发中，这些数据将来自 C++ 的 QAbstractTableModel 或 QVariantMap
property var metersData: []

Component.onCompleted: {
    generateMockData()
}

function generateMockData() {
    let allData = []
    for (let m = 0; m < 5; m++) {
        let meter = {
            uiIndex: m,
            isEnabled: m !== 2, // 模拟第3台表未启用
            status: m === 2 ? 0 : (m === 1 ? 2 : (m === 4 ? 3 : 1)), // 0:Idle, 1:Pass, 2:Fail, 3:Running
            topMsg: m === 1 ? "B相电压超标, 3次谐波超标" : (m === 4 ? "正在测：5次谐波" : "全部测试合格"),
            categories: []
        }

        if(meter.isEnabled) {
            // 0: 电压 (3行 x 6列)
            meter.categories.push(createMatrix(
                ["20% (44V)", "100% (220V)", "120% (264V)"],
                ["Ua", "Ub", "Uc", "Uab", "Ubc", "Uac"],
                m === 1 // 让第2台表产生Fail
            ))
            // 1: 电流 (3行 x 3列)
            meter.categories.push(createMatrix(
                ["10% (0.5A)", "100% (5A)", "120% (6A)"],
                ["Ia", "Ib", "Ic"], false
            ))
            // 2,3,4: 功率 (模拟10行 x 4列: Pa, Pb, Pc, P总)
            for(let i=0; i<3; i++) {
                let pRows = []
                for(let r=1; r<=10; r++) pRows.push("工况 " + r + " (xxxV, xxxA)")
                meter.categories.push(createMatrix(pRows, ["相A", "相B", "相C", "总和"], false))
            }
            // 5: 功率因数
            meter.categories.push(createMatrix(["PF=1.0", "PF=0.5L", "PF=0.8C"], ["PFa", "PFb", "PFc", "PF总"], false))

            // 6: 高次谐波 (30行 x 6列)
            let hRows = []
            for(let h=2; h<=31; h++) hRows.push(h + "次谐波 (含有率)")
            meter.categories.push(createMatrix(hRows, ["Ua", "Ub", "Uc", "Ia", "Ib", "Ic"], m === 1))
        } else {
            for(let i=0; i<7; i++) meter.categories.push({rows:[], cols:[], cells:[]})
        }
        allData.push(meter)
    }
    metersData = allData
}

function createMatrix(rows, cols, injectFail) {
    let matrix = { rows: rows, cols: cols, cells: [] }
    for (let r = 0; r < rows.length; r++) {
        let rowData = []
        for (let c = 0; c < cols.length; c++) {
            // 模拟数据生成
            let isFail = injectFail && r === 1 && c === 1 // 固定让 100% Ub 或 3次谐波 Ub Fail
            let err = (Math.random() * (isFail ? 2.5 : 0.4)) * (Math.random()>0.5?1:-1)
            rowData.push({
                errStr: (err>0?"+":"") + err.toFixed(3) + "%",
                valStr: (220 * (1 + err/100)).toFixed(3), // 随便模拟个读数
                isFail: isFail
            })
        }
        matrix.cells.push(rowData)
    }
    return matrix
}

/* STREAMING_CHUNK: Building the Top Status Cards (Global Dashboard) */
ColumnLayout {
    anchors.fill: parent
    anchors.margins: 20
    spacing: 20

    // --- 顶部状态与选择看板 ---
    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        spacing: 15

        Repeater {
            model: metersData.length

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: cardBg
                radius: 8
                // 核心交互：选中状态高亮边框，去除了Qt5特有的DropShadow，使用纯净扁平风格
                border.width: activeMeterIndex === index ? 3 : 1
                border.color: activeMeterIndex === index ? themeColor : borderColor

                // 鼠标点击切换当前仪表
                MouseArea {
                    anchors.fill: parent
                    onClicked: activeMeterIndex = index
                }

                // 内容布局
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 5

                    // 标题栏： 1#仪表
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "仪表 " + (index + 1)
                            font.pixelSize: 16
                            font.bold: true
                            color: textMain
                        }
                        Item { Layout.fillWidth: true }
                        // 状态圆点
                        Rectangle {
                            width: 12; height: 12; radius: 6
                            color: {
                                let s = metersData[index].status
                                if(s === 0) return colorIdle
                                if(s === 1) return colorPass
                                if(s === 2) return colorFail
                                return colorRunning
                            }
                        }
                    }

                    // 大字状态 (PASS/FAIL)
                    Label {
                        Layout.alignment: Qt.AlignCenter
                        text: {
                            let s = metersData[index].status
                            if(s === 0) return "IDLE"
                            if(s === 1) return "PASS"
                            if(s === 2) return "FAIL"
                            return "RUNNING"
                        }
                        font.pixelSize: 32
                        font.bold: true
                        color: {
                            let s = metersData[index].status
                            if(s === 0) return textSub
                            if(s === 1) return colorPass
                            if(s === 2) return colorFail
                            return colorRunning
                        }
                    }

                    // 小字错误描述或进度
                    Label {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignCenter
                        text: metersData[index].topMsg
                        font.pixelSize: 12
                        color: textSub
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                // 选中时底部的小箭头指示器
                Rectangle {
                    visible: activeMeterIndex === index
                    width: 16; height: 16
                    color: themeColor
                    rotation: 45
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -8
                    z: -1 // 藏在卡片下面一半
                }
            }
        }
    }

    /* STREAMING_CHUNK: Building the Middle Navigation & Filters */
    // --- 中部导航区 ---
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 60
        color: cardBg
        border.color: borderColor
        radius: 8

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 20

            // 类别 Tab 导航
            Row {
                spacing: 10
                Repeater {
                    model: categories.length
                    Button {
                        text: categories[index]
                        font.pixelSize: 14
                        font.bold: activeCategoryIndex === index

                        // 扁平化风格按钮定制
                        background: Rectangle {
                            color: activeCategoryIndex === index ? themeColor : "#F0F2F5"
                            radius: 4
                        }
                        contentItem: Text {
                            text: parent.text
                            color: activeCategoryIndex === index ? "#FFFFFF" : textMain
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        onClicked: activeCategoryIndex = index
                    }
                }
            }

            Item { Layout.fillWidth: true } // 弹簧把 CheckBox 推到右边

            // 过滤 CheckBox
            CheckBox {
                text: "仅显示超标/异常项 (FAIL)"
                checked: showFailOnly
                onCheckedChanged: showFailOnly = checked
                font.pixelSize: 14
                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: showFailOnly ? colorFail : textMain
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: parent.indicator.width + parent.spacing
                }
            }
        }
    }

    /* STREAMING_CHUNK: Building the High-Density Data Matrix Engine */
    // --- 底部核心数据矩阵区 ---
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: cardBg
        border.color: borderColor
        radius: 8
        clip: true

        // 当前仪表的矩阵数据
        property var currentMatrix: {
            if(metersData.length === 0) return {rows:[], cols:[], cells:[]}
            let meter = metersData[activeMeterIndex]
            if(!meter.isEnabled) return {rows:[], cols:[], cells:[]}
            return meter.categories[activeCategoryIndex]
        }

        // 无数据或未启用提示
        Label {
            visible: !metersData[activeMeterIndex].isEnabled
            anchors.centerIn: parent
            text: "该仪表未启用，无校准数据。"
            font.pixelSize: 18
            color: textSub
        }

        // 矩阵主视图 (带滚动条，完美兼容 30 行谐波)
        ScrollView {
            anchors.fill: parent
            anchors.margins: 15
            visible: metersData[activeMeterIndex].isEnabled
            contentWidth: gridContent.width
            contentHeight: gridContent.height

            Column {
                id: gridContent
                spacing: 0

                // 1. 表头行 (第一列为空，后面是 Ua, Ub, Uc 等)
                Row {
                    Rectangle {
                        width: 180; height: 40 // 第一列更宽，容纳测试工况名字
                        color: "#F5F7FA"; border.color: borderColor; border.width: 1
                        Label { anchors.centerIn: parent; text: "测试条件"; font.bold: true; color: textMain }
                    }
                    Repeater {
                        model: parent.parent.parent.currentMatrix.cols
                        Rectangle {
                            width: 140; height: 40
                            color: "#F5F7FA"; border.color: borderColor; border.width: 1
                            Label { anchors.centerIn: parent; text: modelData; font.bold: true; color: textMain }
                        }
                    }
                }

                // 2. 数据行遍历
                Repeater {
                    model: parent.parent.currentMatrix.rows.length
                    delegate: Item {
                        id: rowWrapper
                        width: rowLayout.width
                        // 【核心过滤逻辑】：如果勾选了仅看Fail，且这行没有Fail，则高度设为0隐藏
                        property bool hasFailInRow: {
                            let cells = parent.parent.parent.currentMatrix.cells[index]
                            for(let i=0; i<cells.length; i++) {
                                if(cells[i].isFail) return true
                            }
                            return false
                        }
                        height: (root.showFailOnly && !hasFailInRow) ? 0 : 70
                        visible: height > 0
                        clip: true

                        Row {
                            id: rowLayout
                            // 行标题 (例如: "20% (44V)")
                            Rectangle {
                                width: 180; height: 70
                                color: "#FFFFFF"; border.color: borderColor; border.width: 1
                                Label {
                                    anchors.centerIn: parent
                                    text: parent.parent.parent.parent.parent.currentMatrix.rows[index]
                                    color: textMain
                                    font.pixelSize: 13
                                }
                            }

                            // 单元格数据遍历
                            Repeater {
                                model: parent.parent.parent.parent.currentMatrix.cols.length
                                delegate: Rectangle {
                                    width: 140; height: 70
                                    border.color: borderColor; border.width: 1

                                    // 提取单元格数据
                                    property var cellData: parent.parent.parent.parent.parent.currentMatrix.cells[rowWrapper.Positioner.index][index]

                                    // 【视觉告警】：如果是 Fail，背景微微泛红
                                    color: cellData.isFail ? "#FEF0F0" : "#FFFFFF"

                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 4
                                        // 上排大字：误差 %
                                        Label {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: cellData.errStr
                                            font.pixelSize: 16
                                            font.bold: true
                                            // 【视觉告警】：字变鲜红
                                            color: cellData.isFail ? colorFail : colorPass
                                        }
                                        // 下排小字：仪表读数
                                        Label {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: "读数: " + cellData.valStr
                                            font.pixelSize: 11
                                            color: cellData.isFail ? colorFail : textSub
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
}


}