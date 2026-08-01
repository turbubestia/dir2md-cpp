import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 400
    height: 300
    visible: true
    title: qsTr("Counter Demo")

    property int counter_value: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 20

        Text {
            text: root.counter_value.toString()
            font.pixelSize: 48
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
        }

        Button {
            text: qsTr("Increment")
            Layout.alignment: Qt.AlignHCenter
            onClicked: {
                root.counter_value += 1
            }
        }
    }
}
