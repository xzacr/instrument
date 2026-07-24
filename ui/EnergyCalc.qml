import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Item {
    id: energyCalcPage

    property color themeColor: "#1976D2"
    property color textMain: "#333333"
    property color textSub: "#606266"
    readonly property int mode_EnergyCalc: 2 // 对应C++的电能模式枚举(若不是2请自行调整)

    property int selectedMeterIndex: 0
    property int selectedCategory: 0

    // 接收底层推送的真实数据源：5台仪表，每台2个分类(0:有功, 1:无功)
    property var meterDataStore: []
    property var currentTableModel: [] // 绑定给 ListView 的当前视图数据

    // 🌟 1. 恢复原版分类写法：生成「有功电能」和「无功电能」两个标准切换按钮！
    property var categories: [
        "有功电能", "无功电能"
    ]

    property int failTrigger: 0

    function hasCategoryFail(catIdx) {
        let dummy = failTrigger; // 绑定信号灯，确保数据一来立马重新计算！
        if (!meterDataStore || meterDataStore.length === 0) return false;
        let rows = meterDataStore[selectedMeterIndex][catIdx];
        // 只要该分类下任意一行的任意一格 isFail 为 true，就返回 true
        return rows ? rows.some(r => r.cells.some(c => c.isFail)) : false;
    }
    // =====================================================================
    // 🌟 2. 表头列名映射：两个分类都严格只需要 3 列！
    // =====================================================================
    function getColumns(cat) {
        return ["标准电能", "实测电能", "误差"]
    }

    // =====================================================================
    // 🌟 3. 测试工况大纲表头 (严格按截图及：电压->PF->电流从小到大 排序)
    // =====================================================================
    function getRows(cat) {
        // --- 0: 有功电能 (17个工况) ---
        if (cat === 0) {
            return [
                // 第一组：PF=1.0
                "220V, PF=1.0, 0.05A",
                "220V, PF=1.0, 0.20A",
                "220V, PF=1.0, 0.25A",
                "220V, PF=1.0, 5.00A",
                "220V, PF=1.0, 6.00A",
                // 第二组：PF=0.5L
                "220V, PF=0.5L, 0.10A",
                "220V, PF=0.5L, 0.25A",
                "220V, PF=0.5L, 0.40A",
                "220V, PF=0.5L, 0.50A",
                "220V, PF=0.5L, 5.00A",
                "220V, PF=0.5L, 6.00A",
                // 第三组：PF=0.8C
                "220V, PF=0.8C, 0.10A",
                "220V, PF=0.8C, 0.25A",
                "220V, PF=0.8C, 0.40A",
                "220V, PF=0.8C, 0.50A",
                "220V, PF=0.8C, 5.00A",
                "220V, PF=0.8C, 6.00A"
            ];
        }
        // --- 1: 无功电能 (13个工况，按新截图一字不差录入) ---
        if (cat === 1) {
            return [
                // 第一组：PF=0
                "220V, PF=0, 0.10A",
                "220V, PF=0, 0.20A",
                "220V, PF=0, 0.25A",
                "220V, PF=0, 5.00A",
                "220V, PF=0, 6.00A",
                // 第二组：PF=0.866
                "220V, PF=0.866, 0.25A",
                "220V, PF=0.866, 0.40A",
                "220V, PF=0.866, 0.50A",
                "220V, PF=0.866, 5.00A",
                "220V, PF=0.866, 6.00A",
                // 第三组：PF=0.968246
                "220V, PF=0.968246, 0.50A",
                "220V, PF=0.968246, 5.00A",
                "220V, PF=0.968246, 6.00A"
            ];
        }
        return [];
    }

    // =====================================================================
    // 🌟 4. 全量预加载大纲（默认赋 '-'，自动遍历两个分类）
    // =====================================================================
    function clearAllData() {
        let fresh = [];
        for (let i = 0; i < 5; i++) {
            let cats = [];
            for (let c = 0; c < categories.length; c++) {
                let catRows = [];
                let rowNames = getRows(c);
                let colCount = getColumns(c).length;

                for (let r = 0; r < rowNames.length; r++) {
                    let emptyCells = [];
                    for (let col = 0; col < colCount; col++) {
                        emptyCells.push({ "errStr": "-", "isFail": false });
                    }
                    catRows.push({
                        "header": rowNames[r],
                        "cells": emptyCells
                    });
                }
                cats.push(catRows);
            }
            fresh.push(cats);

            // 重置顶部卡片状态
            meterStateModel.setProperty(i, "status", "IDLE");
            meterStateModel.setProperty(i, "desc", "等待测试");
        }
        meterDataStore = fresh;
        updateView();
    }

    // 触发 ListView 刷新
    function updateView() {
        if (meterDataStore.length > 0 && meterDataStore[selectedMeterIndex]) {
            currentTableModel = [].concat(meterDataStore[selectedMeterIndex][selectedCategory]);
        }
    }

    onSelectedMeterIndexChanged: updateView()
    onSelectedCategoryChanged: updateView()
    Component.onCompleted: clearAllData()

    // 🌟 全局信号监听中枢
    Connections {
        target: ins

        function onIsCalibratingChanged() {
            if (ins.isCalibrating) {
                clearAllData();
            }
        }

        function onUpdateErrorMeterStatus(page,meterIndex, statusEnum, desc) {
            if(page !== mode_EnergyCalc) return;
            let statusStr = "IDLE";
            if (statusEnum === 0) statusStr = "IDLE";
            else if (statusEnum === 1) statusStr = "PASS";
            else if (statusEnum === 2) statusStr = "FAIL";
            else if (statusEnum === 3) statusStr = "RUNNING";
            else if (statusEnum === 4) statusStr = "TIMEOUT";
            else if (statusEnum === 4) statusStr = "STOP";

            meterStateModel.setProperty(meterIndex, "status", statusStr);
            meterStateModel.setProperty(meterIndex, "desc", desc);
        }

        // =====================================================================
        // 🌟 核心升级：智能分流！C++传 categoryIndex=0 归入有功，传 1 归入无功！
        // =====================================================================
        function onAppendErrorRow(page,meterIndex, categoryIndex, rowName, rowCells) {
            if(page !== mode_EnergyCalc) return;
            let data = meterDataStore;

            // 确保安全越界保护：如果后台乱传分类号，默认塞到第0类(有功)里
            let targetCat = (categoryIndex >= 0 && categoryIndex < categories.length) ? categoryIndex : 0;
            let catRows = data[meterIndex][targetCat];
            let foundIndex = -1;

            for (let r = 0; r < catRows.length; r++) {
                if (catRows[r].header === rowName) {
                    catRows[r].cells = rowCells;
                    foundIndex = r;
                    break;
                }
            }

            if (foundIndex === -1) {
                catRows.push({ "header": rowName, "cells": rowCells });
                foundIndex = catRows.length - 1;
            }

            meterDataStore = data;
            failTrigger++;

            // 1. 判断操作员现在是否正停留在表格“最底部” (留出 10 像素的容差)
            let isAtBottom = (resultListView.contentY + resultListView.height >= resultListView.contentHeight - 10);

            // 2. 记住当前位置
            let savedContentY = resultListView.contentY;

            if (meterIndex === selectedMeterIndex) {
                // 1. 智能对偶归并：防止成对数据导致标签前后疯狂闪跳
                let targetCategory = categoryIndex;

                // 2. 如果正在测试的分类和当前看的不一样，自动把标签切过去！
                if (selectedCategory !== targetCategory) {
                    //selectedCategory = targetCategory; // 这一步会自动触发 QML 视图重绘
                } else {
                    updateView(); // 标签没变，正常手动刷新当前表格的值
                }

                // // 3. 延迟一帧让视图将新选中的标签表格渲染完毕，再精准对准到中间！
                // Qt.callLater(function() {
                //     resultListView.positionViewAtIndex(foundIndex, ListView.Center);
                // });
            }
            // 4. 智能分支判断
            Qt.callLater(function() {
                if (!isAtBottom) {
                    // 🛡️【历史查阅模式】：如果用户刚才在翻看顶部或中间的旧数据，
                    // 强行锁死视窗，死记当前位置，绝对不跳动！
                    resultListView.contentY = savedContentY;
                } else {
                    // 🚀【实时监控模式】：如果用户刚才本来就在盯着最底部，
                    // 那新出一条数据，就顺滑地自动帮他滚到最后一行！
                    resultListView.positionViewAtEnd();
                    // 如果是 TableView，可以用：
                }
            });
        }
    }

    // 动态卡片模型
    ListModel {
        id: meterStateModel
        ListElement { title: "1#仪表"; status: "IDLE"; desc: "等待测试" }
        ListElement { title: "2#仪表"; status: "IDLE"; desc: "等待测试" }
        ListElement { title: "3#仪表"; status: "IDLE"; desc: "等待测试" }
        ListElement { title: "4#仪表"; status: "IDLE"; desc: "等待测试" }
        ListElement { title: "5#仪表"; status: "IDLE"; desc: "等待测试" }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 15

        // ==========================================
        // 顶部：带有悬浮放大的仪表看板 (100% 原版代码)
        // ==========================================
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 180
            Layout.maximumHeight: 180
            spacing: 20

            Repeater {
                model: meterStateModel
                delegate: Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    property color bgColor: status === "PASS" ? "#4CAF50" :
                                            status === "FAIL" ? "#F44336" :
                                            status === "TIMEOUT" ? "#F57C00" :
                                            status === "STOP" ? "#F57C00" :
                                            status === "RUNNING" ? "#2196F3" : "#9E9E9E"

                    property bool isSelected: selectedMeterIndex === index

                    MouseArea {
                        id: cardMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: selectedMeterIndex = index
                    }

                    property real currentMargin: isSelected ? 4 : (cardMouseArea.containsMouse ? 8 : 12)
                    Behavior on currentMargin { NumberAnimation { duration: 250; easing.type: Easing.OutBack } }

                    Item {
                        id: shapeSource
                        anchors.fill: parent
                        anchors.margins: currentMargin
                        visible: false

                        Rectangle {
                            id: mainShadowShape
                            anchors.fill: parent
                            anchors.bottomMargin: 14
                            radius: 8
                            color: bgColor
                        }

                        Rectangle {
                            visible: isSelected
                            width: 20; height: 20
                            rotation: 45
                            color: bgColor
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: mainShadowShape.bottom
                        }
                    }

                    MultiEffect {
                        source: shapeSource
                        anchors.fill: shapeSource
                        shadowEnabled: true
                        shadowColor: bgColor
                        shadowBlur: 1.0
                        shadowOpacity: 0.6
                        shadowVerticalOffset:  8
                        Behavior on shadowOpacity { NumberAnimation { duration: 250 } }
                        Behavior on shadowVerticalOffset { NumberAnimation { duration: 250 } }
                        Behavior on shadowColor { ColorAnimation { duration: 250 } }
                    }

                    Item {
                        anchors.fill: parent
                        anchors.margins: currentMargin

                        Rectangle {
                            visible: isSelected
                            width: 20; height: 20
                            rotation: 45
                            color: bgColor
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: mainVisCard.bottom
                        }

                        Rectangle {
                            id: mainVisCard
                            anchors.fill: parent
                            anchors.bottomMargin: 14
                            radius: 8
                            color: bgColor
                            border.color: isSelected ? Qt.lighter(bgColor, 1.2) : "transparent"
                            border.width: isSelected ? 1 : 0
                            clip: true

                            Rectangle {
                                width: parent.width
                                height: 35
                                color: Qt.darker(bgColor, 1.15)
                                radius: 8
                                Rectangle {
                                    width: parent.width; height: 8;
                                    anchors.bottom: parent.bottom; color: parent.color
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    Label {
                                        text: title
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 16
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        id: stateIcon
                                        text: status === "FAIL" ? "❗" :
                                              status === "TIMEOUT" ? "⚠" :
                                              status === "STOP" ? "⚠" :
                                              status === "PASS" ? "✔" :
                                              status === "RUNNING" ? "⚙" : "○"
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 16
                                        RotationAnimation {
                                            target: stateIcon
                                            loops: Animation.Infinite
                                            from: 0; to: 360
                                            duration: 1500
                                            running: status === "RUNNING"
                                            onRunningChanged: {
                                                if (!running) {
                                                    stateIcon.rotation = 0
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 0
                                Label {
                                    text: status
                                    color: "white"
                                    font.pixelSize: 42
                                    font.bold: true
                                    font.letterSpacing: 2
                                    Layout.alignment: Qt.AlignCenter
                                }
                                Label {
                                    text: status === "PASS" ? "合格" :
                                          status === "FAIL" ? "不合格" :
                                          status === "TIMEOUT" ? "已掉线" :
                                          status === "STOP" ? "已停止" :
                                          status === "IDLE" ? "未启用" : "正在测试"
                                    color: "white"
                                    font.pixelSize: 18
                                    font.bold: true
                                    Layout.alignment: Qt.AlignCenter
                                }
                            }

                            Label {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottomMargin: 10
                                text: desc
                                color: "white"
                                font.pixelSize: 14
                                horizontalAlignment: Text.AlignHCenter
                                opacity: 0.9
                            }
                        }
                    }
                }
            }
        }

        // ==========================================
        // 中层：大尺寸导航按钮 (原汁原味的 100% 还原)
        // ==========================================
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Repeater {
                model: categories
                delegate: Button {
                    property bool isCatFail: hasCategoryFail(index)

                    text: modelData
                    // 🌟 1. 字体加大！选中时 20px 粗体，未选中 18px 粗体，绝对清晰大方！
                    font.pixelSize: selectedCategory === index ? 20 : 18
                    font.bold: true

                    // 🌟 2. 按钮稍微加高一点到 54px，跟大的字号更搭
                    Layout.preferredHeight: 54
                    Layout.preferredWidth: implicitWidth + 40 // 左右多留点空白，更大气

                    background: Rectangle {
                        radius: 6

                        // 🌟 3. 核心修复：未选中时改用“纯白 #FFFFFF”！
                        // 这样在灰色的应用底色下，每一个未选中的按钮都是一个立体的白色方块！
                        color: {
                            if (selectedCategory === index) return isCatFail ? "#D32F2F" : themeColor;
                            return isCatFail ? "#FFEBEE" : "#FFFFFF"; // <--- 看这里，从 #F0F2F5 改成了 #FFFFFF
                        }

                        // 🌟 4. 加上精致边框：选中时无边框；没选中时用 #DCDFE6 勾边
                        border.color: {
                            if (selectedCategory === index) return "transparent";
                            return isCatFail ? "#F44336" : "#DCDFE6";
                        }
                        border.width: selectedCategory === index ? 0 : 1

                        // 鼠标悬浮时的反馈动画
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }

                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        // 选中时白字；没选中且报错是红字；没选中正常是深黑字 (#303133)
                        color: {
                            if (selectedCategory === index) return "#FFFFFF";
                            return isCatFail ? "#D32F2F" : "#303133";
                        }
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter

                        Behavior on color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: selectedCategory = index
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                id: energyTestBtn
                Layout.preferredHeight: 50
                Layout.preferredWidth: 160
                property bool isRunning: ins ? ins.isCalibrating : false

                background: Rectangle {
                    color: parent.pressed ? Qt.darker((energyTestBtn.isRunning ? "#F44336" : themeColor), 1.1) : (energyTestBtn.isRunning ? "#F44336" : themeColor)
                    radius: 6
                }
                contentItem: Text {
                    text: energyTestBtn.isRunning ? "停止测试" : "开始测试"
                    font.bold: true
                    font.pixelSize: 18
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    if (isRunning) {
                        if (typeof ins !== "undefined") {
                            loadingPopup.show("正在停止...");
                            ins.stopCalibration();
                        }
                    } else {
                        let config = calibPage.validateAndGetConfig();
                        if (config) {
                            ins.startTask(
                                mode_EnergyCalc,
                                config.srcPort,
                                config.srcBaud,
                                config.meterPort,
                                config.meterBaud,
                                config.meters
                            );
                            console.log("触发开始电能走字测试！")
                        }
                    }
                }
            }
        }

        // ==========================================
        // 底层：动态表头 + 真实大纲渲染区 (100% 原版代码)
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "white"
            border.color: "#C0C0C0"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // === 动态表头 ===
                RowLayout {
                    Layout.fillWidth: true
                    height: 45
                    spacing: 0

                    Rectangle {
                        width: 240; height: parent.height
                        color: "#F5F7FA"; border.color: "#C0C0C0"; border.width: 1
                        Label {
                            anchors.centerIn: parent
                            text: "测试条件"
                            font.bold: true; font.pixelSize: 16; color: textMain
                        }
                    }
                    Repeater {
                        model: getColumns(selectedCategory)
                        delegate: Rectangle {
                            Layout.fillWidth: true; height: parent.height
                            color: "#F5F7FA"; border.color: "#C0C0C0"; border.width: 1
                            Label {
                                anchors.centerIn: parent
                                text: modelData
                                font.bold: true; font.pixelSize: 16; color: textMain
                            }
                        }
                    }
                }

                // === 真实数据渲染区 ===
                ListView {
                    id: resultListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    interactive: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: currentTableModel

                    ScrollBar.vertical: ScrollBar {
                        active: true; policy: ScrollBar.AlwaysOn
                    }

                    delegate: RowLayout {
                        width: ListView.view.width
                        height: 40
                        spacing: 0

                        property var rowData: modelData
                        property int rowIdx: index
                        property bool isSelected: resultListView.currentIndex === rowIdx

                        HoverHandler { id: rowHover }

                        TapHandler {
                            onTapped: {
                                resultListView.currentIndex = rowIdx
                            }
                        }

                        property color rowBgColor: {
                            if (isSelected) return "#CCE8FF"
                            if (rowHover.hovered) return "#E6F7FF"
                            return rowIdx % 2 === 0 ? "#FFFFFF" : "#FAFAFA"
                        }

                        // 测试条件列
                        Rectangle {
                            width: 240; height: parent.height
                            color: rowBgColor; border.color: "#E0E0E0"; border.width: 1
                            Label {
                                anchors.centerIn: parent
                                text: rowData.header
                                font.bold: true; font.pixelSize: 14; color: textMain
                            }
                        }

                        // 测量结果单元格
                        Repeater {
                            model: rowData.cells
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                height: parent.height
                                color: rowBgColor
                                border.color: "#E0E0E0"
                                border.width: 1

                                Label {
                                    anchors.centerIn: parent
                                    text: modelData.errStr !== undefined ? modelData.errStr : "-"
                                    font.bold: true
                                    font.pixelSize: 16
                                    color: {
                                        if (modelData.errStr === "-" || modelData.errStr === undefined) return "#909399";
                                        return modelData.isFail ? "#F44336" : "#388E3C";
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}