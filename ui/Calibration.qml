import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: calibrationPage
    //anchors.fill: parent

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
                        id: port
                        model: ins.rs232.portList
                        // 如果列表为空，显示提示
                        displayText: count > 0 ? currentText : "未发现串口"

                        // 第一次加载或显示时自动刷新一次
                        //Component.onCompleted: gateway.serial.refreshPorts()
                        Layout.preferredWidth: 150
                        enabled: !ins.rs232.isOpen
                        onActivated: {
                            console.log("标准源选择了",currentText)
                        }
                    }
                    ToolButton {
                        id: refreshBtn
                        enabled: !ins.rs232.isOpen
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
                            ins.rs232.refreshPorts()
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
                        id: baudrate
                        model: ["2400","4800","9600","19200","28800","38400","56700","115200"];
                        currentIndex: 5;
                        Layout.preferredWidth: 150
                    }
                    Button {
                        id: openbtn
                        text: ins.rs232.isOpen ? "关闭串口" : "打开串口"
                        highlighted: !ins.rs232.isOpen
                        Layout.preferredWidth: 130
                        Layout.preferredHeight: 60
                        onClicked: {
                            if(openbtn.text === "打开串口"){
                                console.log("232打开串口",port.currentText,"波特率",baudrate.currentText)
                                if(ins.rs232.openPort(port.currentText,baudrate.currentText) === true){
                                    topMsg.display("打开串口成功!","success")
                                }else{
                                    topMsg.display("打开串口失败!" + ins.rs232.lastError,"error")
                                }
                            }else{
                                console.log("232关闭串口")
                                if(ins.rs232.closePort() === true){
                                    topMsg.display("关闭串口成功!","success")
                                }else{
                                    topMsg.display("关闭串口失败!","error")
                                }
                            }
                        }
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
                        id: port1
                        model: ins.rs485.portList
                        // 如果列表为空，显示提示
                        displayText: count > 0 ? currentText : "未发现串口"

                        // 第一次加载或显示时自动刷新一次
                        //Component.onCompleted: gateway.serial.refreshPorts()
                        Layout.preferredWidth: 150
                        enabled: !ins.rs485.isOpen
                        onActivated: {
                            console.log("仪表选择了",currentText)
                        }
                    }
                    ToolButton {
                        id: refreshBtn1
                        enabled: !ins.rs485.isOpen
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
                            ins.rs485.refreshPorts()
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
                        id: baudrate1
                        model: ["2400","4800","9600","19200","28800","38400","56700","115200"];
                        currentIndex: 2
                        Layout.preferredWidth: 150
                    }
                    Button {
                        id: openbtn1
                        text: ins.rs485.isOpen ? "关闭串口" : "打开串口"
                        highlighted: !ins.rs485.isOpen
                        Layout.preferredWidth: 130
                        Layout.preferredHeight: 60
                        onClicked: {
                            if(openbtn1.text === "打开串口"){
                                console.log("485打开串口",port1.currentText,"波特率",baudrate1.currentText)
                                if(ins.rs485.openPort(port1.currentText,baudrate1.currentText) === true){
                                    topMsg.display("打开串口成功!","success")
                                }else{
                                    topMsg.display("打开串口失败!" + ins.rs485.lastError,"error")
                                }
                            }else{
                                console.log("485关闭串口")
                                if(ins.rs485.closePort() === true){
                                    topMsg.display("关闭串口成功!","success")
                                }else{
                                    topMsg.display("关闭串口失败!","error")
                                }
                            }
                        }
                    }

                    // 【内部弹簧】：同样把控件往左边推
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


        RowLayout {
            Layout.fillWidth: true
            spacing: 20

            Label {
                text: qsTr("标准源选择:")
                font.bold: true
            }

            RadioButton {
                text: "XL-3200"
                checked: true // 默认选中
            }

            RadioButton {
                text: "STR3060A"
            }

            Item {
                Layout.preferredWidth: 10
            }

            Button {
                id: startCalibrateBtn
                text: qsTr("▶ 启动全自动校准")
                font.bold: true
                highlighted: true
                Layout.preferredHeight: 60
                Material.background: Material.Teal
            }

            Button {
                text: qsTr("停止校准")
                font.bold: true
                highlighted: true
                Layout.preferredHeight: 60
                Material.background: Material.Red
            }

            Item { Layout.fillWidth: true } // 弹簧占位，把右边的状态顶过去

            RowLayout {
                Label { text: qsTr("标准源当前状态:"); font.pixelSize: 16 }
                Label {
                    text: qsTr("220V / 5A / PF=1.0")
                    color: Material.color(Material.Indigo)
                    font.pixelSize: 18
                    font.bold: true
                }
            }
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
                model: 5

                GroupBox {
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
                                checked: true // 默认全勾选，如果不勾选则跳过此表
                            }
                            Item { Layout.fillWidth: true }
                            Label { text: "地址:" ;font.bold: false}
                            SpinBox {
                                value: index + 1 // 默认地址 1, 2, 3, 4, 5
                                from: 1; to: 255
                                Layout.preferredWidth: 130
                                enabled: enableCheck.checked
                                editable: true
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
                            spacing: 8
                            interactive: false // 步骤不多不需要滚动

                            model: ListModel {
                                ListElement { stepName: "解除写保护"; status: 2 } // 模拟成功
                                ListElement { stepName: "校准准备"; status: 2 }   // 模拟成功
                                ListElement { stepName: "电压校准 (PF=1.0)"; status: 1 } // 模拟正在运行
                                ListElement { stepName: "电流校准 (PF=1.0)"; status: 0 } // 模拟等待
                                ListElement { stepName: "相位校准 (PF=0.5L)"; status: 0 }
                                ListElement { stepName: "参数固化保存"; status: 0 }
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
                                    font.pixelSize: 14
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
    }
}