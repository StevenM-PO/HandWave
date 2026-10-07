import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HandWave

// Draw the oscillator's waveform; pitch, volume, phase and Join.
Item {
    id: page
    required property SynthController synth
    // For tools and tests (main.cpp --eval) to reach the drawing surface.
    property alias canvas: canvas

    // Phase moves in 1/64ths of a cycle per encoder detent or button click
    // (8 of the canvas's 511 points).
    readonly property int phaseStep: 8
    // Join bends this fraction of the cycle around the seam.
    readonly property real joinBlend: 0.05
    readonly property real volumeStep: 0.02
    // Volume to restore when un-muting; negative when not muted.
    property real mutedVolume: -1

    // ---- Control map for this page ----
    function encoderTurned(encoder, steps) {
        switch (encoder) {
        case 1: // phase
            canvas.rotatePhase(steps * phaseStep)
            break
        case 2: // pitch, in semitones
            synth.frequency = synth.frequency * Math.pow(2, steps / 12)
            break
        case 3: { // volume (turning cancels mute)
            const base = mutedVolume >= 0 ? 0 : synth.volume
            mutedVolume = -1
            synth.volume = base + steps * volumeStep
            break
        }
        }
    }

    function encoderPressed(encoder) {
        switch (encoder) {
        case 1: canvas.joinEnds(joinBlend); break
        case 2: synth.holding = !synth.holding; break
        case 3: toggleMute(); break
        }
    }

    function switchPressed(sw) {
        if (sw === 2)
            canvas.joinEnds(joinBlend)
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

    function noteName(hz) {
        const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        const midi = Math.round(12 * Math.log2(hz / 440) + 69)
        return names[((midi % 12) + 12) % 12] + (Math.floor(midi / 12) - 1)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ---- Drawing surface ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: Theme.panel

            WaveCanvas {
                id: canvas
                anchors.fill: parent
                anchors.margins: 10
                lineColor: Theme.accent
                gridColor: Theme.grid
                onSamplesChanged: page.synth.setShape(samples)
                Component.onCompleted: page.synth.setShape(samples)
            }

            Text {
                visible: page.synth.statusText.length > 0
                text: page.synth.statusText
                color: Theme.error
                font.pixelSize: 14
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 14
            }
        }

        // ---- Presets, phase, hold ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            PadButton { text: "Sine";     onClicked: canvas.loadPreset("sine") }
            PadButton { text: "Triangle"; onClicked: canvas.loadPreset("triangle") }
            PadButton { text: "Saw";      onClicked: canvas.loadPreset("saw") }
            PadButton { text: "Square";   onClicked: canvas.loadPreset("square") }
            PadButton { text: "Clear";    onClicked: canvas.clear() }

            Item { Layout.fillWidth: true }

            PadButton {
                text: "◀"
                implicitWidth: 48
                autoRepeat: true
                onClicked: canvas.rotatePhase(-page.phaseStep)
            }
            PadButton {
                text: "▶"
                implicitWidth: 48
                autoRepeat: true
                onClicked: canvas.rotatePhase(page.phaseStep)
            }
            PadButton { text: "Join"; onClicked: canvas.joinEnds(page.joinBlend) }

            PadButton {
                text: page.synth.holding ? "Holding" : "Hold"
                implicitWidth: 120
                checked: page.synth.holding
                onClicked: page.synth.holding = !page.synth.holding
            }
        }

        // ---- Pitch and volume ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text { text: "Pitch"; color: Theme.text; font.pixelSize: Theme.fontSize }
            Slider {
                id: pitchSlider
                Layout.fillWidth: true
                // Logarithmic: 0..1 maps to 20 Hz .. 2 kHz so each octave
                // takes the same slider distance.
                from: 0; to: 1
                onMoved: page.synth.frequency = 20 * Math.pow(100, value)
                // Follow pitch changes from elsewhere (encoders). A plain
                // `value:` binding would break the first time the slider is dragged.
                Binding on value {
                    when: !pitchSlider.pressed
                    value: Math.log(page.synth.frequency / 20) / Math.log(100)
                    restoreMode: Binding.RestoreNone
                }
            }
            Text {
                text: Math.round(page.synth.frequency) + " Hz  " + page.noteName(page.synth.frequency)
                color: Theme.text
                font.pixelSize: Theme.fontSize
                Layout.preferredWidth: 110
            }

            Text { text: "Volume"; color: Theme.text; font.pixelSize: Theme.fontSize }
            Slider {
                id: volumeSlider
                Layout.preferredWidth: 160
                from: 0; to: 1
                onMoved: {
                    page.mutedVolume = -1
                    page.synth.volume = value
                }
                Binding on value {
                    when: !volumeSlider.pressed
                    value: page.synth.volume
                    restoreMode: Binding.RestoreNone
                }
            }
        }
    }
}
