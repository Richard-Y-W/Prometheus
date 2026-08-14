import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    objectName: "motorScenarioDialog"
    width: 720
    height: 800
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    property var executionController
    property var projectController
    property color panelColor: "#222a31"
    property color lineColor: "#35404a"
    property color textColor: "#dfe7ed"
    property color mutedColor: "#91a0ab"

    background: Rectangle {
        color: root.panelColor
        border.color: "#53616c"
        radius: 4
    }

    function valueOrDefault(name, fallback) {
        const draft = executionController ? executionController.scenarioDraft : ({})
        return draft[name] !== undefined ? String(draft[name]) : fallback
    }

    function loadDraft() {
        payloadMassField.text = valueOrDefault("payload_mass_kg", "8.0")
        armRadiusField.text = valueOrDefault("arm_radius_m", "0.2")
        rotationDegreesField.text = valueOrDefault("rotation_degrees", "90.0")
        moveDurationField.text = valueOrDefault("move_duration_s", "1.2")
        holdDurationField.text = valueOrDefault("hold_duration_s", "4.0")
        cycleDurationField.text = valueOrDefault("cycle_duration_s", "10.0")
        ambientTemperatureField.text = valueOrDefault("ambient_temperature_c", "35.0")
    }

    function draftValues() {
        return {
            "payload_mass_kg": Number(payloadMassField.text),
            "arm_radius_m": Number(armRadiusField.text),
            "rotation_degrees": Number(rotationDegreesField.text),
            "move_duration_s": Number(moveDurationField.text),
            "hold_duration_s": Number(holdDurationField.text),
            "cycle_duration_s": Number(cycleDurationField.text),
            "ambient_temperature_c": Number(ambientTemperatureField.text)
        }
    }

    function reviewTypedValues() {
        if (!executionController)
            return
        executionController.setScenarioDraft(draftValues())
        executionController.previewScenarioDegrees()
    }

    function confirmReviewedScenario() {
        if (!executionController)
            return
        executionController.confirmScenario(scenarioIntentField.text)
    }

    onOpened: loadDraft()
    Component.onCompleted: loadDraft()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 11

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Label { text: "REVIEW OPERATING SCENARIO"; color: root.mutedColor; font.bold: true; font.pixelSize: 11 }
                Label { text: "Conditions owned by this project"; color: root.textColor; font.pixelSize: 23 }
            }
            Item { Layout.fillWidth: true }
            Button { text: "×"; flat: true; onClicked: root.close() }
        }
        Label {
            text: "These editable values are not component properties. Review converts the entered degrees through C++ and displays the exact typed values that execution will store."
            color: root.mutedColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.lineColor }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 14
            rowSpacing: 8

            Label { text: "Payload mass"; color: root.mutedColor }
            TextField { id: payloadMassField; objectName: "payloadMassField"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0 } }
            Label { text: "kg"; color: root.textColor }

            Label { text: "Arm radius"; color: root.mutedColor }
            TextField { id: armRadiusField; objectName: "armRadiusField"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0 } }
            Label { text: "m"; color: root.textColor }

            Label { text: "Rotation"; color: root.mutedColor }
            TextField { id: rotationDegreesField; objectName: "rotationDegreesField"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0 } }
            Label { text: "deg (entered)"; color: root.textColor }

            Label { text: "Move duration"; color: root.mutedColor }
            TextField { id: moveDurationField; objectName: "moveDurationField"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0 } }
            Label { text: "s"; color: root.textColor }

            Label { text: "Hold duration"; color: root.mutedColor }
            TextField { id: holdDurationField; objectName: "holdDurationField"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0 } }
            Label { text: "s"; color: root.textColor }

            Label { text: "Cycle duration"; color: root.mutedColor }
            TextField { id: cycleDurationField; objectName: "cycleDurationField"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0 } }
            Label { text: "s"; color: root.textColor }

            Label { text: "Ambient temperature"; color: root.mutedColor }
            TextField { id: ambientTemperatureField; objectName: "ambientTemperatureField"; Layout.fillWidth: true; validator: DoubleValidator { } }
            Label { text: "degC"; color: root.textColor }
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "scenarioPreviewButton"
                text: "Review typed values"
                enabled: !root.executionController || !root.executionController.busy
                onClicked: root.reviewTypedValues()
            }
            Label {
                text: root.executionController && root.executionController.scenarioPreview.rotation_rad !== undefined
                      ? "C++ preview ready — confirmation is still required"
                      : "No authoritative preview yet"
                color: root.executionController && root.executionController.scenarioPreview.rotation_rad !== undefined
                       ? "#70c99a" : root.mutedColor
                Layout.fillWidth: true
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 194
            color: "#182027"
            border.color: root.executionController && root.executionController.scenarioPreview.rotation_rad !== undefined
                          ? "#365f4b" : root.lineColor
            radius: 3
            GridLayout {
                anchors.fill: parent
                anchors.margins: 12
                columns: 2
                columnSpacing: 18
                Label { text: "AUTHORITATIVE C++ PREVIEW"; color: root.mutedColor; font.bold: true; font.pixelSize: 10 }
                Label { text: "Stored typed values"; color: root.mutedColor; font.pixelSize: 10 }
                Label { text: "Rotation entered"; color: root.mutedColor }
                Label {
                    text: rotationDegreesField.text + " deg"
                    color: root.textColor
                }
                Label { text: "Rotation stored"; color: root.mutedColor }
                Label {
                    objectName: "rotationRadiansValue"
                    text: root.executionController && root.executionController.scenarioPreview.rotation_rad !== undefined
                          ? String(root.executionController.scenarioPreview.rotation_rad) + " "
                            + root.executionController.scenarioPreview.rotation_unit
                          : "— rad"
                    color: "#9fd1eb"
                    font.bold: true
                }
                Label { text: "Payload / radius"; color: root.mutedColor }
                Label {
                    text: root.executionController && root.executionController.scenarioPreview.payload_mass_kg !== undefined
                          ? String(root.executionController.scenarioPreview.payload_mass_kg) + " "
                            + root.executionController.scenarioPreview.payload_mass_unit + "  •  "
                            + String(root.executionController.scenarioPreview.arm_radius_m) + " "
                            + root.executionController.scenarioPreview.arm_radius_unit
                          : "—"
                    color: root.textColor
                }
                Label { text: "Move / hold / cycle"; color: root.mutedColor }
                Label {
                    text: root.executionController && root.executionController.scenarioPreview.move_duration_s !== undefined
                          ? String(root.executionController.scenarioPreview.move_duration_s) + " "
                            + root.executionController.scenarioPreview.move_duration_unit + "  •  "
                            + String(root.executionController.scenarioPreview.hold_duration_s) + " "
                            + root.executionController.scenarioPreview.hold_duration_unit + "  •  "
                            + String(root.executionController.scenarioPreview.cycle_duration_s) + " "
                            + root.executionController.scenarioPreview.cycle_duration_unit
                          : "—"
                    color: root.textColor
                }
                Label { text: "Ambient"; color: root.mutedColor }
                Label {
                    text: root.executionController && root.executionController.scenarioPreview.ambient_temperature_c !== undefined
                          ? String(root.executionController.scenarioPreview.ambient_temperature_c) + " "
                            + root.executionController.scenarioPreview.ambient_temperature_unit
                          : "—"
                    color: root.textColor
                }
                Label { text: "Motion profile"; color: root.mutedColor }
                Label {
                    text: root.executionController
                          ? root.executionController.scenarioPreview.motion_profile || "—"
                          : "—"
                    color: root.textColor
                }
            }
        }

        Label { text: "REVIEW INTENT"; color: root.mutedColor; font.bold: true; font.pixelSize: 10 }
        TextField {
            id: scenarioIntentField
            objectName: "scenarioIntentField"
            Layout.fillWidth: true
            placeholderText: "Required: state what this scenario is intended to evaluate"
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
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.executionController && root.executionController.scenarioConfirmed
                      ? "Confirmed scenario " + root.executionController.confirmedScenarioHash.substring(0, 22) + "…"
                      : "Editing or reviewing does not confirm the scenario."
                color: root.executionController && root.executionController.scenarioConfirmed
                       ? "#70c99a" : root.mutedColor
                Layout.fillWidth: true
            }
            Button { text: "Cancel"; onClicked: root.close() }
            Button {
                objectName: "confirmScenarioButton"
                text: "Confirm scenario"
                highlighted: true
                enabled: root.executionController
                         && root.executionController.scenarioPreview.rotation_rad !== undefined
                         && scenarioIntentField.text.trim() !== ""
                         && !root.executionController.busy
                onClicked: root.confirmReviewedScenario()
            }
        }
    }
}
