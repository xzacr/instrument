import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects // Qt 6 推荐的特效模块，用于实现高质量阴影

Item {
    id: parent_topmsg

    // 公开属性，方便外部调用
    property string text: "Notification text"
    property string type: "success" // 可选: success, info, error, warning

    width: 500
    height: 64
    z: 999

    // 样式配色字典
    readonly property var colors: {
        "success": { "bg": "#28B463", "shadow": "#28B463" },
        "info":    { "bg": "#2E86C1", "shadow": "#2E86C1" },
        "error":   { "bg": "#E74C3C", "shadow": "#E74C3C" },
        "warning": { "bg": "#F39C12", "shadow": "#F39C12" }
    }
    readonly property var currentStyle: colors[type] || colors["info"]

    // --- 1. 阴影层 (在 Qt 6 中 MultiEffect 建议与主体平级或包裹) ---
    MultiEffect {
        source: bgRect
        anchors.fill: bgRect
        shadowEnabled: true
        shadowColor: currentStyle.shadow
        shadowBlur: 1.0        // 阴影模糊扩散范围
        shadowOpacity: 0.4     // 阴影透明度，营造柔和感
        shadowVerticalOffset: 8 // 向下偏移，体现悬浮感
        blurEnabled: true      // 开启模糊使阴影更真实
    }

    // --- 2. 主体矩形 ---
    Rectangle {
        id: bgRect
        anchors.fill: parent
        radius: 12
        color: currentStyle.bg

        RowLayout {
            anchors.centerIn: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 15

            // 左侧图标容器 (空心圆)
            Rectangle {
                width: 30; height: 30
                radius: 15
                color: "transparent"
                border.color: "white"
                border.width: 2

                Text {
                    anchors.centerIn: parent
                    text: {
                        if (type === "success") return "✓"
                        if (type === "error")   return "!"
                        if (type === "warning") return "⚠"
                        return "i"
                    }
                    color: "white"
                    font.bold: true
                    font.pixelSize: 15
                }
            }

            // 中间提示文本
            Text {
                Layout.fillWidth: true
                text: parent_topmsg.text
                color: "white"
                font.pixelSize: 16
                font.weight: Font.Medium
            }

            // 右侧关闭按钮
            Text {
                text: "✕"
                color: "white"
                font.pixelSize: 18
                opacity: 0.6

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: parent.opacity = 1.0
                    onExited: parent.opacity = 0.6
                    onClicked: {
                        // 这里可以加一个退出动画再销毁
                        // 【核心修改】：不要 destroy，而是将状态设回初始值
                        // 这会触发你定义的 Transition 动画，让它滑回屏幕外
                        topMsg.state = ""

                        // 同时停止计时器，防止它在收回后再触发一次隐藏
                        hideTimer.stop()
                    }
                }
            }
        }
    }
}