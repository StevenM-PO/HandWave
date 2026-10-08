import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HandWave

// One envelope (amp or filter): ADSR handles or a free drawing, plus knobs.
Item {
    id: page
    required property SynthController synth
    required property EnvelopeController envelope
    property string title
    // For tools and tests (main.cpp --eval) to reach the drawing surface.
    property alias canvas: canvas
    readonly property bool drawn: envelope.mode === 1

    // ---- Control map for this page ----
    function encoderTurned(encoder, steps) {
        const e = envelope
        if (!drawn) {
            switch (encoder) {
            case 1: e.attack = e.stepTime(e.attack, steps, 0.001, 10); break
            case 2: e.decay = e.stepTime(e.decay, steps, 0.001, 20); break
            case 3: e.sustain += steps * 0.02; break
            case 4: e.release = e.stepTime(e.release, steps, 0.001, 20); break
            }
        } else {
            switch (encoder) {
            case 1: e.split1 += steps * 0.01; break
            case 2: e.split2 += steps * 0.01; break
            case 4: e.timespan = e.stepTime(e.timespan, steps, 0.01, 30); break
            }
        }
    }

    function encoderPressed(encoder) {
        if (encoder === 1)
            envelope.mode = drawn ? 0 : 1
    }

    function switchPressed(sw) {}

    // Rounded, readable times: "4 ms", "120 ms", "1.5 s", "12 s".
    function timeText(seconds) {
        if (seconds < 0.01) return (seconds * 1000).toFixed(1) + " ms"
        if (seconds < 1) return Math.round(seconds * 1000) + " ms"
        if (seconds < 10) return seconds.toFixed(1) + " s"
        return Math.round(seconds) + " s"
    }

    function percent(level) {
        return Math.round(level * 100) + "%"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: Theme.panel

            EnvelopeCanvas {
                id: canvas
                anchors.fill: parent
                anchors.margins: 10
                envelope: page.envelope
                gridColor: Theme.grid
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            PadButton {
                text: page.drawn ? "Draw" : "ADSR"
                onClicked: page.envelope.mode = page.drawn ? 0 : 1
            }
            PadButton {
                text: "Play"
                checked: pressed
                onPressedChanged: page.synth.playing = pressed
            }
            PadButton {
                text: "Hold"
                checked: page.synth.holding
                onClicked: page.synth.holding = !page.synth.holding
            }

            // Draw mode's total time; logarithmic, 10 ms .. 30 s.
            Slider {
                id: timespanSlider
                visible: page.drawn
                Layout.preferredWidth: 150
                from: 0; to: 1
                onMoved: page.envelope.timespan = 0.01 * Math.pow(3000, value)
                Binding on value {
                    when: !timespanSlider.pressed
                    value: Math.log(page.envelope.timespan / 0.01) / Math.log(3000)
                    restoreMode: Binding.RestoreNone
                }
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                color: Theme.text
                font.pixelSize: 13
                text: {
                    const e = page.envelope
                    if (!page.drawn)
                        return "A " + page.timeText(e.attack) + "  ·  D " + page.timeText(e.decay)
                               + "  ·  S " + page.percent(e.sustain) + "  ·  R " + page.timeText(e.release)
                    const t = e.timespan
                    return "A " + page.timeText(e.split1 * t) + " · D " + page.timeText((e.split2 - e.split1) * t)
                           + " · R " + page.timeText((1 - e.split2) * t) + " · S " + page.percent(canvas.sustainLevel)
                           + "\nTotal " + page.timeText(t)
                }
            }
        }
    }
}
