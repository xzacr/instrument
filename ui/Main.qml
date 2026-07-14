import QtQuick
import QtQuick.Controls
//import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    width: 1366
    height: 768
    // 【修改点 2】：直接设置窗口可见性为最大化
    visibility: Window.Maximized

    // ==========================================================
    // 补充知识：如果你想做绝对的“全屏”（连 Windows 底部任务栏都盖住），用这个：
    // visibility: Window.FullScreen
    // ==========================================================
    visible: true
    title: "多功能仪表自动校准系统"

    header: Header{
        id: tabBar
    }

    StackLayout {
        visible: true
        anchors.fill: parent
        anchors.margins: 16
        currentIndex: tabBar.currentIndex

        // ------------------------------------------
        // 页面 1：仪表校准界面
        // ------------------------------------------
        Calibration{
            id: calibPage
        }

        // ------------------------------------------
        // 页面 2：误差计算界面 (占位预留)
        // ------------------------------------------
        ErrorCalc{
            id: errorPage
        }
        // ErrorCalcExample{
        //     id: errorPageex
        // }
    }

    TopMessage {
        id: topMsg
        x: (parent.width - width) / 2
        y: -height // 初始位置在屏幕外
        visible: false
        z: 9999

        states: State {
            name: "show"
            PropertyChanges { target: topMsg; y: 120; visible: true }
        }

        transitions: Transition {
            NumberAnimation {
                properties: "y,opacity";
                duration: 400;
                easing.type: Easing.OutCubic
            }
        }

        Timer {
            id: hideTimer
            interval: 2000
            onTriggered: topMsg.state = ""
        }

        function display(msg, msgType = "success") {
            text = msg
            type = msgType
            state = "show"
            hideTimer.restart()
            console.log("topMsg.display msg =",msg,"msgType =",msgType)
        }
    }


    ResultPopup {
        id: resultPopup
    }
    Connections {
        target: ins // 替换为您 C++ 后台线程在 QML 中的实例化上下文名称

        function onShowResultPopup(title, msg, type) {
            resultPopup.show(title, msg, type)
        }
    }
    // FatalErrorPopup {
    //     id: fatalErrorPopup
    //     Component.onCompleted: {
    //         fire("标准源故障!")
    //     }
    // }
}
// Button{
//     width: 300
//     height: 300
//     z: 9999
//     highlighted: true
//     onClicked: {
//         meter.triggerCpuCrash()
//     }
// }