import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: calibrationPage

    readonly property color pageBg: "#FFFBFE"     // 页面底色 (高级浅灰)
    readonly property color cardBg: "#FFFFFF"     // 卡片底色 (纯白)
    readonly property color borderColor: "#E4E7ED"// 极简边框色
    readonly property color themeColor: "#1976D2" // 主题蓝
    readonly property color themeGreen: "#67C23A" // 成功绿
    readonly property color themeRed: "#F56C6C"   // 警示红
    readonly property color textMain: "#303133"   // 正文黑
    readonly property color textSub: "#555555"    // 辅助灰
    readonly property int titleSize: 22
    readonly property int subSize: 18
    // 绘制全局高级灰底色
    Rectangle {
        anchors.fill: parent
        color: pageBg
        z: -1
    }

    Connections {
        target: ins

        // 全局提示
        function onShowTopMsg(msg, type) {
            topMsg.display(msg, type)
        }

        // 接收步骤状态更新
        function onMeterStepStatusChanged(meterIndex, stepIndex, status) {
            let meterCard = meterRepeater.itemAt(meterIndex)
            if (meterCard) {
                meterCard.updateStep(stepIndex, status)
            }
        }

        function onSrcMessage(msg, type) {
            srctext.text = msg
            if (type === "error") {
                srctext.color = themeRed
            } else {
                srctext.color = themeColor
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        // ==========================================
        // 1. 顶部：通信配置区 & 全局控制 (融合进一张大卡片)
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            color: cardBg
            radius: 8
            border.color: borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 30

                // === 左半区：标准源通信配置 ===
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Label {
                        text: qsTr("标准源串口配置")
                        font.pixelSize: titleSize
                        font.bold: true
                        color: textMain
                    }

                    RowLayout {
                        spacing: 15
                        Label { text: qsTr("通信串口"); color: textSub; font.pixelSize: subSize }
                        ComboBox {
                            id: srcport
                            model: ins.availablePorts
                            displayText: count > 0 ? currentText : "未发现串口"
                            Layout.preferredWidth: 160
                            enabled: !ins.isCalibrating
                            font.pixelSize: 18
                        }
                        ToolButton {
                            id: refreshBtn
                            enabled: !ins.isCalibrating
                            icon.source: "image/refresh.svg"
                            icon.width: 24; icon.height: 24
                            icon.color: textSub
                            background: Rectangle {
                                implicitWidth: 40; implicitHeight: 40; radius: 20
                                color: parent.hovered ? "#F0F2F5" : "transparent"
                            }
                            ToolTip.visible: hovered; ToolTip.text: qsTr("刷新列表")
                            onClicked: { rotateAnim.restart(); ins.refreshPorts() }
                            RotationAnimation {
                                id: rotateAnim
                                target: refreshBtn.contentItem
                                from: 0; to: 360; duration: 500
                                easing.type: Easing.OutCubic
                            }
                        }
                        Label { text: qsTr("波特率"); color: textSub; font.pixelSize: subSize }
                        ComboBox {
                            id: srcbaud
                            model: [2400,4800,9600,19200,28800,38400,56700,115200]
                            currentIndex: 5
                            Layout.preferredWidth: 120
                            enabled: !ins.isCalibrating
                            font.pixelSize: 18
                        }
                        Item { Layout.fillWidth: true } // 左侧弹簧
                    }
                }

                // 分割线
                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: borderColor
                }

                // === 右半区：仪表通信配置 ===
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Label {
                        text: qsTr("仪表串口配置")
                        font.pixelSize: titleSize
                        font.bold: true
                        color: textMain
                    }

                    RowLayout {
                        spacing: 15
                        Label { text: qsTr("通信串口"); color: textSub; font.pixelSize: subSize }
                        ComboBox {
                            id: meterport
                            model: ins.availablePorts
                            displayText: count > 0 ? currentText : "未发现串口"
                            Layout.preferredWidth: 160
                            enabled: !ins.isCalibrating
                            font.pixelSize: 18
                        }
                        ToolButton {
                            id: refreshBtn1
                            enabled: !ins.isCalibrating
                            icon.source: "image/refresh.svg"
                            icon.width: 24; icon.height: 24
                            icon.color: textSub
                            background: Rectangle {
                                implicitWidth: 40; implicitHeight: 40; radius: 20
                                color: parent.hovered ? "#F0F2F5" : "transparent"
                            }
                            ToolTip.visible: hovered; ToolTip.text: qsTr("刷新列表")
                            onClicked: { rotateAnim1.restart(); ins.refreshPorts() }
                            RotationAnimation {
                                id: rotateAnim1
                                target: refreshBtn1.contentItem
                                from: 0; to: 360; duration: 500
                                easing.type: Easing.OutCubic
                            }
                        }
                        Label { text: qsTr("波特率"); color: textSub; font.pixelSize: subSize }
                        ComboBox {
                            id: meterbaud
                            model: [2400,4800,9600,19200,28800,38400,56700,115200]
                            currentIndex: 2
                            Layout.preferredWidth: 120
                            enabled: !ins.isCalibrating
                            font.pixelSize: 18
                        }
                        Item { Layout.fillWidth: true } // 右侧弹簧
                    }
                }
            }
        }

        // ==========================================
        // 2. 中部：状态反馈与一键启动区 (重构成一体化高级控制条)
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            color: cardBg             // 整个控制条铺满白底
            radius: 8
            border.color: borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10   // 上下左右留出 10px 边距，内部按钮高度自动变为 60px
                anchors.leftMargin: 20
                spacing: 20

                // === 左侧：物理源状态监听 ===
                // 拆除了原有的嵌套 Rectangle，直接融在长条里
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 15

                    Rectangle {
                        width: 8; height: 24; radius: 4
                        color: ins.isCalibrating ? themeColor : textSub
                    }
                    // 完美保留您的 titleSize
                    Label { text: qsTr("标准源物理状态:"); font.bold: true; font.pixelSize: titleSize; color: textMain }
                    Label {
                        id: srctext
                        text: qsTr("Standby / 等待启动")
                        color: textSub
                        font.pixelSize: 18
                        font.bold: true
                        Layout.fillWidth: true // 自动吃掉中间多余的空白，把按钮挤到右边
                        elide: Text.ElideRight
                    }
                }

                // === 右侧：启动按钮 ===
                Button {
                    id: startCalibrateBtn
                    Layout.preferredWidth: 220
                    Layout.fillHeight: true // 自动填满 (80-20=60px)
                    enabled: !ins.isCalibrating

                    background: Rectangle {
                        color: parent.enabled ? (parent.pressed ? Qt.darker(themeColor, 1.1) : themeColor) : "#E0E0E0"
                        radius: 6 // 内部按钮圆角 6，配合外部的 8 视觉最协调
                    }
                    contentItem: Text {
                        text: qsTr("▶ 启动全自动校准")
                        font.bold: true
                        font.pixelSize: 20
                        color: parent.enabled ? "#FFFFFF" : "#999999"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        let meterConfigs = []
                        let validationPassed = true
                        let enabledCount = 0

                        let seenSns = []
                        let seenAddresses = []

                        for (let i = 0; i < meterRepeater.count; i++) {
                            let item = meterRepeater.itemAt(i)
                            item.resetAllSteps()

                            if (item.isEnabled) {
                                enabledCount++

                                if (item.meterSn === "") {
                                    topMsg.display("仪表 " + (i + 1) + " 编号不能为空！", "error")
                                    validationPassed = false
                                    break
                                }

                                if (seenSns.includes(item.meterSn)) {
                                    topMsg.display("仪表编号存在重复 (冲突编号: " + item.meterSn + ")", "error")
                                    validationPassed = false
                                    break
                                }
                                seenSns.push(item.meterSn)

                                if (seenAddresses.includes(item.meterAddr)) {
                                    topMsg.display("通信地址存在冲突 (冲突地址: " + item.meterAddr + ")", "error")
                                    validationPassed = false
                                    break
                                }
                                seenAddresses.push(item.meterAddr)

                                meterConfigs.push({
                                    "enabled": true,
                                    "address": item.meterAddr,
                                    "sn": item.meterSn
                                })
                            } else {
                                meterConfigs.push({
                                    "enabled": false,
                                    "address": item.meterAddr,
                                    "sn": ""
                                })
                            }
                        }

                        if (validationPassed) {
                            if (enabledCount === 0) {
                                topMsg.display("请至少勾选启用一台仪表！", "error")
                                return
                            }
                            ins.startCalibration(srcport.currentText, srcbaud.currentValue, meterport.currentText, meterbaud.currentValue, meterConfigs)
                            topMsg.display("全自动校准已启动", "success")
                        }
                    }
                }

                // === 右侧：停止按钮 ===
                Button {
                    Layout.preferredWidth: 160
                    Layout.fillHeight: true
                    enabled: ins.isCalibrating

                    background: Rectangle {
                        color: parent.enabled ? (parent.pressed ? Qt.darker(themeRed, 1.1) : themeRed) : "#E0E0E0"
                        radius: 6
                    }
                    contentItem: Text {
                        // 保留您修改的方块图标
                        text: qsTr("■ 停止校准")
                        font.bold: true
                        font.pixelSize: 20
                        color: parent.enabled ? "#FFFFFF" : "#999999"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        ins.stopCalibration()
                        // 完美保留您的霸气提示语
                        bottomStatusPanel.setStatus("操作员强行中止了校准序列！", "error")
                    }
                }
            }
        }

        // ==========================================
        // 5个仪表看板
        // ==========================================
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            Repeater {
                id: meterRepeater
                model: 5

                // 仪表卡片容器
                Rectangle {
                    id: meterCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: cardBg
                    radius: 8
                    // 极简浅灰细边
                    border.color: isEnabled ? "#DCDFE6" : borderColor
                    border.width: 1

                    // 顶部色彩强调线
                    Rectangle {
                        width: parent.width; height: 6
                        anchors.top: parent.top
                        color: isEnabled ? themeColor : "#E4E7ED"
                        radius: 8
                        Rectangle { width: parent.width; height: 3; anchors.bottom: parent.bottom; color: parent.color } // 下方拉直融入卡片
                    }

                    // 未启用时整体变半透明，突出启用的表
                    opacity: isEnabled ? 1.0 : 0.5
                    Behavior on opacity { NumberAnimation { duration: 200 } }

                    property bool isEnabled: enableCheck.checked
                    property int meterAddr: parseInt(addrInput.text) || 1
                    property string meterSn: sn.text.trim()

                    function updateStep(stepIdx, newStatus) {
                        stepModel.setProperty(stepIdx, "status", newStatus)
                    }
                    function resetAllSteps() {
                        for (let i = 0; i < stepModel.count; i++) {
                            stepModel.setProperty(i, "status", 0)
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.topMargin: 6 // 避开顶部高亮线
                        spacing: 0

                        //卡片独立表头
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: 12
                            Label {
                                text: qsTr("No." + (index + 1) + " 仪表")
                                font.bold: true; font.pixelSize: 18; color: isEnabled ? themeColor : textSub
                            }
                            Item { Layout.fillWidth: true }
                            CheckBox {
                                id: enableCheck
                                text: qsTr("启用") // 精简文字防止挤压
                                font.bold: true
                                font.pixelSize: 14
                                enabled: !ins.isCalibrating
                                checked: true
                            }
                        }

                        // 参数录入区
                        GridLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 15; Layout.rightMargin: 15; Layout.bottomMargin: 10
                            columns: 2
                            rowSpacing: 8; columnSpacing: 10

                            Label { text: "地址:"; color: textSub; font.pixelSize: 15 }
                            SpinBox {
                                id: addrInput
                                value: index + 1 // 默认地址 1, 2, 3, 4, 5
                                from: 1; to: 255
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38 // 统一高度压扁
                                enabled: enableCheck.checked && !ins.isCalibrating
                                editable: true
                                font.pixelSize: 18
                            }
                            Label { text: "SN码:"; color: textSub; font.pixelSize: 15 }
                            TextField {
                                id: sn
                                placeholderText: "出厂SN"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38 // 统一高度压扁
                                enabled: enableCheck.checked && !ins.isCalibrating
                                font.pixelSize: 18
                                horizontalAlignment: TextInput.AlignHCenter
                            }
                        }

                        // 分割线
                        Rectangle {
                            Layout.fillWidth: true; Layout.leftMargin: 15; Layout.rightMargin: 15
                            height: 1; color: borderColor
                        }

                        // 步骤流转看板
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.margins: 15
                            enabled: enableCheck.checked
                            clip: true
                            spacing: 10
                            interactive: false

                            model: ListModel {
                                id: stepModel
                                ListElement { stepName: "解除芯片保护"; status: 0 } // 精简文案
                                ListElement { stepName: "系统校准握手"; status: 0 }
                                ListElement { stepName: "电压电流校准 (PF=1.0)"; status: 0 }
                                ListElement { stepName: "电压电流校准 (PF=0.5)"; status: 0 }
                                ListElement { stepName: "E2PROM参数固化"; status: 0 }
                                ListElement { stepName: "等待复位"; status: 0 }
                            }

                            delegate: RowLayout {
                                width: ListView.view.width
                                spacing: 12

                                Rectangle {
                                    width: 20; height: 20; radius: 10 // 圆圈等比例缩小
                                    color: {
                                        if (status === 0) return "#E4E7ED"
                                        if (status === 1) return themeColor
                                        if (status === 2) return themeGreen
                                        if (status === 3) return themeRed
                                        return "#E4E7ED"
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        color: "white"
                                        font.bold: true; font.pixelSize: 12
                                        text: {
                                            if (status === 1) return "..."
                                            if (status === 2) return "✓"
                                            if (status === 3) return "✕"
                                            return ""
                                        }
                                    }
                                }

                                Label {
                                    text: stepName
                                    font.pixelSize: 14 // 字体微缩，防止挤压折行
                                    color: status === 1 ? themeColor : (status === 0 ? textSub : textMain)
                                    font.bold: status === 1 || status === 2
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
        Rectangle {
            id: bottomStatusPanel
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            color: cardBg
            radius: 8
            border.color: borderColor
            border.width: 1

            function setStatus(msg, type) {
                resultText.text = msg
                if (type === "success") {
                    resultText.color = themeGreen
                } else if (type === "error") {
                    resultText.color = themeRed
                } else {
                    resultText.color = themeColor
                }
            }

            // 1. 左侧指示器和标题，绝对靠左
            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 20
                spacing: 15

                Rectangle {
                    width: 6; height: 24; radius: 3
                    color: ins.isCalibrating ? themeColor : textSub
                    anchors.verticalCenter: parent.verticalCenter
                }

                Label {
                    text: qsTr("校准结果：")
                    font.bold: true
                    font.pixelSize: titleSize
                    color: textMain
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 2. 状态文本，无视其他控件，强行霸占整个卡片的绝对正中心！
            Label {
                id: resultText
                anchors.centerIn: parent
                text: qsTr("系统挂起，等待下发启动指令")
                font.pixelSize: 22
                font.bold: true
                color: textSub
            }

            Connections {
                target: ins
                function onShowTopMsg(msg, type) {
                    bottomStatusPanel.setStatus(msg, type)
                }

                function onIsCalibratingChanged() {
                    if (ins.isCalibrating) {
                        bottomStatusPanel.setStatus("🚀 高并发自动化流转执行中...", "info")
                    }
                }
            }
        }
    }
}