import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "runHistoryPanel"

    property var executionController
    property color panelColor: "#20262c"
    property color lineColor: "#35404a"
    property color textColor: "#dfe7ed"
    property color mutedColor: "#91a0ab"
    readonly property int historyCount: executionController
                                                ? executionController.runHistory.length
                                                : 0
    readonly property var selectedResult: executionController
                                          ? executionController.selectedResult
                                          : ({})
    readonly property var selectedBackend: selectedResult.backend || ({})
    readonly property var numericProfile: selectedBackend.numeric_profile || ({})
    readonly property var consumedInputs: selectedResult.consumed_inputs || ({})

    function abbreviated(value) {
        const text = String(value || "")
        return text.length > 30 ? text.substring(0, 18) + "…" + text.substring(text.length - 9) : text
    }

    function quantityText(value) {
        if (!value || value.value === undefined)
            return "—"
        return String(value.value) + " " + String(value.unit || "")
    }

    function inputNames(values) {
        const names = []
        const items = values || []
        for (let index = 0; index < items.length; ++index)
            names.push(String(items[index].slot_name || "unnamed input"))
        return names.length > 0 ? names.join(", ") : "none"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Label { text: "IMMUTABLE RUN HISTORY"; color: root.mutedColor; font.bold: true; font.pixelSize: 11 }
                Label {
                    text: root.historyCount === 0 ? "No committed motor runs" : root.historyCount + " recorded run" + (root.historyCount === 1 ? "" : "s")
                    color: root.textColor
                    font.pixelSize: 21
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: root.executionController && root.executionController.replayState !== ""
                      ? root.executionController.replayState : "Recorded ≠ replayed"
                color: root.executionController && root.executionController.replayState === "Exact match"
                       ? "#70c99a"
                       : root.executionController && root.executionController.replayState === "Reproduction failed"
                         ? "#e87972" : "#83b6d5"
                font.bold: true
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.lineColor }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 292
                Layout.fillHeight: true
                color: "#181f25"
                border.color: root.lineColor
                ListView {
                    id: historyList
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    spacing: 3
                    model: root.executionController ? root.executionController.runHistory : []
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        width: ListView.view.width
                        height: 94
                        color: root.executionController && index === root.executionController.selectedRunIndex
                               ? "#273944" : "#1d252b"
                        border.color: root.executionController && index === root.executionController.selectedRunIndex
                                      ? "#3d9bd6" : "#303b44"
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "RUN " + (index + 1); color: root.mutedColor; font.bold: true; font.pixelSize: 10 }
                                Item { Layout.fillWidth: true }
                                Label {
                                    text: modelData.status
                                    color: modelData.status === "Recorded" ? "#83b6d5" : "#e87972"
                                    font.bold: true
                                }
                            }
                            Label { text: root.abbreviated(modelData.package_hash); color: root.textColor; font.pixelSize: 10 }
                            Label { text: root.abbreviated(modelData.manifest_hash); color: root.mutedColor; font.pixelSize: 10 }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.executionController.selectRun(index)
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: root.historyCount === 0
                    text: "Completed runs appear here\nafter transactional publication."
                    color: root.mutedColor
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#181f25"
                border.color: root.lineColor
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 9
                        visible: root.selectedResult.execution_disposition !== undefined

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            Label {
                                text: "Recorded engineering result"
                                color: root.textColor
                                font.pixelSize: 18
                                font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: root.executionController && root.executionController.busy ? "Replaying…" : "Replay exact bytes"
                                enabled: root.executionController && !root.executionController.busy
                                onClicked: root.executionController.replaySelected()
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            columns: 2
                            columnSpacing: 14
                            Label { text: "Backend"; color: root.mutedColor }
                            Label { text: root.selectedBackend.backend_id || "—"; color: root.textColor }
                            Label { text: "Contract"; color: root.mutedColor }
                            Label { text: root.selectedBackend.contract_version || "—"; color: root.textColor }
                            Label { text: "Build fingerprint"; color: root.mutedColor }
                            Label { text: root.abbreviated(root.numericProfile.backend_build_fingerprint); color: "#83b6d5" }
                            Label { text: "Package hash"; color: root.mutedColor }
                            Label { text: root.abbreviated(root.selectedResult.package_hash); color: "#83b6d5" }
                            Label { text: "Request hash"; color: root.mutedColor }
                            Label {
                                text: root.selectedResult.obligation_outcomes
                                      && root.selectedResult.obligation_outcomes.length > 0
                                      ? root.abbreviated(root.selectedResult.obligation_outcomes[0].request_hash) : "—"
                                color: "#83b6d5"
                            }
                            Label { text: "Scenario hash"; color: root.mutedColor }
                            Label {
                                text: root.selectedResult.obligation_outcomes
                                      && root.selectedResult.obligation_outcomes.length > 0
                                      ? root.abbreviated(root.selectedResult.obligation_outcomes[0].scenario_hash) : "—"
                                color: "#83b6d5"
                            }
                        }

                        Label {
                            Layout.leftMargin: 10
                            text: "SCOPED FINDINGS"
                            color: root.mutedColor
                            font.bold: true
                            font.pixelSize: 10
                        }
                        Repeater {
                            model: root.selectedResult.obligation_outcomes || []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.leftMargin: 10
                                Layout.rightMargin: 10
                                Layout.preferredHeight: findingContent.implicitHeight + 18
                                color: "#1d252b"
                                border.color: modelData.outcome === "pass" ? "#365f4b"
                                              : modelData.outcome === "fail" ? "#8a4542" : "#725d35"
                                radius: 3
                                ColumnLayout {
                                    id: findingContent
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 9
                                    spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: String(modelData.outcome).toUpperCase()
                                            color: modelData.outcome === "pass" ? "#70c99a"
                                                   : modelData.outcome === "fail" ? "#e87972" : "#e0ac62"
                                            font.bold: true
                                        }
                                        Label { text: modelData.title; color: root.textColor; font.bold: true; Layout.fillWidth: true }
                                    }
                                    Label { text: modelData.mechanism; color: root.mutedColor; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Label {
                                        text: root.quantityText(modelData.calculated_quantity) + "  "
                                              + modelData.comparison_operator + "  "
                                              + root.quantityText(modelData.comparison_quantity)
                                              + "  •  signed margin " + String(modelData.signed_margin)
                                        color: root.textColor
                                        font.bold: true
                                    }
                                    Label {
                                        text: "Claims: " + (modelData.consumed_claim_ids || []).join(", ")
                                        color: "#83b6d5"
                                        font.pixelSize: 10
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: "Package " + root.abbreviated(modelData.package_hash)
                                              + "  •  Request " + root.abbreviated(modelData.request_hash)
                                              + "  •  Scenario " + root.abbreviated(modelData.scenario_hash)
                                        color: "#83b6d5"
                                        font.pixelSize: 10
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: "Backend " + (root.selectedBackend.backend_id || "—")
                                              + "  •  Profile "
                                              + root.abbreviated(root.numericProfile.backend_build_fingerprint)
                                        color: root.mutedColor
                                        font.pixelSize: 10
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: "Assumptions: " + (modelData.assumptions || []).join(" • ")
                                        color: "#d8b36e"
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: "Limitations: " + (modelData.limitations || []).join(" • ")
                                        color: root.mutedColor
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }

                        Label {
                            Layout.leftMargin: 10
                            text: "INPUT ACCOUNTING"
                            color: root.mutedColor
                            font.bold: true
                            font.pixelSize: 10
                        }
                        Label {
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            Layout.fillWidth: true
                            text: "Calculation inputs: " + (root.consumedInputs.calculation_inputs || []).length
                                  + "  •  validation-only: " + (root.consumedInputs.validation_inputs || []).length
                                  + "  •  available but unused: " + (root.consumedInputs.available_but_unused || []).length
                            color: root.textColor
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            Layout.fillWidth: true
                            text: "Validation-only: "
                                  + root.inputNames(root.consumedInputs.validation_inputs)
                            color: "#d8b36e"
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            Layout.fillWidth: true
                            text: "Available but unused: "
                                  + root.inputNames(root.consumedInputs.available_but_unused)
                            color: root.mutedColor
                            wrapMode: Text.WordWrap
                        }
                        Repeater {
                            model: root.selectedResult.missing_information || []
                            delegate: Label {
                                required property var modelData
                                Layout.leftMargin: 10
                                Layout.rightMargin: 10
                                Layout.fillWidth: true
                                text: "Not evaluated — " + modelData.question_id + ": " + modelData.reason
                                color: "#e0ac62"
                                wrapMode: Text.WordWrap
                            }
                        }
                        Item { Layout.preferredHeight: 10 }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: root.selectedResult.execution_disposition === undefined
                    text: "Select a verified Recorded row.\nViewing does not invoke replay."
                    color: root.mutedColor
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
