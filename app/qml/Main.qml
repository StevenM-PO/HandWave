import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HandWave

// Sized for the 800x480 touchscreen. On the Pi (eglfs, no window system) it
// fills the screen; on the desktop it opens as a normal window.
ApplicationWindow {
    id: window
    width: 800
    height: 480
    visible: true
    visibility: Qt.platform.pluginName === "eglfs" ? Window.FullScreen : Window.Windowed
    title: "HandWave"
    color: Theme.background

    SynthController {
        id: synth
    }

    function currentPage() {
        return pages.children[pages.currentIndex]
    }

    // ---- Hardware controls (keyboard-simulated for now, GPIO later) ----
    // Encoders go to the current page's control map. Switches are the same
    // everywhere except 2, which the page decides:
    //   1 = Play (held = note on)   3 = next page   4 = Hold (latch)
    ControlSurface {
        id: controls
        onEncoderTurned: (encoder, steps) => window.currentPage().encoderTurned(encoder, steps)
        onEncoderPressed: (encoder) => window.currentPage().encoderPressed(encoder)
        onSwitchPressed: (sw) => {
            switch (sw) {
            case 1: synth.playing = true; break
            case 3: pages.currentIndex = (pages.currentIndex + 1) % pages.count; break
            case 4: synth.holding = !synth.holding; break
            default: window.currentPage().switchPressed(sw)
            }
        }
        onSwitchReleased: (sw) => {
            if (sw === 1)
                synth.playing = false
        }
    }

    StackLayout {
        id: pages
        anchors.fill: parent
        anchors.margins: 12

        OscillatorPage { synth: synth }
        FilterPage { synth: synth }
        EnvelopePage { synth: synth; envelope: synth.ampEnvelope; title: "AMP ENV" }
        EnvelopePage { synth: synth; envelope: synth.filterEnvelope; title: "FILTER ENV" }
    }

    // Page indicator: a small title and one dot per page, top-right corner.
    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 16
        anchors.rightMargin: 20
        spacing: 6

        Text {
            text: pages.children[pages.currentIndex].title
            color: Theme.dimText
            font.pixelSize: Theme.smallFontSize
            anchors.verticalCenter: parent.verticalCenter
            rightPadding: 4
        }
        Repeater {
            model: pages.count
            Rectangle {
                required property int index
                anchors.verticalCenter: parent.verticalCenter
                width: 6
                height: 6
                radius: 3
                color: index === pages.currentIndex ? Theme.accent : Theme.control
            }
        }
    }
}
