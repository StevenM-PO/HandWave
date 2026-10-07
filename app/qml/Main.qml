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
    // Encoders go to the current page's control map. Switches 1 (Hold) and
    // 3 (next page) are the same everywhere; others go to the page.
    ControlSurface {
        id: controls
        onEncoderTurned: (encoder, steps) => window.currentPage().encoderTurned(encoder, steps)
        onEncoderPressed: (encoder) => window.currentPage().encoderPressed(encoder)
        onSwitchPressed: (sw) => {
            if (sw === 1)
                synth.holding = !synth.holding
            else if (sw === 3)
                pages.currentIndex = (pages.currentIndex + 1) % pages.count
            else
                window.currentPage().switchPressed(sw)
        }
    }

    StackLayout {
        id: pages
        anchors.fill: parent
        anchors.margins: 12

        OscillatorPage { synth: synth }
        FilterPage { synth: synth }
    }

    // Page indicator: one small dot per page, top-right corner.
    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 20
        spacing: 6
        Repeater {
            model: pages.count
            Rectangle {
                required property int index
                width: 6
                height: 6
                radius: 3
                color: index === pages.currentIndex ? Theme.accent : Theme.control
            }
        }
    }
}
