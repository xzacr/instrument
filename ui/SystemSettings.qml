import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore // 🌟 Qt 6 官方标准导入

Item {
    id: settingsPage

    property color themeColor: "#1976D2"
    property color textMain: "#333333"
    property color textSub: "#606266"

    // 🌟 核心对外暴露的属性：标准源误差补偿值（0.0 或 0.1）
    // 默认初始设为 0.1
    property real sourceErrorOffset: 0.0

    Settings {
        id: sysSettings
        location: "file:///" +appDirPath + "/506787841.ini"
        category: "SysConfig"

        property int sourceErrorOffsetIndex: 0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        // 顶部标题
        Label {
            text: "系统参数设置"
            font.pixelSize: 24
            font.bold: true
            color: textMain
        }

        // ==========================================
        // 设置卡片：标准源误差补偿
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            color: "#FFFFFF"
            border.color: "#C0C0C0"
            border.width: 1
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15

                Label {
                    text: "标准源精度补偿配置"
                    font.pixelSize: 18
                    font.bold: true
                    color: themeColor
                }

                RowLayout {
                    spacing: 15

                    Label {
                        text: "标准源系统误差："
                        font.pixelSize: 16
                        font.bold: true
                        color: textMain
                    }

                    // 🌟 核心下拉框
                    ComboBox {
                        id: errorOffsetCombo
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 40

                        // 下拉选项
                        model: [0.0, 0.1, 0.2, 1.0]

                        // 默认选中第2项 (即 0.1%)
                        currentIndex: sysSettings["sourceErrorOffsetIndex"]
                        font.pixelSize: 16
                        font.bold: true

                        // 选中变化时触发联动
                        onCurrentIndexChanged: {
                            // 0. 安全防呆：在组件初始化或重置时，currentIndex 可能短暂为 -1。
                            // 直接跳过，防止数组越界 model[-1] 拿到 undefined 导致崩掉 double 类型！
                            if (currentIndex < 0 || currentIndex >= model.length) return;

                            // 1. 直接用数组下标读取真正的浮点数字，最为可靠！
                            let val = model[currentIndex];

                            sysSettings["sourceErrorOffsetIndex"] = currentIndex

                            // 1. 更新本页面的全局属性，供其他 QML 页面获取
                            settingsPage.sourceErrorOffset = val;

                            // 2. 如果 C++ 底层提供了设置接口，立刻同步给 C++！
                            if (typeof ins !== "undefined" && ins.setSourceErrorOffset) {
                                ins.setSourceErrorOffset(val);
                            }

                            console.log(">>> [系统设置] 标准源误差补偿已更新为:", val, "%");

                            if (typeof errorPage !== "undefined" && errorPage.refreshHeaders) {
                                errorPage.refreshHeaders();
                                console.log(">>> 已远程触发 [误差计算] 页面表头文字刷新！");
                            }
                        }
                    }

                    // Label {
                    //     text: "* 提示：切换此项将立即联动 C++ 判定限值与各测试页面的大纲误差显示"
                    //     font.pixelSize: 14
                    //     color: "#909399"
                    //     Layout.leftMargin: 10
                    // }
                }
            }
        }

        // 底部占位符，把上方的卡片自然顶在上面，不会被拉伸
        Item {
            Layout.fillHeight: true
        }
    }
}