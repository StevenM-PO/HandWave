import QtQuick
import QtQuick.Controls.Basic

// Large touch-friendly button. `checked` only changes how it looks: buttons
// that represent state bind it, and toggle the state in onClicked, so the
// binding survives when the state is changed from elsewhere (encoders).
Button {
    id: pad
    implicitHeight: 48
    implicitWidth: 76
    font.pixelSize: Theme.fontSize
    contentItem: Text {
        text: pad.text
        font: pad.font
        color: pad.checked ? Theme.background : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 8
        color: pad.checked ? Theme.accent : (pad.down ? Qt.lighter(Theme.control, 1.4) : Theme.control)
    }
}
