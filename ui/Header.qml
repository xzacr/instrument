import QtQuick
import QtQuick.Controls

TabBar {
    id: tabBar
    width: parent.width
    TabButton { text: qsTr("仪表校准") }
    TabButton { text: qsTr("误差计算") }
    TabButton { text: qsTr("电能计算") }
}
