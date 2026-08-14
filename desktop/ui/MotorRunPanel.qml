import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root
    objectName: "motorRunPanel"

    property var executionController
    property var projectController
    property var engineeringController
    property color panelColor: "#20262c"
    property color lineColor: "#35404a"
    property color textColor: "#dfe7ed"
    property color mutedColor: "#91a0ab"
    readonly property var activePackage: executionController
                                         ? executionController.activePackage
                                         : ({})
    readonly property var selectedResult: executionController
                                          ? executionController.selectedResult
                                          : ({})
    readonly property var coverage: selectedResult.coverage || ({})
    readonly property var counts: coverage.counts || ({})

    signal reviewScenarioRequested()

    padding: 0
    background: Rectangle { color: root.panelColor }

    function abbreviated(value) {
        const text = String(value || "")
        return text.length > 28 ? text.substring(0, 18) + "…" + text.substring(text.length - 8) : text
    }

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 11

            Label { text: "MOTOR ARM CAPABILITY"; color: root.mutedColor; font.bold: true; font.pixelSize: 11 }
            Label {
                text: root.activePackage.component_id !== undefined
                      ? root.activePackage.manufacturer + " " + root.activePackage.part_number
                      : "No exact component package bound"
                color: root.textColor
                font.pixelSize: 21
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: root.activePackage.package_hash !== undefined
                      ? root.abbreviated(root.activePackage.package_hash)
                      : "Select an entity, review evidence, and bind exact bytes."
                color: root.activePackage.package_hash !== undefined ? "#83b6d5" : root.mutedColor
                font.pixelSize: 10
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.lineColor }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                color: "#192127"
                border.color: root.activePackage.execution_readiness === "ready"
                              ? "#365f4b"
                              : root.activePackage.execution_readiness === "blocked"
                                ? "#725d35" : root.lineColor
                radius: 3
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    Label { text: "PACKAGE EXECUTION GATE"; color: root.mutedColor; font.bold: true; font.pixelSize: 10 }
                    Label {
                        text: root.activePackage.execution_readiness || "not bound"
                        color: root.activePackage.execution_readiness === "ready" ? "#70c99a" : "#e0ac62"
                        font.bold: true
                    }
                    Label {
                        id: blockedReasonLabel
                        objectName: "blockedReasonLabel"
                        visible: root.activePackage.execution_readiness === "blocked"
                        text: root.activePackage.blocked_reason || ""
                        color: "#e0ac62"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                color: "#192127"
                border.color: root.executionController && root.executionController.scenarioConfirmed
                              ? "#365f4b" : root.lineColor
                radius: 3
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { text: "REVIEWED SCENARIO"; color: root.mutedColor; font.bold: true; font.pixelSize: 10 }
                        Label {
                            text: root.executionController && root.executionController.scenarioConfirmed
                                  ? "Confirmed immutable conditions"
                                  : "Explicit confirmation required"
                            color: root.executionController && root.executionController.scenarioConfirmed
                                   ? "#70c99a" : "#e0ac62"
                            font.bold: true
                        }
                        Label {
                            text: root.executionController && root.executionController.confirmedScenarioHash !== ""
                                  ? root.abbreviated(root.executionController.confirmedScenarioHash)
                                  : "No scenario object"
                            color: root.mutedColor
                            font.pixelSize: 10
                        }
                    }
                    Button { text: "Review…"; onClicked: root.reviewScenarioRequested() }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                visible: root.projectController && root.projectController.saveAsRequired
                color: "#302b21"
                border.color: "#725d35"
                Label {
                    anchors.fill: parent
                    anchors.margins: 9
                    text: "Save As version 2 is required before the first package binding or scenario confirmation."
                    color: "#e0ac62"
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    id: runMotorButton
                    objectName: "runMotorButton"
                    text: root.executionController && root.executionController.busy
                          ? "Running…" : "Run motor analysis"
                    highlighted: true
                    Layout.fillWidth: true
                    enabled: root.executionController && root.executionController.canRun
                    onClicked: root.executionController.runAnalysis()
                }
                Button {
                    text: "Cancel"
                    visible: root.executionController && root.executionController.busy
                    onClicked: root.executionController.cancelExecution()
                }
            }

            Label {
                visible: root.executionController && root.executionController.error !== ""
                text: root.executionController
                      ? root.executionController.error + " [" + root.executionController.errorCode + "]"
                      : ""
                color: "#e87972"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Label { text: "INDEPENDENT CAPABILITY STATUS"; color: root.mutedColor; font.bold: true; font.pixelSize: 10 }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 14
                rowSpacing: 6
                Label { text: "Geometry"; color: root.mutedColor }
                Label {
                    objectName: "geometryStatusLabel"
                    text: root.engineeringController
                          ? "Geometry: " + root.engineeringController.geometryStatus
                          : "Geometry: unavailable"
                    color: "#83b6d5"
                    font.bold: true
                }
                Label { text: "Motor"; color: root.mutedColor }
                Label {
                    objectName: "motorStatusLabel"
                    text: root.executionController
                          ? "Motor: " + (root.executionController.status || "not_evaluated")
                          : "Motor: unavailable"
                    color: root.executionController && root.executionController.status === "completed"
                           ? "#70c99a" : root.mutedColor
                    font.bold: true
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.lineColor }
            Label { text: "SELECTED MOTOR RESULT"; color: root.mutedColor; font.bold: true; font.pixelSize: 10 }
            Label {
                text: root.selectedResult.execution_disposition !== undefined
                      ? "Disposition: " + root.selectedResult.execution_disposition
                      : "Select a recorded run to inspect its scoped conclusions."
                color: root.selectedResult.execution_disposition === "completed" ? "#70c99a" : root.mutedColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            GridLayout {
                Layout.fillWidth: true
                visible: root.selectedResult.execution_disposition !== undefined
                columns: 4
                Label { text: "Pass"; color: root.mutedColor }
                Label { text: String(root.counts.pass || 0); color: "#70c99a"; font.bold: true }
                Label { text: "Fail"; color: root.mutedColor }
                Label { text: String(root.counts.fail || 0); color: "#e87972"; font.bold: true }
                Label { text: "Indeterminate"; color: root.mutedColor }
                Label { text: String(root.counts.indeterminate || 0); color: "#e0ac62"; font.bold: true }
                Label { text: "Not evaluated"; color: root.mutedColor }
                Label { text: String(root.counts.not_evaluated || 0); color: root.mutedColor; font.bold: true }
            }
            Label {
                visible: root.coverage.requested_obligations !== undefined
                text: "Coverage: " + root.coverage.evaluated_obligations + " / "
                      + root.coverage.requested_obligations + " requested obligations"
                color: root.textColor
            }
            Label {
                visible: root.executionController && root.executionController.runHistory.length > 0
                text: root.executionController.runHistory.length + " immutable recorded run"
                      + (root.executionController.runHistory.length === 1 ? "" : "s")
                color: "#83b6d5"
                font.bold: true
            }
            Item { Layout.fillHeight: true }
        }
    }
}
