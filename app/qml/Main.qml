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
    color: "#0e1116"

    readonly property color accent: "#4fd1c5"
    readonly property color panel: "#151a22"
    readonly property color control: "#232a35"
    readonly property color textColor: "#d8dee9"

    // Phase moves in 1/64ths of a cycle per encoder detent or button click
    // (8 of the canvas's 511 points).
    readonly property int phaseStep: 8
    // Join bends this fraction of the cycle around the seam.
    readonly property real joinBlend: 0.05
    readonly property real volumeStep: 0.02
    // Volume to restore when un-muting; negative when not muted.
    property real mutedVolume: -1

    SynthController {
        id: synth
    }

    // ---- Hardware controls (keyboard-simulated for now, GPIO later) ----
    // This block is the control map for this page. Encoder N = hold digit N
    // while turning/pressing the knob (or [ ] Enter); bare knob = encoder 1.
    ControlSurface {
        id: controls

        onEncoderTurned: (encoder, steps) => {
            switch (encoder) {
            case 1: // phase
                canvas.rotatePhase(steps * window.phaseStep)
                break
            case 2: // pitch, in semitones
                synth.frequency = synth.frequency * Math.pow(2, steps / 12)
                break
            case 3: // volume (turning cancels mute)
                const base = window.mutedVolume >= 0 ? 0 : synth.volume
                window.mutedVolume = -1
                synth.volume = base + steps * window.volumeStep
                break
            }
        }

        onEncoderPressed: (encoder) => {
            switch (encoder) {
            case 1: canvas.joinEnds(window.joinBlend); break
            case 2: synth.holding = !synth.holding; break
            case 3: window.toggleMute(); break
            }
        }

        onSwitchPressed: (sw) => {
            switch (sw) {
            case 1: synth.holding = !synth.holding; break
            case 2: canvas.joinEnds(window.joinBlend); break
            }
        }
    }

    function toggleMute() {
        if (mutedVolume >= 0) {
            synth.volume = mutedVolume
            mutedVolume = -1
        } else {
            mutedVolume = synth.volume
            synth.volume = 0
        }
    }

    // Large touch-friendly button used throughout the bar below.
    component PadButton: Button {
        id: pad
        implicitHeight: 48
        implicitWidth: 76
        font.pixelSize: 15
        contentItem: Text {
            text: pad.text
            font: pad.font
            color: pad.checked ? window.color : window.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8
            color: pad.checked ? window.accent : (pad.down ? Qt.lighter(window.control, 1.4) : window.control)
        }
    }

    function noteName(hz) {
        const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        const midi = Math.round(12 * Math.log2(hz / 440) + 69)
        return names[((midi % 12) + 12) % 12] + (Math.floor(midi / 12) - 1)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // ---- Drawing surface ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: window.panel

            WaveCanvas {
                id: canvas
                anchors.fill: parent
                anchors.margins: 10
                lineColor: window.accent
                gridColor: "#262d38"
                onSamplesChanged: synth.setShape(samples)
            }

            Text {
                visible: synth.statusText.length > 0
                text: synth.statusText
                color: "#ff7a7a"
                font.pixelSize: 14
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 14
            }
        }

        // ---- Presets and hold ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            PadButton { text: "Sine";     onClicked: canvas.loadPreset("sine") }
            PadButton { text: "Triangle"; onClicked: canvas.loadPreset("triangle") }
            PadButton { text: "Saw";      onClicked: canvas.loadPreset("saw") }
            PadButton { text: "Square";   onClicked: canvas.loadPreset("square") }
            PadButton { text: "Clear";    onClicked: canvas.clear() }

            Item { Layout.fillWidth: true }

            // Touch stand-ins for a phase encoder and a "join" switch.
            PadButton {
                text: "◀"
                implicitWidth: 48
                autoRepeat: true
                onClicked: canvas.rotatePhase(-window.phaseStep)
            }
            PadButton {
                text: "▶"
                implicitWidth: 48
                autoRepeat: true
                onClicked: canvas.rotatePhase(window.phaseStep)
            }
            PadButton { text: "Join"; onClicked: canvas.joinEnds(window.joinBlend) }

            PadButton {
                text: synth.holding ? "Holding" : "Hold"
                implicitWidth: 120
                // Not checkable: clicking would overwrite `checked` and break
                // this binding, so encoder/switch changes would stop showing.
                checked: synth.holding
                onClicked: synth.holding = !synth.holding
            }
        }

        // ---- Pitch and volume ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text { text: "Pitch"; color: window.textColor; font.pixelSize: 15 }
            Slider {
                id: pitchSlider
                Layout.fillWidth: true
                // Logarithmic: 0..1 maps to 20 Hz .. 2 kHz so each octave
                // takes the same slider distance.
                from: 0; to: 1
                onMoved: synth.frequency = 20 * Math.pow(100, value)
                // Follow pitch changes from elsewhere (encoders). A plain
                // `value:` binding would break the first time the slider is dragged.
                Binding on value {
                    when: !pitchSlider.pressed
                    value: Math.log(synth.frequency / 20) / Math.log(100)
                    restoreMode: Binding.RestoreNone
                }
            }
            Text {
                text: Math.round(synth.frequency) + " Hz  " + window.noteName(synth.frequency)
                color: window.textColor
                font.pixelSize: 15
                Layout.preferredWidth: 110
            }

            Text { text: "Volume"; color: window.textColor; font.pixelSize: 15 }
            Slider {
                id: volumeSlider
                Layout.preferredWidth: 160
                from: 0; to: 1
                onMoved: {
                    window.mutedVolume = -1
                    synth.volume = value
                }
                Binding on value {
                    when: !volumeSlider.pressed
                    value: synth.volume
                    restoreMode: Binding.RestoreNone
                }
            }
        }
    }

    Component.onCompleted: synth.setShape(canvas.samples)
}
