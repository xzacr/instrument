import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Item {
    id: root

    property string type: "success" // 可选: "success" (绿色) 或 "error" (红色)
    property string title: "测试全部通过"
    property string message: "所有数据已校验完毕!"
    readonly property string buttonText: type === "success" ? "完成" : "我知道了"

    // --- 🌟 点击信号 ---
    signal buttonClicked()

    // 默认卡片尺寸 (近似于 3:4 的黄金分割比)
    width: 700
    height: 600

    // --- 🎨 主题配色字典 (精准还原图中的高级配色) ---
    readonly property color primaryColor: type === "success" ? "#358E58" : "#D0483E"
    readonly property color lightColor:   type === "success" ? "#E8F4EC" : "#FCEEEB"
    readonly property color shadowColor:  type === "success" ? "#1A502E" : "#7A201A"

    // --- 1. 全局卡片外阴影 ---
    MultiEffect {
        source: cardContainer
        anchors.fill: cardContainer
        shadowEnabled: true
        shadowColor: "#000000"
        shadowBlur: 1.5
        shadowOpacity: 0.35
        shadowVerticalOffset: 10
    }

    // --- 2. 卡片主容器 ---
    Item {
        id: cardContainer
        anchors.fill: parent

        // 顶层深色背景 (上半部分)
        Rectangle {
            id: topHeader
            width: parent.width
            height: parent.height * 0.48 // 占据上方约 48% 的高度
            radius: 10                   // 全局卡片大圆角
            color: root.primaryColor

            // 🌟 使用纯 QML Canvas 绘制高比格的极简白色脸部图标！不需要任何外部图片！
            Canvas {
                id: faceCanvas
                width: 86; height: 86
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -10 // 略微偏上，给下方留出重叠空间

                // 当类型切换时，重新绘制图标
                Connections {
                    target: root
                    function onTypeChanged() { faceCanvas.requestPaint(); }
                }

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = "white";
                    ctx.fillStyle = "white";
                    ctx.lineWidth = 4.5;
                    ctx.lineCap = "round";

                    // 1. 画外圆圈
                    ctx.beginPath();
                    ctx.arc(width/2, height/2, width/2 - 5, 0, Math.PI * 2);
                    ctx.stroke();

                    // 2. 画两只眼睛
                    ctx.beginPath();
                    ctx.arc(width/2 - 14, height/2 - 10, 3.5, 0, Math.PI * 2);
                    ctx.arc(width/2 + 14, height/2 - 10, 3.5, 0, Math.PI * 2);
                    ctx.fill();

                    // 3. 画嘴巴弧线
                    ctx.beginPath();
                    if (root.type === "success") {
                        // 微笑弧线
                        ctx.arc(width/2, height/2 + 2, 16, 0.15 * Math.PI, 0.85 * Math.PI, false);
                    } else {
                        // 难过哭哭弧线
                        ctx.arc(width/2, height/2 + 22, 16, 1.15 * Math.PI, 1.85 * Math.PI, false);
                    }
                    ctx.stroke();
                }
            }
        }

        // 底层浅色背景 (下半部分)
        Rectangle {
            id: bottomBody
            width: parent.width
            // 🌟 核心精髓：让 Y 坐标上移，盖住上半部的一角，利用 radius 做出高级重叠叠层！
            y: parent.height * 0.42
            height: parent.height - y
            radius: 10
            color: root.lightColor

            // --- 文本与按钮布局 ---
            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 25
                anchors.bottomMargin: 30
                anchors.leftMargin: 25
                anchors.rightMargin: 25
                spacing: 16

                // 大标题
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.title
                    color: root.primaryColor
                    font.pixelSize: 32
                    font.bold: true
                }

                // 描述小字
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    text: root.message
                    color: "#4A5055" // 极高级的深灰冷色
                    font.pixelSize: 22
                    font.weight: Font.Medium // 加上中等字重（介于普通和加粗之间）
                    lineHeight: 1.4
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                // 弹性空白，把按钮挤到最底部
                Item { Layout.fillHeight: true }

                // --- 胶囊按钮 (带独立下沉阴影) ---
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 180
                    Layout.preferredHeight: 52

                    // 按钮投影
                    MultiEffect {
                        source: btnRect
                        anchors.fill: btnRect
                        shadowEnabled: true
                        shadowColor: root.shadowColor
                        shadowBlur: 1.0
                        shadowOpacity: 0.45
                        shadowVerticalOffset: 5
                    }

                    // 按钮本体
                    Rectangle {
                        id: btnRect
                        anchors.fill: parent
                        radius: height / 2 // 胶囊圆角 (Pill Shape)
                        color: btnMouse.pressed
                               ? Qt.darker(root.primaryColor, 1.1)
                               : (btnMouse.containsMouse ? Qt.lighter(root.primaryColor, 1.08) : root.primaryColor)

                        Behavior on color { ColorAnimation { duration: 150 } }

                        Text {
                            anchors.centerIn: parent
                            text: root.buttonText
                            color: "white"
                            font.pixelSize: 16
                            font.bold: true
                            font.letterSpacing: 1.5 // 稍微拉开字间距更显精致
                        }

                        MouseArea {
                            id: btnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.buttonClicked()
                        }
                    }
                }
            }
        }
    }
}