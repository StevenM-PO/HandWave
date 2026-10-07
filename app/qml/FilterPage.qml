import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HandWave

// Draw the filter's response; cutoff, loop, resonance and bypass.
Item {
    id: page
    required property SynthController synth
    property string title: "FILTER"
    // For tools and tests (main.cpp --eval) to reach the drawing surface.
    property alias canvas: canvas

    // ---- Control map for this page ----
    function encoderTurned(encoder, steps) {
        switch (encoder) {
        case 1: canvas.shiftOctaves += steps / 12; break    // cutoff, semitones
        case 2: canvas.resonanceDb += steps; break          // resonance level, dB
        case 3: canvas.moveResonance(steps); break          // resonance position, semitones
        case 4: synth.filterEnvAmount += steps * 0.25; break // filter envelope amount, octaves
        }
    }

    function encoderPressed(encoder) {
        switch (encoder) {
        case 1: canvas.loop = !canvas.loop; break
        case 2: canvas.resetResonance(); break
        case 3: synth.filterEnabled = !synth.filterEnabled; break
        case 4: synth.filterEnvAmount = 0; break
        }
    }

    function switchPressed(sw) {}

    function hzText(hz) {
        return hz >= 1000 ? (hz / 1000).toFixed(hz >= 10000 ? 0 : 1) + " kHz" : Math.round(hz) + " Hz"
    }

    function signed(value, digits) {
        return (value >= 0 ? "+" : "") + value.toFixed(digits)
    }

    // The canvas holds the filter's state; the synth follows it.
    Binding { target: page.synth; property: "filterShift"; value: canvas.shiftOctaves }
    Binding { target: page.synth; property: "filterLoop"; value: canvas.loop }
    Binding { target: page.synth; property: "resonanceX"; value: canvas.resonanceX }
    Binding { target: page.synth; property: "resonanceDb"; value: canvas.resonanceDb }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ---- Drawing surface ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: Theme.panel

            FilterCanvas {
                id: canvas
                anchors.fill: parent
                anchors.margins: 10
                lineColor: Theme.accent
                gridColor: Theme.grid
                sampleRate: page.synth.sampleRate
                opacity: page.synth.filterEnabled ? 1.0 : 0.4
                onSamplesChanged: page.synth.setFilterCurve(samples)
                Component.onCompleted: page.synth.setFilterCurve(samples)
            }

            // Decade labels along the bottom of the plot.
            Repeater {
                model: [{ hz: 100, label: "100" }, { hz: 1000, label: "1k" }, { hz: 10000, label: "10k" }]
                Text {
                    required property var modelData
                    text: modelData.label
                    color: Theme.dimText
                    font.pixelSize: Theme.smallFontSize
                    x: canvas.x + canvas.xForHz(modelData.hz) + 3 + 0 * canvas.width
                    y: canvas.y + canvas.height - height - 2
                }
            }
        }

        // ---- Presets, loop, bypass, readout ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            PadButton { text: "LP";   implicitWidth: 56; onClicked: canvas.loadPreset("lowpass") }
            PadButton { text: "HP";   implicitWidth: 56; onClicked: canvas.loadPreset("highpass") }
            PadButton { text: "BP";   implicitWidth: 56; onClicked: canvas.loadPreset("bandpass") }
            PadButton { text: "Flat"; implicitWidth: 56; onClicked: canvas.loadPreset("flat") }

            CheckBox {
                id: loopBox
                text: "Loop"
                onToggled: canvas.loop = checked
                // A Binding (unlike a plain binding) survives the box being
                // clicked, so encoder changes keep showing.
                Binding on checked { value: canvas.loop; restoreMode: Binding.RestoreNone }
                indicator: Rectangle {
                    implicitWidth: 26
                    implicitHeight: 26
                    x: loopBox.leftPadding
                    y: (loopBox.height - height) / 2
                    radius: 6
                    color: loopBox.checked ? Theme.accent : Theme.control
                    Text {
                        anchors.centerIn: parent
                        visible: loopBox.checked
                        text: "✓"
                        color: Theme.background
                        font.pixelSize: 16
                        font.bold: true
                    }
                }
                contentItem: Text {
                    text: loopBox.text
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: loopBox.indicator.width + loopBox.spacing
                }
            }

            PadButton {
                text: "Bypass"
                checked: !page.synth.filterEnabled
                onClicked: page.synth.filterEnabled = !page.synth.filterEnabled
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                color: Theme.text
                font.pixelSize: 13
                text: "Cutoff " + page.signed(canvas.shiftOctaves, 1) + " oct  ·  Env "
                      + page.signed(page.synth.filterEnvAmount, 2) + " oct\nRes "
                      + (canvas.resonanceHz > 0
                         ? page.hzText(canvas.resonanceHz) + " " + page.signed(canvas.resonanceDb, 1) + " dB"
                         : "off-screen")
            }
        }
    }
}
