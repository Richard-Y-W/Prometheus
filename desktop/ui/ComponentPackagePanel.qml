import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "componentPackagePanel"

    property var serviceController
    property var executionController
    property string selectedEntityId: ""
    property string selectedEntityName: ""
    property color panelColor: "#20262c"
    property color lineColor: "#35404a"
    property color textColor: "#dfe7ed"
    property color mutedColor: "#91a0ab"
    property bool reviewChoicesValid: false
    property string reviewStateToken: ""
    readonly property bool hasCandidate: serviceController
                                                 && serviceController.candidate
                                                 && serviceController.candidate.id !== undefined
    readonly property var candidateEvidence: hasCandidate
                                                 && serviceController.candidate.evidence
                                             ? serviceController.candidate.evidence
                                             : []
    readonly property var firstEvidence: candidateEvidence.length > 0
                                             ? candidateEvidence[0]
                                             : ({})
    readonly property var boundPackage: executionController
                                            ? executionController.activePackage
                                            : ({})
    readonly property bool selectedPackageBound: serviceController
                                                   && serviceController.objectHash !== ""
                                                   && boundPackage.package_hash === serviceController.objectHash
    readonly property var displayedLimitations: limitationStatements()

    signal closeRequested()

    function abbreviated(value) {
        const text = String(value || "")
        return text.length > 24 ? text.substring(0, 15) + "…" + text.substring(text.length - 8) : text
    }

    function selectedFixtureId() {
        const roleValue = fixtureChoice.currentValue
        if (roleValue !== undefined && roleValue !== null
                && String(roleValue) !== "")
            return String(roleValue)
        if (!serviceController)
            return ""
        const choices = serviceController.fixtureChoices || []
        const index = fixtureChoice.currentIndex
        if (index < 0 || index >= choices.length)
            return ""
        const choice = choices[index]
        if (!choice || choice.fixture_id === undefined
                || choice.fixture_id === null)
            return ""
        return String(choice.fixture_id)
    }

    function renderEngineeringValue(parameter) {
        const claim = parameter.selected_claim || {}
        const value = claim.value || {}
        const unit = claim.unit || ""
        if (value.kind === "scalar")
            return String(value.value) + " " + unit
        if (value.kind === "range")
            return String(value.minimum) + "–" + String(value.maximum) + " " + unit
        if (value.kind === "enumeration")
            return (value.values || []).join(", ")
        if (value.kind === "curve")
            return (value.points || []).length + " reviewed points (" + value.independent_unit + ")"
        if (value.kind === "unknown")
            return "Unknown: " + value.reason
        return "Unsupported value"
    }

    function limitationStatements() {
        const result = []
        const packageItems = boundPackage.limitations || []
        const candidateItems = serviceController
                ? serviceController.candidate.limitations || [] : []
        const evidenceItems = firstEvidence.limitations || []
        for (let index = 0; index < packageItems.length; ++index)
            if (!result.includes(String(packageItems[index])))
                result.push(String(packageItems[index]))
        for (let index = 0; index < candidateItems.length; ++index) {
            const statement = String(candidateItems[index].statement || "")
            if (statement !== "" && !result.includes(statement))
                result.push(statement)
        }
        for (let index = 0; index < evidenceItems.length; ++index)
            if (!result.includes(String(evidenceItems[index])))
                result.push(String(evidenceItems[index]))
        return result
    }

    function resetReviewChoices() {
        reviewChoices.clear()
        if (!serviceController)
            return
        for (let index = 0; index < serviceController.parameters.length; ++index) {
            const claim = serviceController.parameters[index].selected_claim || {}
            reviewChoices.append({
                                     "claim_id": claim.claim_id || "",
                                     "status": "",
                                     "note": ""
                                 })
        }
        validateReviewChoices()
    }

    function validateReviewChoices() {
        let valid = reviewerField.text.trim() !== ""
                && serviceController
                && reviewChoices.count === serviceController.parameters.length
                && reviewChoices.count > 0
        for (let index = 0; index < reviewChoices.count; ++index) {
            const row = reviewChoices.get(index)
            if (row.claim_id === "" || row.status === "" || row.note.trim() === "") {
                valid = false
                break
            }
        }
        reviewChoicesValid = valid
    }

    function recordReviewDecision(rowIndex, choiceIndex) {
        const row = Number(rowIndex)
        const choice = Number(choiceIndex)
        if (!Number.isInteger(row) || row < 0 || row >= reviewChoices.count)
            return
        const status = choice === 1 ? "accepted"
                     : choice === 2 ? "ambiguous"
                       : choice === 3 ? "rejected" : ""
        reviewChoices.setProperty(row, "status", status)
        validateReviewChoices()
    }

    function recordReviewNote(rowIndex, note) {
        const row = Number(rowIndex)
        if (!Number.isInteger(row) || row < 0 || row >= reviewChoices.count)
            return
        reviewChoices.setProperty(row, "note", String(note))
        validateReviewChoices()
    }

    function reviewDecisionPayload() {
        const result = []
        for (let index = 0; index < reviewChoices.count; ++index) {
            const row = reviewChoices.get(index)
            result.push({
                            "claim_id": row.claim_id,
                            "status": row.status,
                            "note": row.note
                        })
        }
        return result
    }

    ListModel {
        id: reviewChoices
    }

    Connections {
        target: root.serviceController
        function onChanged() {
            if (!root.serviceController)
                return
            const candidateId = root.serviceController.candidate.id
            if (candidateId === undefined) {
                root.reviewStateToken = ""
                reviewChoices.clear()
                reviewerField.text = ""
                root.validateReviewChoices()
                return
            }
            const nextToken = String(candidateId) + ":" + String(root.serviceController.draftVersion)
            if (nextToken !== root.reviewStateToken) {
                root.reviewStateToken = nextToken
                reviewerField.text = ""
                root.resetReviewChoices()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Label {
                    text: "COMPONENT EVIDENCE → EXACT PACKAGE"
                    color: root.mutedColor
                    font.bold: true
                    font.pixelSize: 11
                }
                Label {
                    objectName: "componentIdentityValue"
                    text: root.boundPackage.component_id !== undefined
                          ? (root.boundPackage.manufacturer + " " + root.boundPackage.part_number)
                          : root.hasCandidate
                            ? ((root.serviceController.candidate.manufacturer || "") + " "
                               + (root.serviceController.candidate.part_number || ""))
                            : "Choose a fixed Program 01B input"
                    color: root.textColor
                    font.pixelSize: 23
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Close"
                onClicked: root.closeRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.lineColor
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 82
            color: "#1b2228"
            border.color: root.lineColor
            radius: 3
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 18
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: "TARGET ENTITY"; color: root.mutedColor; font.bold: true; font.pixelSize: 10 }
                    Label {
                        text: root.selectedEntityName !== "" ? root.selectedEntityName : "No CAD entity selected"
                        color: root.selectedEntityId !== "" ? root.textColor : "#e0ac62"
                        font.bold: true
                    }
                    Label { text: root.abbreviated(root.selectedEntityId); color: root.mutedColor; font.pixelSize: 10 }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: "BOUND IMMUTABLE PACKAGE"; color: root.mutedColor; font.bold: true; font.pixelSize: 10 }
                    Label {
                        text: root.boundPackage.package_hash !== undefined
                              ? root.abbreviated(root.boundPackage.package_hash)
                              : "None"
                        color: root.boundPackage.package_hash !== undefined ? "#83b6d5" : root.mutedColor
                    }
                    Label {
                        text: root.boundPackage.execution_readiness !== undefined
                              ? "Readiness: " + root.boundPackage.execution_readiness
                              : "Acquire reviewed exact bytes to bind"
                        color: root.boundPackage.execution_readiness === "ready" ? "#70c99a" : "#e0ac62"
                        font.pixelSize: 10
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: !root.hasCandidate
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: "FIXED PROGRAM 01B CATALOG"; color: root.mutedColor; font.bold: true; font.pixelSize: 10 }
                ComboBox {
                    id: fixtureChoice
                    Layout.fillWidth: true
                    model: root.serviceController ? root.serviceController.fixtureChoices : []
                    textRole: "label"
                    valueRole: "fixture_id"
                }
                Label {
                    text: "Motor A, Motor B, and the retained blocked conformance package are explicit identities—not a free-form search."
                    color: root.mutedColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            Button {
                text: "Load evidence"
                enabled: root.serviceController && !root.serviceController.busy
                         && root.selectedFixtureId() !== ""
                Layout.alignment: Qt.AlignBottom
                onClicked: {
                    const fixtureId = root.selectedFixtureId()
                    if (fixtureId !== "")
                        root.serviceController.loadFixture(fixtureId)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.serviceController && root.serviceController.busy
            BusyIndicator { running: true; Layout.alignment: Qt.AlignHCenter }
            Label {
                text: root.serviceController.status === "acquiring_exact_package"
                      ? "Acquiring and independently verifying exact published bytes…"
                      : "Loading evidence and selected claims…"
                color: root.mutedColor
                Layout.alignment: Qt.AlignHCenter
            }
        }

        Label {
            visible: root.serviceController && root.serviceController.error !== ""
            text: root.serviceController
                  ? root.serviceController.error
                    + (root.serviceController.errorCode !== "" ? " [" + root.serviceController.errorCode + "]" : "")
                  : ""
            color: "#e87972"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        GridLayout {
            Layout.fillWidth: true
            visible: root.hasCandidate
            columns: 4
            columnSpacing: 18
            rowSpacing: 4
            Label { text: "Revision"; color: root.mutedColor }
            Label { text: root.serviceController.candidate.revision || "unknown"; color: root.textColor }
            Label { text: "Evidence class"; color: root.mutedColor }
            Label { text: root.firstEvidence.evidence_class || "not reported"; color: root.textColor }
            Label { text: "Source authority"; color: root.mutedColor }
            Label { text: root.firstEvidence.source_authority || "not reported"; color: root.textColor }
            Label { text: "Physical validation"; color: root.mutedColor }
            Label { text: root.firstEvidence.physical_validation_status || "not reported"; color: "#e0ac62" }
            Label { text: "Publication"; color: root.mutedColor }
            Label { text: root.serviceController.publicationIntegrity || "draft"; color: "#83b6d5" }
            Label { text: "Capability"; color: root.mutedColor }
            Label {
                text: root.serviceController.candidate.capability_id || root.boundPackage.capability_id || "not compiled"
                color: root.textColor
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Label { text: "Readiness"; color: root.mutedColor }
            Label {
                text: root.boundPackage.execution_readiness
                      || root.serviceController.executionReadiness
                      || "not evaluated"
                color: text === "ready" ? "#70c99a"
                       : text === "blocked" ? "#e0ac62" : root.mutedColor
            }
            Label { text: "Object hash"; color: root.mutedColor }
            Label {
                text: root.abbreviated(root.boundPackage.package_hash
                                       || root.serviceController.objectHash)
                      || "not published"
                color: "#83b6d5"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: limitationContent.implicitHeight + 18
            visible: root.displayedLimitations.length > 0
            color: "#302b21"
            border.color: "#725d35"
            radius: 3
            ColumnLayout {
                id: limitationContent
                anchors.fill: parent
                anchors.margins: 9
                Label { text: "KNOWN LIMITATIONS"; color: "#e0ac62"; font.bold: true; font.pixelSize: 10 }
                Repeater {
                    model: root.displayedLimitations
                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true
                        text: "• " + modelData
                        color: "#d7c39d"
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        Label {
            visible: root.serviceController && root.serviceController.parameters.length > 0
            text: "REVIEWED CLAIMS — COMPONENT PROPERTIES ARE READ-ONLY"
            color: root.mutedColor
            font.bold: true
            font.pixelSize: 11
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.serviceController ? root.serviceController.parameters : []
            spacing: 3
            delegate: Rectangle {
                id: claimRow
                required property int index
                required property var modelData
                width: ListView.view.width
                height: 120
                color: index % 2 ? "#1e252b" : "#1b2228"
                border.color: "#303b44"
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 10
                    ColumnLayout {
                        Layout.preferredWidth: 220
                        Label { text: String(modelData.name).replace(/_/g, " "); color: root.textColor; font.bold: true }
                        Label {
                            text: root.renderEngineeringValue(modelData)
                            color: "#9fd1eb"
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Label {
                            text: String((modelData.selected_claim || {}).provenance || "").replace(/_/g, " ")
                            color: "#77c49b"
                            font.pixelSize: 10
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Claim " + String((modelData.selected_claim || {}).claim_id || "")
                            color: root.mutedColor
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            text: "Fingerprint " + String((modelData.selected_claim || {}).claim_fingerprint || "")
                            color: "#83b6d5"
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            text: "Evidence records: " + ((modelData.selected_claim || {}).evidence_ids || []).length
                            color: root.mutedColor
                            font.pixelSize: 10
                        }
                    }
                    ColumnLayout {
                        Layout.preferredWidth: 220
                        ComboBox {
                            Layout.fillWidth: true
                            model: ["Select decision", "Accept", "Ambiguous", "Reject"]
                            enabled: root.serviceController.status === "draft" && !root.serviceController.busy
                            currentIndex: reviewChoices.count > claimRow.index
                                          ? reviewChoices.get(claimRow.index).status === "accepted" ? 1
                                            : reviewChoices.get(claimRow.index).status === "ambiguous" ? 2
                                              : reviewChoices.get(claimRow.index).status === "rejected" ? 3 : 0
                                          : 0
                            onActivated: function(choiceIndex) {
                                root.recordReviewDecision(claimRow.index, choiceIndex)
                            }
                        }
                        TextField {
                            Layout.fillWidth: true
                            placeholderText: "Required review note"
                            enabled: root.serviceController.status === "draft" && !root.serviceController.busy
                            text: reviewChoices.count > claimRow.index
                                  ? reviewChoices.get(claimRow.index).note : ""
                            onTextEdited: {
                                root.recordReviewNote(claimRow.index, text)
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.serviceController
                     && root.serviceController.parameters.length > 0
                     && root.serviceController.status === "draft"
            Label { text: "Reviewer"; color: root.mutedColor }
            TextField {
                id: reviewerField
                Layout.preferredWidth: 240
                placeholderText: "Your name"
                enabled: !root.serviceController.busy
                onTextChanged: root.validateReviewChoices()
            }
            Label {
                text: root.reviewChoicesValid
                      ? "Every claim has an explicit decision and note"
                      : "Every claim requires a decision and nonblank note."
                color: root.reviewChoicesValid ? "#70c99a" : "#e0ac62"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            visible: root.boundPackage.execution_readiness === "blocked"
            color: "#302b21"
            border.color: "#725d35"
            Label {
                anchors.fill: parent
                anchors.margins: 9
                text: root.boundPackage.blocked_reason || "The reviewed execution gate is blocked."
                color: "#e0ac62"
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignVCenter
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: "Submit review"
                visible: root.serviceController
                         && root.serviceController.parameters.length > 0
                         && root.serviceController.status === "draft"
                enabled: root.reviewChoicesValid && !root.serviceController.busy
                onClicked: root.serviceController.submitReview(root.reviewDecisionPayload(), reviewerField.text)
            }
            Button {
                text: "Publish reviewed input"
                visible: root.serviceController
                         && root.serviceController.parameters.length > 0
                         && root.serviceController.status === "reviewed"
                enabled: !root.serviceController.busy
                onClicked: root.serviceController.publish()
            }
            Button {
                text: root.selectedPackageBound
                      ? "Exact package bound"
                      : root.serviceController && root.serviceController.status === "exact_package_ready"
                        ? "Reacquire exact package and bind"
                        : "Acquire exact package and bind"
                highlighted: true
                visible: root.serviceController
                         && root.serviceController.publicationIntegrity === "sealed_v2"
                enabled: root.selectedEntityId !== ""
                         && !root.serviceController.busy
                         && !root.selectedPackageBound
                onClicked: root.serviceController.acquireExactPackage()
            }
        }
    }
}
