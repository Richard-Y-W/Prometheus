import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var projectIntakeController
    property bool cadPartSelected: false
    property bool claimsExpanded: false
    property color panelColor: "#20262c"
    property color lineColor: "#35404a"
    property color textColor: "#dfe7ed"
    property color mutedColor: "#91a0ab"

    signal loadRequested(string path)
    signal bindCandidateRequested(var candidate)
    signal closeRequested()

    function stateColor(state) {
        if (state === "ready")
            return "#70c99a"
        if (state === "not_evaluated")
            return "#e0ac62"
        return "#e87972"
    }

    function formatBytes(value) {
        const bytes = Number(value || 0)
        if (bytes < 1024)
            return bytes + " B"
        if (bytes < 1024 * 1024)
            return (bytes / 1024).toFixed(1) + " KiB"
        if (bytes < 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GiB"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "PROJECT INTAKE"
                    color: root.mutedColor
                    font.bold: true
                    font.pixelSize: 11
                }
                Label {
                    text: root.projectIntakeController.rootPath === ""
                          ? "Select a mechanical-project folder"
                          : root.projectIntakeController.rootPath
                    color: root.textColor
                    font.pixelSize: 20
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
            Button {
                text: "Close"
                onClicked: root.closeRequested()
            }
        }

        Label {
            text: "Every discovered file stays visible. Only STEP is loadable in this prototype; recognized files are not silently treated as understood."
            color: root.mutedColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [
                    { label: "ACCOUNTED", value: root.projectIntakeController.totalCount, color: root.textColor },
                    { label: "READY", value: root.projectIntakeController.readyCount, color: "#70c99a" },
                    { label: "NOT EVALUATED", value: root.projectIntakeController.notEvaluatedCount, color: "#e0ac62" },
                    { label: "UNSUPPORTED", value: root.projectIntakeController.unsupportedCount, color: "#e87972" },
                    { label: "UNREADABLE", value: root.projectIntakeController.unreadableCount, color: "#ef6f6c" },
                    { label: "DUPLICATE COPIES", value: root.projectIntakeController.duplicateCopyCount, color: "#8ca7d8" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    color: "#1b2228"
                    border.color: root.lineColor
                    radius: 3
                    Column {
                        anchors.centerIn: parent
                        spacing: 3
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.value
                            color: modelData.color
                            font.bold: true
                            font.pixelSize: 21
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.label
                            color: root.mutedColor
                            font.bold: true
                            font.pixelSize: 9
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.lineColor
        }

        BusyIndicator {
            visible: root.projectIntakeController.busy
            running: visible
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            visible: root.projectIntakeController.busy
            text: root.projectIntakeController.status
            color: root.mutedColor
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            visible: !root.projectIntakeController.busy
                     && root.projectIntakeController.error !== ""
            text: root.projectIntakeController.error
            color: "#e87972"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: !root.projectIntakeController.busy
                     && root.projectIntakeController.error === ""
            text: root.projectIntakeController.status
            color: root.mutedColor
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 112
            visible: root.projectIntakeController.candidateComponents.length > 0
            color: "#1b2228"
            border.color: "#8ca7d8"
            radius: 3
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: "CANDIDATE COMPONENT EVIDENCE"; color: "#8ca7d8"; font.bold: true; font.pixelSize: 10 }
                    Label {
                        property var candidate: root.projectIntakeController.candidateComponents.length > 0
                                                ? root.projectIntakeController.candidateComponents[0] : ({})
                        text: candidate.manufacturer + " " + candidate.part_number
                        color: root.textColor
                        font.pixelSize: 18
                    }
                    Label {
                        text: "Source hash verified • specifications unreviewed • execution disabled"
                        color: "#e0ac62"
                        font.pixelSize: 10
                    }
                    Label {
                        property var candidate: root.projectIntakeController.candidateComponents.length > 0
                                                ? root.projectIntakeController.candidateComponents[0] : ({})
                        text: (candidate.candidate_claims ? candidate.candidate_claims.length : 0)
                              + " source-located specification candidates await human review"
                        color: root.mutedColor
                        font.pixelSize: 10
                    }
                }
                ColumnLayout {
                    Button {
                        text: root.claimsExpanded ? "Hide claims" : "Review claims"
                        onClicked: root.claimsExpanded = !root.claimsExpanded
                    }
                    Button {
                        text: "Bind candidate to selected part"
                        enabled: root.cadPartSelected
                        onClicked: root.bindCandidateRequested(root.projectIntakeController.candidateComponents[0])
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            visible: root.claimsExpanded
                     && root.projectIntakeController.candidateComponents.length > 0
            color: "#171e24"
            border.color: root.lineColor
            radius: 3
            ListView {
                id: candidateClaimList
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                spacing: 5
                model: root.projectIntakeController.candidateComponents.length > 0
                       ? root.projectIntakeController.candidateComponents[0].candidate_claims : []
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 58
                    color: "#202830"
                    border.color: modelData.review_state === "accepted" ? "#70c99a"
                                  : modelData.review_state === "rejected" ? "#e87972"
                                  : root.lineColor
                    radius: 3
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label { text: modelData.label + "  " + modelData.original_value + " " + modelData.original_unit; color: root.textColor; font.bold: true }
                            Label { text: "SI: " + Number(modelData.value_si).toPrecision(8) + " " + modelData.si_unit + "  •  " + modelData.source_file + " p." + modelData.source_page; color: root.mutedColor; font.pixelSize: 9 }
                        }
                        Label { text: String(modelData.review_state).toUpperCase(); color: modelData.review_state === "accepted" ? "#70c99a" : modelData.review_state === "rejected" ? "#e87972" : "#e0ac62"; font.bold: true; font.pixelSize: 9 }
                        Button { text: "Accept"; onClicked: root.projectIntakeController.reviewCandidateClaim(root.projectIntakeController.candidateComponents[0].id, modelData.id, "accepted") }
                        Button { text: "Reject"; onClicked: root.projectIntakeController.reviewCandidateClaim(root.projectIntakeController.candidateComponents[0].id, modelData.id, "rejected") }
                        Button { text: "Reset"; enabled: modelData.review_state !== "unreviewed"; onClicked: root.projectIntakeController.reviewCandidateClaim(root.projectIntakeController.candidateComponents[0].id, modelData.id, "unreviewed") }
                    }
                }
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.projectIntakeController.busy
            clip: true
            spacing: 7
            model: root.projectIntakeController.artifacts

            delegate: Rectangle {
                required property int index
                required property var modelData
                width: ListView.view.width
                height: 104
                color: "#1b2228"
                border.color: root.lineColor
                radius: 3

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.fillHeight: true
                        color: root.stateColor(modelData.analysis_state)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: modelData.relative_path
                                color: root.textColor
                                font.bold: true
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Label {
                                text: String(modelData.analysis_state).replace(/_/g, " ").toUpperCase()
                                color: root.stateColor(modelData.analysis_state)
                                font.bold: true
                                font.pixelSize: 10
                            }
                        }
                        Label {
                            text: modelData.category + "  •  " + root.formatBytes(modelData.byte_size)
                                  + (modelData.duplicate_copy ? "  •  exact duplicate copy" : "")
                            color: root.mutedColor
                            font.pixelSize: 10
                        }
                        Label {
                            text: modelData.detail
                            color: modelData.analysis_state === "ready" ? "#83b6d5" : root.mutedColor
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: modelData.sha256 === "" ? "SHA-256 unavailable" : modelData.sha256
                            color: root.mutedColor
                            font.family: "monospace"
                            font.pixelSize: 9
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }
                    Button {
                        objectName: "loadArtifactButton_" + index
                        visible: modelData.loadable
                        text: "Load assembly"
                        highlighted: true
                        onClicked: root.loadRequested(modelData.absolute_path)
                    }
                }
            }
        }

        Label {
            visible: !root.projectIntakeController.busy
                     && root.projectIntakeController.readyCount > 1
            text: "Multiple STEP files were found. Choose the authoritative assembly; Prometheus will not guess."
            color: "#e0ac62"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
