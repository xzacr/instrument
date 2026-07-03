import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: calibrationPage
    //anchors.fill: parent

    Connections {
        target: ins

        // 原来的全局提示
        function onShowTopMsg(msg, type) {
            topMsg.display(msg, type)
        }

        // 接收步骤状态更新
        function onMeterStepStatusChanged(meterIndex, stepIndex, status) {
            let groupBox = meterRepeater.itemAt(meterIndex)
            if (groupBox) {
                groupBox.updateStep(stepIndex, status)
            }
        }

        function onSrcMessage(msg, type) {
            // 1. 更新文本
            srctext.text = msg

            // 2. 根据 type 动态变色
            if (type === "error") {
                // 如果是失败或报错，变成红色
                srctext.color = Material.color(Material.Red)
            } else {
                // 其他情况（如 "info"），恢复默认的深蓝色
                srctext.color = Material.color(Material.Indigo)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 20

        // ==========================================
        // 顶部：通信配置区 (维持原样，左右布局)
        // ==========================================
        //
        RowLayout {
            Layout.fillWidth: true
            spacing: 40 // 这个控制两个半区中间的留白大小

            // ==========================================
            // 第一半区：左侧的标准源配置（严格占 50%）
            // ==========================================
            ColumnLayout {
                Layout.fillWidth: true // 开启平分机制
                spacing: 10

                Label {
                    text: qsTr("标准源通信配置")
                    font.bold: true
                }

                RowLayout {
                    spacing: 10
                    Label { text: qsTr("串口:") }
                    ComboBox {
                        id: srcport
                        model: ins.availablePorts
                        // 如果列表为空，显示提示
                        displayText: count > 0 ? currentText : "未发现串口"
                        Layout.preferredWidth: 150
                        enabled: !ins.isCalibrating
                        onActivated: {
                            console.log("标准源选择了",currentText)
                        }
                    }
                    ToolButton {
                        id: refreshBtn
                        enabled: !ins.isCalibrating
                        icon.source: "image/refresh.svg"
                        icon.width: 28
                        icon.height: 28
                        icon.color: "#606266" // 给图标一个高级的深灰色，而不是纯黑

                        background: Rectangle {
                            implicitWidth: 44
                            implicitHeight: 44
                            radius: 22
                            color: parent.hovered ? "#F0F2F5" : "transparent"
                        }

                        // 增加鼠标悬停提示，增加交互感
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("刷新串口列表")

                        // 点击时的旋转动画
                        onClicked: {
                            console.log("标准源刷新串口")
                            rotateAnim.restart()
                            ins.refreshPorts()
                        }

                        RotationAnimation {
                            id: rotateAnim
                            target: refreshBtn.contentItem // 让图标转动
                            from: 0; to: 360; duration: 500
                            easing.type: Easing.OutCubic
                        }
                    }
                    Label { text: qsTr("波特率:") }
                    ComboBox {
                        id: srcbaud
                        model: [2400,4800,9600,19200,28800,38400,56700,115200];
                        currentIndex: 5;
                        Layout.preferredWidth: 150
                        enabled: !ins.isCalibrating
                    }
                    // 【内部弹簧】：把上面的控件紧紧往左边推，防止它们在这个 50% 区域里散开
                    Item { Layout.fillWidth: true }
                }
            }

            // ==========================================
            // 第二半区：右侧的仪表配置（严格占 50%）
            // ==========================================
            ColumnLayout {
                Layout.fillWidth: true // 开启平分机制
                spacing: 10

                Label {
                    text: qsTr("仪表通信配置")
                    font.bold: true
                }

                RowLayout {
                    spacing: 10
                    Label { text: qsTr("串口:") }
                    ComboBox {
                        id: meterport
                        model: ins.availablePorts
                        // 如果列表为空，显示提示
                        displayText: count > 0 ? currentText : "未发现串口"
                        Layout.preferredWidth: 150
                        enabled: !ins.isCalibrating
                        onActivated: {
                            console.log("仪表选择了",currentText)
                        }
                    }
                    ToolButton {
                        id: refreshBtn1
                        enabled: !ins.isCalibrating
                        icon.source: "image/refresh.svg"
                        icon.width: 28
                        icon.height: 28
                        icon.color: "#606266" // 给图标一个高级的深灰色，而不是纯黑

                        background: Rectangle {
                            implicitWidth: 44
                            implicitHeight: 44
                            radius: 22
                            color: parent.hovered ? "#F0F2F5" : "transparent"
                        }

                        // 增加鼠标悬停提示，增加交互感
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("刷新串口列表")

                        // 点击时的旋转动画
                        onClicked: {
                            console.log("仪表刷新串口")
                            rotateAnim1.restart()
                            ins.refreshPorts()
                        }

                        RotationAnimation {
                            id: rotateAnim1
                            target: refreshBtn1.contentItem // 让图标转动
                            from: 0; to: 360; duration: 500
                            easing.type: Easing.OutCubic
                        }
                    }
                    Label { text: qsTr("波特率:") }
                    ComboBox {
                        id: meterbaud
                        model: [2400,4800,9600,19200,28800,38400,56700,115200];
                        currentIndex: 2
                        Layout.preferredWidth: 150
                        enabled: !ins.isCalibrating
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        Rectangle{
            height: 1
            Layout.fillWidth: true
            color: "#9F9D9E"
            Layout.topMargin: -20
        }

        // 中部：全局校准控制区
        // ==========================================

        RowLayout{
            RowLayout {
                Layout.alignment: Qt.AlignCenter
                Label { text: qsTr("标准源当前状态:"); font.bold: true }
                Label {
                    id: srctext
                    text: qsTr("unknow")
                    color: Material.color(Material.Indigo)
                    font.pixelSize: 18
                    font.bold: true
                    Layout.minimumWidth: 200
                }
            }

            Item {
                Layout.preferredWidth: 200
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 20
                //Layout.alignment: Qt.AlignCenter
                Button {
                    id: startCalibrateBtn
                    text: qsTr("▶ 启动全自动校准")
                    font.bold: true
                    highlighted: true
                    Layout.preferredHeight: 60
                    enabled: !ins.isCalibrating
                    Material.background: Material.Teal
                    onClicked: {
                        if(sn.text === ""){
                            topMsg.display("编号不能为空","error")
                        }else{
                            // 1. 创建一个空数组，用来装 5 个表的数据
                            let meterConfigs = []

                            // 2. 遍历 5 个 GroupBox，提取是否启用和地址
                            for (let i = 0; i < meterRepeater.count; i++) {
                                let item = meterRepeater.itemAt(i)
                                if (item) {
                                    item.resetAllSteps()

                                    meterConfigs.push({
                                        "enabled": item.isEnabled,
                                        "address": item.meterAddr
                                    })
                                }
                            }
                            ins.startCalibration(srcport.currentText,srcbaud.currentValue,meterport.currentText,meterbaud.currentValue,meterConfigs)
                        }
                    }
                }

                Button {
                    text: qsTr("停止校准")
                    font.bold: true
                    highlighted: true
                    enabled: ins.isCalibrating
                    Layout.preferredHeight: 60
                    Material.background: Material.Red
                    onClicked: {
                        // 【绑定停止】
                        ins.stopCalibration()
                        bottomStatusPanel.setStatus("用户取消校准", "error")
                    }
                }
            }
        }


        Item {
            Layout.preferredHeight: 10
        }

        // ==========================================
        // 3. 底部：5个并行的仪表校准监控看板
        // ==========================================
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true // 填满下方剩余空间
            spacing: 12

            // 使用 Repeater 循环创建 5 个板块
            Repeater {
                id: meterRepeater
                model: 5

                GroupBox {
                    id: meterGroup
                    property bool isEnabled: enableCheck.checked
                    property int meterAddr: addrSpinBox.value
                    function updateStep(stepIdx, newStatus) {
                        stepModel.setProperty(stepIdx, "status", newStatus)
                    }
                    // 清空所有步骤状态的函数
                    function resetAllSteps() {
                        for (let i = 0; i < stepModel.count; i++) {
                            stepModel.setProperty(i, "status", 0) // 0 代表灰色等待状态
                        }
                    }
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // 动态标题
                    title: qsTr("仪表 " + (index + 1))
                    font.bold: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        // --- 仪表使能与地址配置 ---
                        RowLayout {
                            Layout.fillWidth: true
                            CheckBox {
                                id: enableCheck
                                text: qsTr("启用")
                                font.bold: false
                                font.pixelSize: 18
                                enabled: !ins.isCalibrating
                                checked: true // 默认全勾选，如果不勾选则跳过此表
                            }
                            Item { Layout.fillWidth: true }
                            Label { text: "地址:" ;font.bold: false}
                            SpinBox {
                                id: addrSpinBox
                                value: index + 1 // 默认地址 1, 2, 3, 4, 5
                                from: 1; to: 255
                                Layout.preferredWidth: 130
                                enabled: enableCheck.checked && !ins.isCalibrating
                                editable: true
                                font.bold: false
                                font.pixelSize: 18
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: " 编号:  "
                                font.bold: false
                                font.pixelSize: 18
                            }
                            TextField{
                                id: sn
                                Layout.fillWidth: true
                                font.bold: false
                                font.pixelSize: 18
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: "#E0E0E0"
                        }

                        // --- 校准步骤列表 ---
                        // 这里写死了一个前端模拟用的 ListModel。
                        // 实际开发中，你会用 C++ 传过来的 QAbstractListModel 替换它。
                        // 状态码预设：0=等待中(灰色), 1=正在执行(蓝色), 2=成功(绿色✔), 3=失败(红色✘)
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            enabled: enableCheck.checked
                            opacity: enabled ? 1.0 : 0.4
                            clip: true
                            spacing: 16
                            interactive: false // 步骤不多不需要滚动

                            model: ListModel {
                                id: stepModel
                                ListElement { stepName: "解除写保护"; status: 0 }
                                ListElement { stepName: "校准准备"; status: 0 }
                                ListElement { stepName: "电压电流校准 (PF=1.0)"; status: 0 }
                                ListElement { stepName: "电压电流校准 (PF=0.5)"; status: 0 }
                                ListElement { stepName: "参数固化保存"; status: 0 }
                                ListElement { stepName: "等待复位"; status: 0 }
                            }

                            delegate: RowLayout {
                                width: ListView.view.width
                                spacing: 10

                                // 状态图标显示逻辑
                                Label {
                                    font.pixelSize: 18
                                    font.bold: true
                                    text: {
                                        if (status === 0) return "○" // 等待
                                        if (status === 1) return "➤" // 运行中
                                        if (status === 2) return "✔" // 成功
                                        if (status === 3) return "✘" // 失败
                                        return ""
                                    }
                                    color: {
                                        if (status === 0) return "gray"
                                        if (status === 1) return Material.color(Material.Blue)
                                        if (status === 2) return Material.color(Material.Green)
                                        if (status === 3) return Material.color(Material.Red)
                                        return "black"
                                    }
                                }

                                // 步骤名称
                                Label {
                                    text: stepName
                                    font.pixelSize: 16
                                    color: status === 1 ? Material.color(Material.Indigo) : "black"
                                    font.bold: status === 1
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }
        }
        // ==========================================
        // 4. 底部：全局运行结果看板
        // ==========================================
        GroupBox {
            id: bottomStatusPanel
            Layout.fillWidth: true
            Layout.preferredHeight: 70

            function setStatus(msg, type) {
                resultText.text = msg
                if (type === "success") {
                    resultText.color = Material.color(Material.Green)
                } else if (type === "error") {
                    resultText.color = Material.color(Material.Red)
                } else {
                    resultText.color = Material.color(Material.Indigo)
                }
            }

            // 🌟 1. 左侧固定标题，钉在左边
            Label {
                text: qsTr("校准结果：")
                font.bold: true
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter // 垂直居中
            }

            // 🌟 2. 状态文本，无视其他控件，绝对居中！
            Label {
                id: resultText
                text: qsTr("系统空闲，等待启动")
                font.pixelSize: 22
                font.bold: true
                color: "gray"

                // 【核心代码】：把它绝对定位在整个 GroupBox 的正中心
                anchors.centerIn: parent
            }

            // 监听信号同步显示
            Connections {
                target: ins
                function onShowTopMsg(msg, type) {
                    bottomStatusPanel.setStatus(msg, type)
                }

                function onIsCalibratingChanged() {
                    if (ins.isCalibrating) {
                        bottomStatusPanel.setStatus("🚀 自动化校准执行中...", "info")
                    }
                }
            }
        }
    }
}

// Label {
//     text: qsTr("标准源选择:")
//     font.bold: true
// }

// RadioButton {
//     text: "XL-3200"
//     checked: true // 默认选中
// }

// RadioButton {
//     text: "STR3060A"
// }

// Item {
//     Layout.preferredWidth: 10
// }
