pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick3D

Item {
    id: root
    objectName: "structuralSetupPanel"

    property var controller
    property color panelColor: "#20262c"
    property color lineColor: "#35404a"
    property color textColor: "#dfe7ed"
    property color mutedColor: "#91a0ab"
    property real orbitX: -24
    property real orbitY: -38
    readonly property real sceneDiameter: controller && controller.displayDiameterM > 0
                                                 ? controller.displayDiameterM : 1
    readonly property var sceneCenter: controller && controller.displayCenterM.length === 3
                                           ? controller.displayCenterM : [0, 0, 0]

    function conciseHash(value) {
        const text = String(value || "")
        return text.length > 28
                ? text.substring(0, 17) + "…" + text.substring(text.length - 8)
                : text
    }

    function engineeringValue(value) {
        const number = Number(value)
        return Number.isFinite(number) ? number.toExponential(4) : "—"
    }

    function vectorValue(value) {
        if (!value || value.length !== 3)
            return "—"
        return "[" + engineeringValue(value[0]) + ", "
                + engineeringValue(value[1]) + ", "
                + engineeringValue(value[2]) + "]"
    }

    function selectedMaterialValue(key) {
        if (!controller || structuralMaterialCandidate.currentIndex < 0
                || structuralMaterialCandidate.currentIndex
                   >= controller.materialCandidates.length)
            return ""
        const candidate = controller.materialCandidates[
                    structuralMaterialCandidate.currentIndex]
        const value = candidate ? candidate[key] : undefined
        return value === undefined || value === null ? "" : String(value)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 480
            color: "#111820"
            border.color: root.lineColor
            radius: 4

            View3D {
                id: structuralMeshViewport
                objectName: "structuralMeshViewport"
                anchors.fill: parent
                anchors.margins: 1
                camera: structuralCamera
                environment: SceneEnvironment {
                    clearColor: "#111820"
                    backgroundMode: SceneEnvironment.Color
                    antialiasingMode: SceneEnvironment.MSAA
                    antialiasingQuality: SceneEnvironment.High
                    tonemapMode: SceneEnvironment.TonemapModeFilmic
                }

                Node {
                    eulerRotation.x: root.orbitX
                    eulerRotation.y: root.orbitY
                    PerspectiveCamera {
                        id: structuralCamera
                        z: root.sceneDiameter * 1.7
                        clipNear: root.sceneDiameter * 0.001
                        clipFar: root.sceneDiameter * 20
                    }
                }

                DirectionalLight {
                    eulerRotation.x: -35
                    eulerRotation.y: -30
                    brightness: 1.25
                    castsShadow: true
                }
                DirectionalLight {
                    eulerRotation.x: 35
                    eulerRotation.y: 145
                    brightness: 0.55
                    color: "#9fc8e7"
                }

                Node {
                    position: Qt.vector3d(-root.sceneCenter[0],
                                          -root.sceneCenter[1],
                                          -root.sceneCenter[2])
                    Model {
                        geometry: root.controller ? root.controller.meshGeometry : null
                        materials: PrincipledMaterial {
                            baseColor: "#7894a8"
                            metalness: 0.05
                            roughness: 0.55
                        }
                    }
                    Model {
                        geometry: root.controller ? root.controller.highlightGeometry : null
                        scale: Qt.vector3d(1.006, 1.006, 1.006)
                        opacity: 0.86
                        castsShadows: false
                        materials: PrincipledMaterial {
                            baseColor: "#e69a45"
                            lighting: PrincipledMaterial.NoLighting
                        }
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 12
                width: viewportStatus.implicitWidth + 20
                height: viewportStatus.implicitHeight + 12
                color: "#151c22dd"
                border.color: root.lineColor
                radius: 3
                Label {
                    id: viewportStatus
                    anchors.centerIn: parent
                    text: root.controller && root.controller.nodeCount > 0
                          ? root.controller.nodeCount + " nodes  •  "
                            + root.controller.elementCount + " tetrahedra"
                          : "No structural mesh loaded"
                    color: root.textColor
                    font.pixelSize: 11
                }
            }

            Row {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 12
                spacing: 6
                Button {
                    text: "Isometric"
                    onClicked: {
                        root.orbitX = -24
                        root.orbitY = -38
                    }
                }
                Button {
                    text: "Front"
                    onClicked: {
                        root.orbitX = 0
                        root.orbitY = 0
                    }
                }
                Button {
                    text: "Top"
                    onClicked: {
                        root.orbitX = -89
                        root.orbitY = 0
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 610
            Layout.fillHeight: true
            color: root.panelColor
            border.color: root.lineColor
            radius: 4

            ScrollView {
                id: setupScroll
                anchors.fill: parent
                anchors.margins: 12
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: setupScroll.availableWidth
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                text: "REVIEWED LINEAR-STATIC SETUP"
                                color: root.mutedColor
                                font.bold: true
                                font.pixelSize: 11
                            }
                            Label {
                                text: "Mesh, boundaries, material, load, limits"
                                color: root.textColor
                                font.pixelSize: 21
                            }
                        }
                        Button {
                            id: structuralCandidateFile
                            objectName: "structuralCandidateFile"
                            text: "Load candidate…"
                            onClicked: candidateDialog.open()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.controller && root.controller.sourcePath !== ""
                              ? root.controller.sourcePath : "No candidate selected"
                        color: root.mutedColor
                        elide: Text.ElideMiddle
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.controller
                              ? "Geometry " + root.conciseHash(root.controller.geometrySha256)
                                + "  •  minimum mean ratio "
                                + root.engineeringValue(root.controller.minimumMeanRatio)
                              : ""
                        color: "#83b6d5"
                        font.pixelSize: 10
                    }

                    GroupBox {
                        title: "1 · Named boundary faces"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            Label {
                                Layout.fillWidth: true
                                text: "Select explicit mesh surface groups. The orange overlay is the active group."
                                color: root.mutedColor
                                wrapMode: Text.WordWrap
                            }
                            ListView {
                                id: structuralSurfaceList
                                objectName: "structuralSurfaceList"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 264
                                clip: true
                                spacing: 6
                                model: root.controller ? root.controller.surfaceGroups : []
                                delegate: Rectangle {
                                    id: surfaceRow
                                    required property var modelData
                                    width: structuralSurfaceList.width
                                    height: 126
                                    color: root.controller
                                           && root.controller.activeSurfaceGroup.name === surfaceRow.modelData.name
                                           ? "#2d3c46" : "#1b2228"
                                    border.color: root.controller
                                                  && root.controller.activeSurfaceGroup.name === surfaceRow.modelData.name
                                                  ? "#d98a3f" : root.lineColor
                                    radius: 3
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 3
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Label {
                                                Layout.fillWidth: true
                                                text: surfaceRow.modelData.name
                                                color: root.textColor
                                                font.bold: true
                                            }
                                            Label {
                                                text: surfaceRow.modelData.triangle_count + " triangles"
                                                color: root.mutedColor
                                            }
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: "Area " + root.engineeringValue(surfaceRow.modelData.area_m2)
                                                  + " m²  •  centroid "
                                                  + root.vectorValue(surfaceRow.modelData.centroid_m) + " m"
                                            color: root.mutedColor
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: "Normal " + root.vectorValue(surfaceRow.modelData.normal_m)
                                                  + (surfaceRow.modelData.normal_defined ? "" : " (not uniquely defined)")
                                            color: root.mutedColor
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                        }
                                        RowLayout {
                                            CheckBox {
                                                objectName: "structuralRestraintToggle"
                                                text: "Fully fixed restraint"
                                                checked: Boolean(surfaceRow.modelData.restrained)
                                                onClicked: if (root.controller)
                                                    root.controller.setSurfaceRole(
                                                                surfaceRow.modelData.name,
                                                                "restraint", checked)
                                            }
                                            CheckBox {
                                                objectName: "structuralLoadToggle"
                                                text: "Load surface"
                                                checked: Boolean(surfaceRow.modelData.loaded)
                                                onClicked: if (root.controller)
                                                    root.controller.setSurfaceRole(
                                                                surfaceRow.modelData.name,
                                                                "load", checked)
                                            }
                                            Item { Layout.fillWidth: true }
                                            Button {
                                                text: "Inspect"
                                                flat: true
                                                onClicked: if (root.controller)
                                                    root.controller.setActiveSurfaceGroup(
                                                                surfaceRow.modelData.name)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    GroupBox {
                        title: "2 · Material evidence"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            RowLayout {
                                Layout.fillWidth: true
                                Button {
                                    id: structuralMaterialEvidence
                                    objectName: "structuralMaterialEvidence"
                                    text: "Load evidence…"
                                    onClicked: materialDialog.open()
                                }
                                ComboBox {
                                    id: structuralMaterialCandidate
                                    objectName: "structuralMaterialCandidate"
                                    Layout.fillWidth: true
                                    model: root.controller
                                           ? root.controller.materialCandidates : []
                                    textRole: "designation"
                                    valueRole: "candidate_id"
                                    displayText: currentIndex >= 0
                                                 ? currentText : "No evidence candidate"
                                }
                                ComboBox {
                                    id: materialApplicability
                                    model: [
                                        {"label": "Applicability unresolved", "value": ""},
                                        {"label": "Known for this part", "value": "known"},
                                        {"label": "Assumed for this part", "value": "assumed"}
                                    ]
                                    textRole: "label"
                                    valueRole: "value"
                                }
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                Label { text: "Designation"; color: root.mutedColor }
                                TextField {
                                    id: structuralMaterialDesignation
                                    objectName: "structuralMaterialDesignation"
                                    Layout.fillWidth: true
                                    readOnly: true
                                    text: root.selectedMaterialValue("designation")
                                }
                                Label { text: "Temper"; color: root.mutedColor }
                                TextField {
                                    id: structuralTemper
                                    objectName: "structuralTemper"
                                    Layout.fillWidth: true
                                    readOnly: true
                                    text: root.selectedMaterialValue("temper")
                                }
                                Label { text: "Product form"; color: root.mutedColor }
                                TextField {
                                    id: structuralProductForm
                                    objectName: "structuralProductForm"
                                    Layout.fillWidth: true
                                    readOnly: true
                                    text: root.selectedMaterialValue("product_form")
                                }
                                Label { text: "Young's modulus (Pa)"; color: root.mutedColor }
                                TextField {
                                    id: structuralYoungsModulus
                                    objectName: "structuralYoungsModulus"
                                    Layout.fillWidth: true
                                    readOnly: true
                                    text: root.selectedMaterialValue("youngs_modulus_pa")
                                }
                                Label { text: "Poisson ratio"; color: root.mutedColor }
                                TextField {
                                    id: structuralPoissonRatio
                                    objectName: "structuralPoissonRatio"
                                    Layout.fillWidth: true
                                    readOnly: true
                                    text: root.selectedMaterialValue("poisson_ratio")
                                }
                            }
                            Button {
                                text: "Review selected material"
                                enabled: structuralMaterialCandidate.currentIndex >= 0
                                         && materialApplicability.currentIndex > 0
                                onClicked: if (root.controller)
                                    root.controller.selectMaterialCandidate(
                                                String(structuralMaterialCandidate.currentValue),
                                                String(materialApplicability.currentValue))
                            }
                        }
                    }

                    GroupBox {
                        title: "3 · Load and requirements"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            Label {
                                Layout.fillWidth: true
                                text: "The total force is applied as uniform traction over the selected load groups and deterministically distributed to their mesh nodes."
                                color: root.mutedColor
                                wrapMode: Text.WordWrap
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 4
                                Label { text: "Total force (N)"; color: root.mutedColor }
                                TextField {
                                    id: structuralForceMagnitude
                                    objectName: "structuralForceMagnitude"
                                    placeholderText: "100"
                                    validator: DoubleValidator {
                                        bottom: 0
                                        notation: DoubleValidator.ScientificNotation
                                    }
                                }
                                Label { text: "Direction X / Y / Z"; color: root.mutedColor }
                                RowLayout {
                                    TextField {
                                        id: structuralForceDirectionX
                                        objectName: "structuralForceDirectionX"
                                        Layout.preferredWidth: 70
                                        text: "0"
                                        validator: DoubleValidator {
                                            notation: DoubleValidator.ScientificNotation
                                        }
                                    }
                                    TextField {
                                        id: structuralForceDirectionY
                                        objectName: "structuralForceDirectionY"
                                        Layout.preferredWidth: 70
                                        text: "0"
                                        validator: DoubleValidator {
                                            notation: DoubleValidator.ScientificNotation
                                        }
                                    }
                                    TextField {
                                        id: structuralForceDirectionZ
                                        objectName: "structuralForceDirectionZ"
                                        Layout.preferredWidth: 70
                                        text: "-1"
                                        validator: DoubleValidator {
                                            notation: DoubleValidator.ScientificNotation
                                        }
                                    }
                                }
                            }
                            Button {
                                text: "Review force"
                                onClicked: if (root.controller)
                                    root.controller.setForce(
                                                Number(structuralForceMagnitude.text),
                                                Number(structuralForceDirectionX.text),
                                                Number(structuralForceDirectionY.text),
                                                Number(structuralForceDirectionZ.text))
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                Label { text: "Displacement limit (m)"; color: root.mutedColor }
                                TextField {
                                    id: structuralDisplacementLimit
                                    objectName: "structuralDisplacementLimit"
                                    Layout.fillWidth: true
                                    placeholderText: "Optional"
                                    validator: DoubleValidator {
                                        bottom: 0
                                        notation: DoubleValidator.ScientificNotation
                                    }
                                }
                                Label { text: "Displacement-limit basis"; color: root.mutedColor }
                                TextField {
                                    id: displacementLimitBasis
                                    Layout.fillWidth: true
                                    placeholderText: "Requirement, drawing, or reviewed assumption"
                                }
                                Label { text: "Von Mises stress limit (Pa)"; color: root.mutedColor }
                                TextField {
                                    id: structuralStressLimit
                                    objectName: "structuralStressLimit"
                                    Layout.fillWidth: true
                                    placeholderText: "Optional"
                                    validator: DoubleValidator {
                                        bottom: 0
                                        notation: DoubleValidator.ScientificNotation
                                    }
                                }
                                Label { text: "Stress-limit basis"; color: root.mutedColor }
                                TextField {
                                    id: stressLimitBasis
                                    Layout.fillWidth: true
                                    placeholderText: "Requirement, allowable, or reviewed assumption"
                                }
                            }
                            Button {
                                text: "Review limits"
                                onClicked: if (root.controller)
                                    root.controller.setLimits({
                                        "displacement_limit_m": structuralDisplacementLimit.text === ""
                                                                ? null : Number(structuralDisplacementLimit.text),
                                        "displacement_limit_basis": displacementLimitBasis.text,
                                        "von_mises_limit_pa": structuralStressLimit.text === ""
                                                              ? null : Number(structuralStressLimit.text),
                                        "von_mises_limit_basis": stressLimitBasis.text
                                    })
                            }
                            Label {
                                Layout.fillWidth: true
                                text: root.controller
                                      ? "Selected area: "
                                        + root.engineeringValue(root.controller.selectedLoadAreaM2)
                                        + " m²  •  compiled resultant: "
                                        + root.vectorValue(root.controller.compiledResultantN) + " N"
                                      : ""
                                color: "#83b6d5"
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    GroupBox {
                        title: "4 · Mesh and complete scenario review"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            Label {
                                Layout.fillWidth: true
                                text: root.controller
                                      ? "Verified candidate target size: "
                                        + root.engineeringValue(root.controller.candidateMeshTargetSizeM)
                                        + " m  •  observed minimum mean ratio: "
                                        + root.engineeringValue(root.controller.minimumMeanRatio)
                                      : ""
                                color: root.mutedColor
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                Label { text: "Required minimum mean ratio"; color: root.mutedColor }
                                TextField {
                                    id: minimumMeanRatioThreshold
                                    Layout.preferredWidth: 120
                                    text: "0.05"
                                    validator: DoubleValidator {
                                        bottom: 0
                                        top: 1
                                        notation: DoubleValidator.ScientificNotation
                                    }
                                    onEditingFinished: if (structuralMeshConfirmation.checked
                                                             && root.controller)
                                        root.controller.setMeshReview({
                                            "target_size_m": root.controller.candidateMeshTargetSizeM,
                                            "minimum_mean_ratio_threshold": Number(text),
                                            "confirmed": true
                                        })
                                }
                                Item { Layout.fillWidth: true }
                                CheckBox {
                                    id: structuralMeshConfirmation
                                    objectName: "structuralMeshConfirmation"
                                    text: "I reviewed this mesh and quality floor"
                                    checked: root.controller
                                             ? root.controller.meshReviewed : false
                                    onClicked: if (root.controller)
                                        root.controller.setMeshReview({
                                            "target_size_m": root.controller.candidateMeshTargetSizeM,
                                            "minimum_mean_ratio_threshold": Number(minimumMeanRatioThreshold.text),
                                            "confirmed": checked
                                        })
                                }
                            }
                            CheckBox {
                                id: structuralScenarioConfirmation
                                objectName: "structuralScenarioConfirmation"
                                text: "I confirm the complete material, boundaries, force, limits, and mesh scenario"
                                checked: root.controller
                                         ? root.controller.scenarioConfirmed : false
                                onClicked: if (root.controller)
                                    root.controller.confirmScenario(checked)
                            }
                        }
                    }

                    GroupBox {
                        title: "Blocking issues"
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.fill: parent
                            ListView {
                                id: structuralBlockingList
                                objectName: "structuralBlockingList"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 150
                                clip: true
                                spacing: 5
                                model: root.controller
                                       ? root.controller.blockingIssues : []
                                delegate: Rectangle {
                                    id: blockingRow
                                    required property var modelData
                                    width: structuralBlockingList.width
                                    height: issueMessage.implicitHeight + 18
                                    color: "#2b2220"
                                    border.color: "#765044"
                                    radius: 3
                                    Label {
                                        id: issueMessage
                                        anchors.fill: parent
                                        anchors.margins: 9
                                        text: blockingRow.modelData.code + " — "
                                              + blockingRow.modelData.message
                                        color: "#e7b48a"
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }

                    Button {
                        id: structuralExportButton
                        objectName: "structuralExportButton"
                        Layout.fillWidth: true
                        text: root.controller && root.controller.readyToExport
                              ? "Export reviewed structural case…"
                              : "Resolve every blocking issue before export"
                        enabled: root.controller
                                 ? root.controller.readyToExport : false
                        highlighted: enabled
                        onClicked: exportDialog.open()
                    }
                }
            }
        }
    }

    FileDialog {
        id: candidateDialog
        title: "Load structural candidate manifest"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Structural candidate (*.json)"]
        onAccepted: if (root.controller)
            root.controller.loadCandidate(selectedFile)
    }
    FileDialog {
        id: materialDialog
        title: "Load material evidence"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Material evidence (*.json)"]
        onAccepted: if (root.controller)
            root.controller.loadMaterialEvidence(selectedFile)
    }
    FolderDialog {
        id: exportDialog
        title: "Export reviewed structural case"
        onAccepted: if (root.controller)
            root.controller.exportReviewedCase(selectedFolder)
    }
}
