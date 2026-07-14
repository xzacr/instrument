import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    // 🌟 1. 把尺寸显式赋给 Popup 本身，让引擎知道弹窗到底有多大！
    width: 700
    height: 600

    // 🌟 2. 剥离控件自带的任何隐形边距干扰
    padding: 0
    margins: 0

    // 🌟 3. 使用绝对坐标数学公式计算居中（比 anchors 更底层、更抗干扰！）
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    modal: true                       // 开启模态遮罩（点击背后无效）
    closePolicy: Popup.NoAutoClose    // 严禁点击空白处自动关闭，必须点按钮

    // 设为透明，因为背景和阴影由里面的 StatusCard 自己负责
    background: Item {}

    // --- 🌟 遮罩层样式 (背后的半透明黑底) ---
    Overlay.modal: Rectangle {
        color: "#80000000" // 50% 透明度的纯黑，凸显弹窗卡片
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    // --- 🌟 核心：内部嵌入我们画好的 StatusCard ---
    StatusCard {
        id: card
        anchors.fill: parent // 🌟 顺从地填满外层已经居中的 Popup

        // 当用户点击卡片底部的按钮时
        onButtonClicked: {
            root.close()
            // 这里可以再发个信号通知主界面做相应清理或重置
            // root.closedByUser()
        }
    }

    function show(title, msg, type = "success") {
        card.title = title
        card.message = msg
        card.type = type
        root.open()
    }
}