import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: errorCalcPage

    Label {
        anchors.centerIn: parent
        text: qsTr("误差计算界面 (开发中...)")
        font.pixelSize: 24
        color: "gray"
    }
}
