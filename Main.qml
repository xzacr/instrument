import QtQuick
import QtQuick.Controls
//import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    width: 1200
    height: 800
    visible: true
    title: qsTr("Hello World")

    ColumnLayout{
        ComboBox{
            model: ["asdf","123"]
        }
        Label{
            text: "hhhhhhh"
        }
        CheckBox{

        }
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
}
