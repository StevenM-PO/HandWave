pragma Singleton
import QtQuick

// Shared colours and sizes for every page.
QtObject {
    readonly property color background: "#0e1116"
    readonly property color accent: "#4fd1c5"
    readonly property color panel: "#151a22"
    readonly property color control: "#232a35"
    readonly property color grid: "#262d38"
    readonly property color text: "#d8dee9"
    readonly property color dimText: "#7d8794"
    readonly property color error: "#ff7a7a"

    readonly property int fontSize: 15
    readonly property int smallFontSize: 10
}
