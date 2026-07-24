import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: loadingRoot

    // 加载弹窗通常不需要太大，400x220 正好合适
    width: 400
    height: 220

    padding: 0
    margins: 0

    // 完美的居中算法
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2

    modal: true                     // 开启模态遮罩
    closePolicy: Popup.NoAutoClose    // 禁止点空白处关闭

    // 透明底，由内部的圆角矩形接管外观
    background: Item {}

    // 半透明背景遮罩
    Overlay.modal: Rectangle {
        color: "#80000000"
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    // --- 🌟 替换掉 StatusCard，换成专属的加载卡片 ---
    Rectangle {
        anchors.fill: parent
        color: "#FFFFFF"          // 卡片背景色（如果你用暗色主题可以改成 "#2D3035"）
        radius: 12                // 圆角
        border.color: "#E0E0E0"
        border.width: 1

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 20

            // Qt 官方自带的炫酷转圈圈控件
            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 50
                Layout.preferredHeight: 50
                running: loadingRoot.visible // 弹窗可见时自动开始转圈
            }

            // 动态提示文本
            Text {
                id: messageText
                Layout.alignment: Qt.AlignHCenter
                text: "正在停止测试，请稍候..."
                font.pixelSize: 15
                font.bold: true
                color: "#333333"          // 暗色主题下可改为 "#FFFFFF"
            }
        }
    }

    // 外部调用的公共方法
    function show(msg) {
        if (msg) messageText.text = msg
        loadingRoot.open()
    }

    function hide() {
        loadingRoot.close()
    }
}