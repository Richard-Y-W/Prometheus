import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick3D

Item {
    id: root
    required property var structuralController
    property color panelColor: "#20262c"
    property color lineColor: "#35404a"
    property color textColor: "#dfe7ed"
    property color mutedColor: "#91a0ab"
    signal closeRequested()
    property url calculixExecutable
    property url outputRoot
    property string appliedRestoredManifest
    readonly property bool workflowAccepted: structuralController.status === "comparison_accepted" ||
                                              structuralController.status === "modal_run_archived"
    readonly property bool workflowFailed: structuralController.status.indexOf("failed") >= 0
    readonly property color workflowStatusColor: workflowAccepted ? "#70c99a" :
                                                   (workflowFailed ? "#e87972" : "#e0ac62")
    // Mirrors StructuralController::draft_resolves_modal_capability: a live,
    // non-authoritative routing signal so the UI can hide coarse/fine-only
    // controls before a review round-trip confirms the compiled capability.
    // The compiled request_preview_.capability (once reviewed) and
    // lastRun.capability (once run) remain the authoritative signals.
    readonly property bool modalDraft: Number(naturalFrequencyLimit.text) > 0 &&
                                        !(Number(displacementLimit.text) > 0) &&
                                        !(Number(stressLimit.text) > 0)
    readonly property bool modalActive: root.modalDraft ||
                                         structuralController.requestPreview.capability === "modal_frequency" ||
                                         structuralController.lastRun.capability === "modal_frequency"

    function invalidateLoadReview() {
        loadReviewed.checked = false
        loadCorrespondenceReviewed.checked = false
        if (!structuralController.sharedInputsLocked)
            scenarioConfirmed.checked = false
    }

    function invalidateRestraintReview() {
        restraintReviewed.checked = false
        restraintCorrespondenceReviewed.checked = false
        if (!structuralController.sharedInputsLocked)
            scenarioConfirmed.checked = false
    }

    function invalidateMaterialReview() {
        materialReviewed.checked = false
        if (!structuralController.sharedInputsLocked)
            scenarioConfirmed.checked = false
    }

    function invalidateRequirementReview() {
        requirementReviewed.checked = false
    }

    function invalidateMeshReview() {
        meshReviewed.checked = false
        if (!structuralController.sharedInputsLocked)
            scenarioConfirmed.checked = false
    }

    function setApplicability(value) {
        const normalized = value || "unresolved"
        const index = materialApplicability.model.indexOf(normalized)
        materialApplicability.currentIndex = index >= 0 ? index : 0
    }

    function applyMaterialDraft() {
        const draft = structuralController.setupDraft
        materialName.text = draft.material_designation || ""
        materialTemper.text = draft.material_temper || ""
        materialProductForm.text = draft.material_product_form || ""
        materialHash.text = draft.material_source_sha256 || ""
        setApplicability(draft.material_applicability)
        youngsModulus.text = String(draft.youngs_modulus_pa || 0)
        poissonRatio.text = String(draft.poisson_ratio || 0)
        densityKgM3.text = String(draft.density_kg_m3 || 0)
        materialReviewed.checked = false
        scenarioConfirmed.checked = false
    }

    function applyRestoredDraft() {
        const draft = structuralController.setupDraft
        const marker = structuralController.lastRun.archive_manifest || ""
        if (!draft.restored || !draft.analysis_id || !marker || appliedRestoredManifest === marker)
            return
        appliedRestoredManifest = marker
        analysisId.text = draft.analysis_id
        componentName.text = draft.component_name
        geometryHash.text = draft.geometry_sha256
        materialName.text = draft.material_designation
        materialTemper.text = draft.material_temper
        materialProductForm.text = draft.material_product_form
        materialHash.text = draft.material_source_sha256
        setApplicability(draft.material_applicability)
        youngsModulus.text = String(draft.youngs_modulus_pa)
        poissonRatio.text = String(draft.poisson_ratio)
        densityKgM3.text = String(draft.density_kg_m3 || 0)
        materialReviewed.checked = draft.material_reviewed
        forceX.text = String(draft.force_x_n)
        forceY.text = String(draft.force_y_n)
        forceZ.text = String(draft.force_z_n)
        loadReviewed.checked = draft.load_reviewed
        restraintReviewed.checked = draft.restraint_reviewed
        displacementLimit.text = String(draft.displacement_limit_m || 0)
        stressLimit.text = String(draft.von_mises_limit_pa || 0)
        displacementLimitBasis.text = draft.displacement_limit_basis
        stressLimitBasis.text = draft.von_mises_limit_basis
        naturalFrequencyLimit.text = String(draft.natural_frequency_limit_hz || 0)
        naturalFrequencyLimitBasis.text = draft.natural_frequency_limit_basis || ""
        requirementRationale.text = draft.requirement_rationale
        requirementReviewed.checked = draft.requirement_reviewed
        requirementApplicability.text = draft.requirement_applicability || ""
        requirementCriticality.currentIndex = Math.max(0,
            requirementCriticality.model.indexOf(draft.requirement_criticality || "advisory"))
        otherRequirementDescription.text = draft.other_requirement_description || ""
        otherRequirementUnit.text = draft.other_requirement_unit || ""
        otherRequirementLimit.text = String(draft.other_requirement_limit_value || 0)
        meshMinimum.text = String(draft.mesh_minimum_size_m)
        meshMaximum.text = String(draft.mesh_maximum_size_m)
        meshTarget.text = String(draft.mesh_target_size_m)
        minimumMeanRatioThreshold.text = String(draft.minimum_mean_ratio_threshold)
        mesherIdentity.text = draft.mesher_identity
        meshReviewed.checked = draft.mesh_controls_reviewed
        scenarioDescription.text = draft.scenario_description
        scenarioConfirmed.checked = draft.scenario_confirmed
        refinementMaximumChange.text = String(draft.refinement_maximum_change_fraction || 0.10)
        loadCorrespondenceReviewed.checked = draft.boundary_load_correspondence_reviewed || false
        restraintCorrespondenceReviewed.checked = draft.boundary_restraint_correspondence_reviewed || false
        coordinateScale.text = String(structuralController.meshSummary.coordinate_scale_to_m || 1)
        patchAngle.text = String(structuralController.meshSummary.patch_angle_degrees || 15)
    }

    Connections {
        target: structuralController
        function onChanged() { root.applyRestoredDraft() }
    }

    function submitReview() {
        structuralController.reviewSetup({
            analysis_id: analysisId.text,
            component_name: componentName.text,
            geometry_sha256: geometryHash.text,
            material_designation: materialName.text,
            material_temper: materialTemper.text,
            material_product_form: materialProductForm.text,
            material_source_sha256: materialHash.text,
            material_applicability: materialApplicability.currentText,
            youngs_modulus_pa: Number(youngsModulus.text),
            poisson_ratio: Number(poissonRatio.text),
            density_kg_m3: Number(densityKgM3.text),
            material_reviewed: materialReviewed.checked,
            force_x_n: Number(forceX.text), force_y_n: Number(forceY.text), force_z_n: Number(forceZ.text),
            load_reviewed: loadReviewed.checked,
            restraint_reviewed: restraintReviewed.checked,
            displacement_limit_m: Number(displacementLimit.text),
            von_mises_limit_pa: Number(stressLimit.text),
            displacement_limit_basis: displacementLimitBasis.text,
            von_mises_limit_basis: stressLimitBasis.text,
            natural_frequency_limit_hz: Number(naturalFrequencyLimit.text),
            natural_frequency_limit_basis: naturalFrequencyLimitBasis.text,
            requirement_rationale: requirementRationale.text,
            requirement_reviewed: requirementReviewed.checked,
            requirement_applicability: requirementApplicability.text,
            requirement_criticality: requirementCriticality.currentText,
            other_requirement_description: otherRequirementDescription.text,
            other_requirement_unit: otherRequirementUnit.text,
            other_requirement_limit_value: Number(otherRequirementLimit.text),
            mesh_minimum_size_m: Number(meshMinimum.text),
            mesh_maximum_size_m: Number(meshMaximum.text),
            mesh_target_size_m: Number(meshTarget.text),
            minimum_mean_ratio_threshold: Number(minimumMeanRatioThreshold.text),
            mesher_identity: mesherIdentity.text,
            mesh_controls_reviewed: meshReviewed.checked,
            scenario_description: scenarioDescription.text,
            scenario_confirmed: scenarioConfirmed.checked,
            refinement_maximum_change_fraction: Number(refinementMaximumChange.text),
            boundary_load_correspondence_reviewed: loadCorrespondenceReviewed.checked,
            boundary_restraint_correspondence_reviewed: restraintCorrespondenceReviewed.checked
        });
    }

    FileDialog {
        id: meshDialog
        title: "Open Gmsh Abaqus tetrahedral mesh"
        nameFilters: ["Abaqus input mesh (*.inp)", "All files (*)"]
        onAccepted: {
            structuralController.loadMesh(selectedFile, Number(coordinateScale.text), Number(patchAngle.text))
            root.invalidateLoadReview()
            root.invalidateRestraintReview()
            root.invalidateMeshReview()
        }
    }
    FileDialog {
        id: materialEvidenceDialog
        title: "Open bounded material candidate evidence"
        nameFilters: ["Material evidence (*.json)", "JSON files (*.json)"]
        onAccepted: structuralController.loadMaterialEvidence(selectedFile)
    }
    FileDialog {
        id: calculixDialog
        title: "Select CalculiX executable"
        nameFilters: ["CalculiX executable (ccx*.exe)", "Executables (*.exe)", "All files (*)"]
        onAccepted: root.calculixExecutable = selectedFile
    }
    FolderDialog {
        id: outputDialog
        title: "Select structural run output folder"
        onAccepted: root.outputRoot = selectedFolder
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Label { text: root.modalActive ? "BOUNDED MODAL/FREQUENCY WORKFLOW" : "BOUNDED LINEAR-STATIC WORKFLOW"; color: mutedColor; font.bold: true; font.pixelSize: 11 }
                Label { text: "Structural setup review"; color: textColor; font.pixelSize: 23 }
                Label { text: "Status: " + structuralController.status; color: root.workflowStatusColor }
            }
            Item { Layout.fillWidth: true }
            Button { text: "×"; flat: true; onClicked: root.closeRequested() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: lineColor }
        Label {
            Layout.fillWidth: true
            visible: structuralController.error !== ""
            text: structuralController.error
            color: "#e87972"
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Frame {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                background: Rectangle { color: "#1b2228"; border.color: lineColor }
                ColumnLayout {
                    anchors.fill: parent
                    Label { text: "1  MESH AND SURFACES"; color: textColor; font.bold: true }
                    RowLayout {
                        Label { text: "Scale to m"; color: mutedColor }
                        TextField {
                            id: coordinateScale
                            text: "0.001"
                            Layout.fillWidth: true
                            validator: DoubleValidator { bottom: 0 }
                            onTextEdited: root.invalidateMeshReview()
                        }
                        Label { text: "Angle°"; color: mutedColor }
                        TextField {
                            id: patchAngle
                            text: "15"
                            Layout.preferredWidth: 55
                            validator: DoubleValidator { bottom: 0; top: 180 }
                            onEditingFinished: {
                                structuralController.setPatchAngle(Number(text))
                                root.invalidateLoadReview()
                                root.invalidateRestraintReview()
                            }
                        }
                    }
                    Button {
                        text: root.modalActive ? "Load mesh…" :
                              (structuralController.hasRefinementBaseline ?
                              "Load fine mesh…" : "Load coarse mesh…")
                        Layout.fillWidth: true
                        onClicked: meshDialog.open()
                    }
                    Label {
                        Layout.fillWidth: true
                        color: mutedColor
                        wrapMode: Text.WordWrap
                        text: structuralController.meshSummary.nodes ?
                            structuralController.meshSummary.nodes + " nodes  •  " + structuralController.meshSummary.elements + " tetrahedra\n" +
                            structuralController.meshSummary.exterior_faces + " exterior faces  •  " + structuralController.meshSummary.surface_patches + " visual patches\n" +
                            Number(structuralController.meshSummary.exterior_area_m2).toExponential(5) + " m² exterior area" :
                            "Load an isolated Gmsh/Abaqus C3D4 mesh. Visual patches are geometric selection aids, not inferred contacts or fixtures."
                    }
                    Rectangle {
                        objectName: "structuralMeshViewport"
                        Layout.fillWidth: true
                        Layout.preferredHeight: structuralController.meshGeometry ? 175 : 0
                        visible: structuralController.meshGeometry !== null
                        color: "#10161b"
                        border.color: lineColor
                        View3D {
                            anchors.fill: parent
                            environment: SceneEnvironment {
                                backgroundMode: SceneEnvironment.Color
                                clearColor: "#10161b"
                                antialiasingMode: SceneEnvironment.MSAA
                            }
                            Node {
                                id: meshOrbit
                                position: Qt.vector3d(
                                    structuralController.meshSummary.center_x_mm || 0,
                                    structuralController.meshSummary.center_y_mm || 0,
                                    structuralController.meshSummary.center_z_mm || 0)
                                eulerRotation: Qt.vector3d(-20, -30, 0)
                                PerspectiveCamera {
                                    id: meshCamera
                                    z: 3.2 * (structuralController.meshSummary.radius_mm || 1)
                                    clipNear: Math.max(0.01, (structuralController.meshSummary.radius_mm || 1) * 0.01)
                                    clipFar: Math.max(1000, (structuralController.meshSummary.radius_mm || 1) * 20)
                                }
                            }
                            DirectionalLight { eulerRotation: Qt.vector3d(-35, -35, 0); brightness: 1.2 }
                            Model {
                                geometry: structuralController.meshGeometry
                                materials: PrincipledMaterial {
                                    baseColor: "#5e7c91"
                                    opacity: 0.45
                                    alphaMode: PrincipledMaterial.Blend
                                    roughness: 0.8
                                    cullMode: Material.NoCulling
                                }
                            }
                            Model {
                                geometry: structuralController.highlightGeometry
                                materials: PrincipledMaterial {
                                    baseColor: "#f1b24c"
                                    emissiveFactor: Qt.vector3d(0.25, 0.12, 0.01)
                                    roughness: 0.65
                                    cullMode: Material.NoCulling
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            property real previousX
                            property real previousY
                            onPressed: mouse => { previousX = mouse.x; previousY = mouse.y }
                            onPositionChanged: mouse => {
                                if (!pressed) return
                                meshOrbit.eulerRotation.y += mouse.x - previousX
                                meshOrbit.eulerRotation.x += mouse.y - previousY
                                previousX = mouse.x
                                previousY = mouse.y
                            }
                            onWheel: wheel => {
                                meshCamera.z = Math.max(
                                    1.2 * (structuralController.meshSummary.radius_mm || 1),
                                    meshCamera.z * (wheel.angleDelta.y > 0 ? 0.88 : 1.14))
                            }
                        }
                    }
                    Label {
                        objectName: "selectedSurfaceSummary"
                        Layout.fillWidth: true
                        visible: structuralController.activeSurfacePatch.id !== undefined
                        text: visible ?
                            "INSPECTING PATCH " + structuralController.activeSurfacePatch.id +
                            "  •  " + Number(structuralController.activeSurfacePatch.area_m2).toExponential(4) + " m²\n" +
                            "centroid [" + Number(structuralController.activeSurfacePatch.centroid_x_m).toExponential(2) + ", " +
                            Number(structuralController.activeSurfacePatch.centroid_y_m).toExponential(2) + ", " +
                            Number(structuralController.activeSurfacePatch.centroid_z_m).toExponential(2) + "] m" :
                            "Select a patch to inspect its exact area and centroid."
                        color: "#e0ac62"
                        wrapMode: Text.WordWrap
                        font.pixelSize: 10
                    }
                    Label { text: "Select exact surface roles"; color: textColor; font.bold: true }
                    ListView {
                        id: patchList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 3
                        model: structuralController.surfacePatches
                        delegate: Rectangle {
                            required property var modelData
                            width: patchList.width
                            height: 67
                            color: structuralController.activeSurfacePatch.id === modelData.id ? "#29343d" : "#151b20"
                            border.color: structuralController.activeSurfacePatch.id === modelData.id ? "#e0ac62" : lineColor
                            TapHandler {
                                onTapped: structuralController.setActiveSurfacePatch(modelData.id)
                            }
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 1
                                Label { text: "Patch " + modelData.id + "  •  " + modelData.face_count + " faces  •  " + Number(modelData.area_m2).toExponential(3) + " m²"; color: textColor; font.pixelSize: 11 }
                                Label { text: "n = [" + Number(modelData.normal_x).toFixed(2) + ", " + Number(modelData.normal_y).toFixed(2) + ", " + Number(modelData.normal_z).toFixed(2) + "]"; color: mutedColor; font.pixelSize: 10 }
                                RowLayout {
                                    CheckBox {
                                        text: "Load"
                                        palette.text: textColor
                                        palette.windowText: textColor
                                        checked: structuralController.selectedLoadPatchIds.indexOf(modelData.id) >= 0
                                        onToggled: {
                                            structuralController.setPatchSelected(modelData.id, "load", checked)
                                            root.invalidateLoadReview()
                                        }
                                    }
                                    CheckBox {
                                        text: "Fully fixed"
                                        palette.text: textColor
                                        palette.windowText: textColor
                                        checked: structuralController.selectedRestraintPatchIds.indexOf(modelData.id) >= 0
                                        onToggled: {
                                            structuralController.setPatchSelected(modelData.id, "restraint", checked)
                                            root.invalidateRestraintReview()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                background: Rectangle { color: "#1b2228"; border.color: lineColor }
                ScrollView {
                    anchors.fill: parent
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 7
                        Label { text: "2  REVIEW INPUTS"; color: textColor; font.bold: true }
                        Label { text: "Maximum coarse-to-fine change"; color: mutedColor; visible: !root.modalActive }
                        TextField {
                            id: refinementMaximumChange
                            objectName: "refinementMaximumChange"
                            Layout.fillWidth: true
                            visible: !root.modalActive
                            text: "0.10"
                            enabled: !structuralController.sharedInputsLocked
                            validator: DoubleValidator { bottom: 0; top: 1 }
                        }
                        Label {
                            text: "MODAL_FREQUENCY  •  single-run analysis, no coarse/fine mesh-convergence pass"
                            visible: root.modalActive
                            color: mutedColor
                            wrapMode: Text.WordWrap
                            font.pixelSize: 10
                        }
                        Button {
                            objectName: "discardRefinementBaseline"
                            Layout.fillWidth: true
                            visible: structuralController.hasRefinementBaseline
                            text: "Discard baseline and start over"
                            onClicked: structuralController.discardRefinementBaseline()
                        }
                        Button {
                            objectName: "materialEvidenceButton"
                            text: structuralController.materialCandidates.length > 0 ?
                                "Material evidence loaded ✓" : "Load material evidence…"
                            Layout.fillWidth: true
                            enabled: !structuralController.sharedInputsLocked
                            onClicked: materialEvidenceDialog.open()
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            enabled: !structuralController.sharedInputsLocked
                            ComboBox {
                                id: materialCandidateSelector
                                objectName: "materialCandidateSelector"
                                Layout.fillWidth: true
                                model: structuralController.materialCandidates
                                textRole: "designation"
                                valueRole: "candidate_id"
                                displayText: currentIndex >= 0 ? currentText : "Choose a bounded candidate"
                            }
                            ComboBox {
                                id: materialApplicability
                                Layout.preferredWidth: 110
                                model: ["unresolved", "known", "assumed"]
                                onActivated: root.invalidateMaterialReview()
                            }
                            Button {
                                text: "Use"
                                enabled: materialCandidateSelector.currentIndex >= 0 &&
                                         materialApplicability.currentText !== "unresolved"
                                onClicked: {
                                    structuralController.selectMaterialCandidate(
                                        String(materialCandidateSelector.currentValue),
                                        materialApplicability.currentText)
                                    root.applyMaterialDraft()
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: "Choosing evidence fills fields only. You must still review applicability and reconfirm the scenario."
                            color: mutedColor
                            wrapMode: Text.WordWrap
                            font.pixelSize: 10
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            enabled: !structuralController.sharedInputsLocked
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 5
                            Label { text: "Analysis ID"; color: mutedColor }
                            TextField { id: analysisId; Layout.fillWidth: true; placeholderText: "stable analysis identity" }
                            Label { text: "Component"; color: mutedColor }
                            TextField { id: componentName; Layout.fillWidth: true; placeholderText: "selected component" }
                            Label { text: "Geometry SHA-256"; color: mutedColor }
                            TextField { id: geometryHash; Layout.fillWidth: true; placeholderText: "sha256:…"; onTextEdited: root.invalidateMeshReview() }
                            Label { text: "Material designation"; color: mutedColor }
                            TextField { id: materialName; Layout.fillWidth: true; placeholderText: "exact alloy and temper"; onTextEdited: root.invalidateMaterialReview() }
                            Label { text: "Temper / condition"; color: mutedColor }
                            TextField { id: materialTemper; Layout.fillWidth: true; placeholderText: "exact temper or not applicable"; onTextEdited: root.invalidateMaterialReview() }
                            Label { text: "Product form"; color: mutedColor }
                            TextField { id: materialProductForm; Layout.fillWidth: true; placeholderText: "plate, extrusion, casting…"; onTextEdited: root.invalidateMaterialReview() }
                            Label { text: "Material source SHA-256"; color: mutedColor }
                            TextField { id: materialHash; Layout.fillWidth: true; placeholderText: "sha256:…"; onTextEdited: root.invalidateMaterialReview() }
                            Label { text: "Young's modulus (Pa)"; color: mutedColor }
                            TextField {
                                id: youngsModulus
                                Layout.fillWidth: true
                                validator: DoubleValidator { bottom: 0 }
                                onTextEdited: root.invalidateMaterialReview()
                            }
                            Label { text: "Poisson ratio"; color: mutedColor }
                            TextField {
                                id: poissonRatio
                                Layout.fillWidth: true
                                validator: DoubleValidator { bottom: -0.999; top: 0.499 }
                                onTextEdited: root.invalidateMaterialReview()
                            }
                            Label { text: "Density (kg/m³)"; color: mutedColor; visible: root.modalActive }
                            TextField {
                                id: densityKgM3
                                Layout.fillWidth: true
                                visible: root.modalActive
                                validator: DoubleValidator { bottom: 0 }
                                onTextEdited: root.invalidateMaterialReview()
                            }
                        }
                        CheckBox { id: materialReviewed; enabled: !structuralController.sharedInputsLocked; text: "I reviewed material identity, source, applicability, and elastic properties" + (root.modalActive ? " and density" : ""); palette.text: textColor; palette.windowText: textColor }
                        Rectangle { Layout.fillWidth: true; height: 1; color: lineColor }
                        Label {
                            text: "A modal_frequency review has no applied load -- only the fixed restraint and material density feed the frequency solve."
                            visible: root.modalActive
                            color: mutedColor
                            wrapMode: Text.WordWrap
                            font.pixelSize: 10
                        }
                        Label { text: "Total surface force (N)"; color: textColor; font.bold: true; visible: !root.modalActive }
                        RowLayout {
                            visible: !root.modalActive
                            enabled: !structuralController.sharedInputsLocked
                            Label { text: "X"; color: mutedColor }
                            TextField {
                                id: forceX
                                text: "0"
                                Layout.fillWidth: true
                                validator: DoubleValidator {}
                                onTextEdited: root.invalidateLoadReview()
                            }
                            Label { text: "Y"; color: mutedColor }
                            TextField {
                                id: forceY
                                text: "0"
                                Layout.fillWidth: true
                                validator: DoubleValidator {}
                                onTextEdited: root.invalidateLoadReview()
                            }
                            Label { text: "Z"; color: mutedColor }
                            TextField {
                                id: forceZ
                                text: "0"
                                Layout.fillWidth: true
                                validator: DoubleValidator {}
                                onTextEdited: root.invalidateLoadReview()
                            }
                        }
                        RowLayout {
                            CheckBox { id: loadReviewed; visible: !root.modalActive; text: "Load selection and vector reviewed"; palette.text: textColor; palette.windowText: textColor }
                            CheckBox { id: restraintReviewed; text: "Fixed surface reviewed"; palette.text: textColor; palette.windowText: textColor }
                        }
                        CheckBox {
                            id: loadCorrespondenceReviewed
                            objectName: "loadCorrespondenceReviewed"
                            visible: structuralController.hasRefinementBaseline
                            text: "Fine load faces represent the same physical region as baseline"
                            palette.text: textColor
                            palette.windowText: textColor
                        }
                        CheckBox {
                            id: restraintCorrespondenceReviewed
                            objectName: "restraintCorrespondenceReviewed"
                            visible: structuralController.hasRefinementBaseline
                            text: "Fine restraint faces represent the same physical region as baseline"
                            palette.text: textColor
                            palette.windowText: textColor
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: lineColor }
                        GridLayout {
                            Layout.fillWidth: true; columns: 2
                            enabled: !structuralController.sharedInputsLocked
                            Label { text: "Displacement limit (m)"; color: mutedColor; visible: !root.modalActive }
                            TextField {
                                id: displacementLimit
                                Layout.fillWidth: true
                                visible: !root.modalActive
                                text: "0"
                                validator: DoubleValidator { bottom: 0 }
                                onTextEdited: root.invalidateRequirementReview()
                            }
                            Label { text: "Displacement limit basis"; color: mutedColor; visible: !root.modalActive }
                            TextField { id: displacementLimitBasis; Layout.fillWidth: true; visible: !root.modalActive; placeholderText: "requirement, test, or exploratory basis"; onTextEdited: root.invalidateRequirementReview() }
                            Label { text: "Von Mises limit (Pa)"; color: mutedColor; visible: !root.modalActive }
                            TextField {
                                id: stressLimit
                                Layout.fillWidth: true
                                visible: !root.modalActive
                                text: "0"
                                validator: DoubleValidator { bottom: 0 }
                                onTextEdited: root.invalidateRequirementReview()
                            }
                            Label { text: "Stress limit basis"; color: mutedColor; visible: !root.modalActive }
                            TextField { id: stressLimitBasis; Layout.fillWidth: true; visible: !root.modalActive; placeholderText: "allowable source or exploratory basis"; onTextEdited: root.invalidateRequirementReview() }
                            Label { text: "Minimum natural frequency (Hz)"; color: mutedColor }
                            TextField {
                                id: naturalFrequencyLimit
                                Layout.fillWidth: true
                                text: "0"
                                validator: DoubleValidator { bottom: 0 }
                                onTextEdited: root.invalidateRequirementReview()
                            }
                            Label { text: "Frequency limit basis"; color: mutedColor; visible: root.modalActive }
                            TextField { id: naturalFrequencyLimitBasis; Layout.fillWidth: true; visible: root.modalActive; placeholderText: "requirement, test, or exploratory basis"; onTextEdited: root.invalidateRequirementReview() }
                            Label { text: "Applicability"; color: mutedColor }
                            TextField { id: requirementApplicability; Layout.fillWidth: true; placeholderText: "condition under which these limits apply"; onTextEdited: root.invalidateRequirementReview() }
                            Label { text: "Criticality"; color: mutedColor }
                            ComboBox { id: requirementCriticality; Layout.fillWidth: true; model: ["informational", "advisory", "critical"]; currentIndex: 1; onActivated: root.invalidateRequirementReview() }
                            Label { text: "Source or exploratory rationale"; color: mutedColor }
                            TextField { id: requirementRationale; Layout.fillWidth: true; onTextEdited: root.invalidateRequirementReview() }
                        }
                        CheckBox { id: requirementReviewed; enabled: !structuralController.sharedInputsLocked; text: "Requirement limits and rationale reviewed"; palette.text: textColor; palette.windowText: textColor }
                        Label { text: "Other requirement (recorded as uncovered work)"; color: mutedColor }
                        GridLayout {
                            Layout.fillWidth: true
                            enabled: !structuralController.sharedInputsLocked
                            columns: 2
                            Label { text: "Description"; color: mutedColor }
                            TextField { id: otherRequirementDescription; Layout.fillWidth: true; placeholderText: "e.g. fatigue life under duty cycle"; onTextEdited: root.invalidateRequirementReview() }
                            Label { text: "Unit"; color: mutedColor }
                            TextField { id: otherRequirementUnit; Layout.fillWidth: true; placeholderText: "e.g. cycles"; onTextEdited: root.invalidateRequirementReview() }
                            Label { text: "Limit value"; color: mutedColor }
                            TextField {
                                id: otherRequirementLimit
                                Layout.fillWidth: true
                                text: "0"
                                validator: DoubleValidator { bottom: 0 }
                                onTextEdited: root.invalidateRequirementReview()
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: lineColor }
                        GridLayout {
                            Layout.fillWidth: true; columns: 2
                            Label { text: "Minimum mesh size (m)"; color: mutedColor }
                            TextField {
                                id: meshMinimum
                                Layout.fillWidth: true
                                text: "0.001"
                                validator: DoubleValidator { bottom: 0 }
                                onTextEdited: root.invalidateMeshReview()
                            }
                            Label { text: "Maximum mesh size (m)"; color: mutedColor }
                            TextField {
                                id: meshMaximum
                                Layout.fillWidth: true
                                text: "0.003"
                                validator: DoubleValidator { bottom: 0 }
                                onTextEdited: root.invalidateMeshReview()
                            }
                            Label { text: "Target mesh size (m)"; color: mutedColor }
                            TextField {
                                id: meshTarget
                                Layout.fillWidth: true
                                text: "0.002"
                                validator: DoubleValidator { bottom: 0 }
                                onTextEdited: root.invalidateMeshReview()
                            }
                            Label { text: "Minimum mean-ratio quality"; color: mutedColor }
                            TextField {
                                id: minimumMeanRatioThreshold
                                Layout.fillWidth: true
                                text: "0.05"
                                validator: DoubleValidator { bottom: 0; top: 1 }
                                onTextEdited: root.invalidateMeshReview()
                            }
                            Label { text: "Mesher identity"; color: mutedColor }
                            TextField { id: mesherIdentity; Layout.fillWidth: true; text: "Gmsh 4.15.2"; onTextEdited: root.invalidateMeshReview() }
                        }
                        CheckBox { id: meshReviewed; text: "Mesh controls and mesher reviewed"; palette.text: textColor; palette.windowText: textColor }
                        Label { text: "Scenario description"; color: mutedColor }
                        TextArea { id: scenarioDescription; enabled: !structuralController.sharedInputsLocked; Layout.fillWidth: true; Layout.preferredHeight: 62; wrapMode: TextEdit.Wrap; placeholderText: "What is loaded, fixed, assumed, and intentionally excluded?"; onTextChanged: if (!structuralController.sharedInputsLocked) scenarioConfirmed.checked = false }
                        CheckBox { id: scenarioConfirmed; enabled: !structuralController.sharedInputsLocked; text: "I confirm this complete bounded scenario"; palette.text: textColor; palette.windowText: textColor }
                        Label {
                            Layout.fillWidth: true
                            text: "The criterion and correspondence confirmations are reviewed inputs. Prometheus derives all result changes, coverage, findings, and archive evidence from the two completed solver runs."
                            color: "#e0ac62"
                            wrapMode: Text.WordWrap
                            font.pixelSize: 10
                        }
                        Button { text: "Validate and preview request"; highlighted: true; Layout.fillWidth: true; onClicked: root.submitReview() }
                    }
                }
            }

            Frame {
                Layout.preferredWidth: 285
                Layout.fillHeight: true
                background: Rectangle { color: "#1b2228"; border.color: lineColor }
                ColumnLayout {
                    anchors.fill: parent
                    Label { text: "3  AUTHORITY CHECK"; color: textColor; font.bold: true }
                    Label {
                        objectName: "refinementStateSummary"
                        Layout.fillWidth: true
                        visible: structuralController.hasRefinementBaseline ||
                                 structuralController.refinementComparison.status !== undefined
                        text: "REFINEMENT STAGE  " + structuralController.refinementStage.toUpperCase() +
                              "\ncoarse  " + (structuralController.baselineRun.elements || 0) + " elements" +
                              "  •  load " + Number(structuralController.baselineRun.selected_load_area_m2 || 0).toExponential(3) + " m²" +
                              "  •  restraint " + Number(structuralController.baselineRun.selected_restraint_area_m2 || 0).toExponential(3) + " m²" +
                              "\nfine  " + (structuralController.requestPreview.elements || 0) + " elements" +
                              "  •  load " + Number(structuralController.requestPreview.selected_load_area_m2 || 0).toExponential(3) + " m²" +
                              "  •  restraint " + Number(structuralController.requestPreview.selected_restraint_area_m2 || 0).toExponential(3) + " m²" +
                              (structuralController.refinementComparison.status !== undefined ?
                               "\n" + structuralController.refinementComparison.status.toUpperCase() +
                               "  •  displacement Δ " + Number(structuralController.refinementComparison.displacement_change_fraction).toExponential(3) +
                               "  •  stress Δ " + Number(structuralController.refinementComparison.stress_change_fraction).toExponential(3) +
                               "\nmaximum Δ " + Number(structuralController.refinementComparison.maximum_change_fraction).toExponential(3) +
                               "  ≤  criterion " + Number(structuralController.refinementComparison.maximum_allowed_change_fraction).toExponential(3) :
                               "\ncomparison pending  •  criterion " + Number(refinementMaximumChange.text).toExponential(3))
                        color: root.workflowStatusColor
                        wrapMode: Text.WordWrap
                        font.pixelSize: 10
                    }
                    Label {
                        Layout.fillWidth: true
                        text: structuralController.canRun ? "READY FOR ISOLATED EXECUTION" : structuralController.blockers.length + " BLOCKER(S)"
                        color: root.workflowStatusColor
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: structuralController.resultGeometry ? 220 : 0
                        visible: structuralController.resultGeometry !== null
                        color: "#10161b"
                        border.color: lineColor
                        View3D {
                            anchors.fill: parent
                            environment: SceneEnvironment {
                                backgroundMode: SceneEnvironment.Color
                                clearColor: "#10161b"
                                antialiasingMode: SceneEnvironment.MSAA
                            }
                            Node {
                                id: resultOrbit
                                position: Qt.vector3d(
                                    structuralController.resultView.center_x_mm || 0,
                                    structuralController.resultView.center_y_mm || 0,
                                    structuralController.resultView.center_z_mm || 0)
                                eulerRotation: Qt.vector3d(-20, -30, 0)
                                PerspectiveCamera {
                                    id: resultCamera
                                    z: 3.2 * (structuralController.resultView.radius_mm || 1)
                                    clipNear: Math.max(0.01, (structuralController.resultView.radius_mm || 1) * 0.01)
                                    clipFar: Math.max(1000, (structuralController.resultView.radius_mm || 1) * 20)
                                }
                            }
                            DirectionalLight { eulerRotation: Qt.vector3d(-35, -35, 0); brightness: 1.2 }
                            Model {
                                geometry: structuralController.resultGeometry
                                materials: PrincipledMaterial {
                                    vertexColorsEnabled: true
                                    roughness: 0.72
                                    metalness: 0.0
                                    cullMode: Material.NoCulling
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            property real previousX
                            property real previousY
                            onPressed: mouse => { previousX = mouse.x; previousY = mouse.y }
                            onPositionChanged: mouse => {
                                if (!pressed) return
                                resultOrbit.eulerRotation.y += mouse.x - previousX
                                resultOrbit.eulerRotation.x += mouse.y - previousY
                                previousX = mouse.x; previousY = mouse.y
                            }
                            onWheel: wheel => {
                                resultCamera.z = Math.max(
                                    1.2 * (structuralController.resultView.radius_mm || 1),
                                    resultCamera.z * (wheel.angleDelta.y > 0 ? 0.88 : 1.14))
                            }
                        }
                        Label {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 7
                            color: "white"
                            font.pixelSize: 9
                            text: "VON MISES  blue 0 → red " +
                                  Number(structuralController.resultView.color_max_pa || 0).toExponential(3) +
                                  " Pa  •  deformation ×" + Number(structuralController.resultView.deformation_scale || 1).toFixed(1)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Button { text: root.calculixExecutable.toString() === "" ? "Select ccx…" : "ccx selected ✓"; onClicked: calculixDialog.open() }
                        Button { text: root.outputRoot.toString() === "" ? "Output folder…" : "Output selected ✓"; onClicked: outputDialog.open() }
                    }
                    Button {
                        Layout.fillWidth: true
                        text: root.modalActive ?
                              (structuralController.busy ? "Running modal analysis…" : "Run modal analysis") :
                              (structuralController.busy ?
                               (structuralController.hasRefinementBaseline ?
                                "Running fine comparison…" : "Running coarse baseline…") :
                               (structuralController.hasRefinementBaseline ?
                                "Run fine comparison" : "Run coarse baseline"))
                        highlighted: true
                        enabled: structuralController.canRun && !structuralController.busy && root.calculixExecutable.toString() !== "" && root.outputRoot.toString() !== ""
                        onClicked: structuralController.runAnalysis(root.calculixExecutable, root.outputRoot)
                    }
                    Button {
                        Layout.fillWidth: true
                        text: structuralController.busy && structuralController.status === "publishing_structural_archive" ? "Embedding run artifacts…" :
                              (structuralController.lastRun.project_anchored ? "Run embedded in project ✓" : "Embed verified run in project")
                        enabled: structuralController.lastRun.archived === true && !structuralController.lastRun.project_anchored && !structuralController.busy
                        onClicked: structuralController.commitLastRun()
                    }
                    Label {
                        visible: structuralController.storedRuns.length > 0
                        text: "PROJECT STRUCTURAL HISTORY"
                        color: mutedColor
                        font.bold: true
                        font.pixelSize: 10
                    }
                    ListView {
                        id: structuralHistory
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 112)
                        visible: structuralController.storedRuns.length > 0
                        model: structuralController.storedRuns
                        spacing: 3
                        clip: true
                        delegate: Button {
                            required property var modelData
                            required property int index
                            width: structuralHistory.width
                            text: modelData.analysis_id ?
                                  "Restore " + modelData.analysis_id + " • " + modelData.component_name +
                                  (modelData.source_current === false ? " • STALE SOURCE" : "") :
                                  "Structural run " + modelData.status
                            enabled: modelData.restorable && !structuralController.busy && root.outputRoot.toString() !== ""
                            onClicked: structuralController.restoreStoredRun(index, root.outputRoot)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: structuralController.lastRun.status !== undefined
                        text: "Last local run: " + structuralController.lastRun.status +
                              "\n" + (structuralController.lastRun.first_natural_frequency_hz !== undefined ?
                              "first natural frequency  " + Number(structuralController.lastRun.first_natural_frequency_hz).toExponential(5) + " Hz\n" :
                              structuralController.lastRun.maximum_displacement_m !== undefined ?
                              "max displacement  " + Number(structuralController.lastRun.maximum_displacement_m).toExponential(5) + " m at node " + structuralController.lastRun.maximum_displacement_node_id +
                              "\n  vector [" + Number(structuralController.lastRun.maximum_displacement_x_m).toExponential(3) + ", " + Number(structuralController.lastRun.maximum_displacement_y_m).toExponential(3) + ", " + Number(structuralController.lastRun.maximum_displacement_z_m).toExponential(3) + "] m" +
                              "\nmax von Mises  " + Number(structuralController.lastRun.maximum_von_mises_pa).toExponential(5) + " Pa at element " + structuralController.lastRun.maximum_stress_element_id + ", integration point " + structuralController.lastRun.maximum_stress_integration_point +
                              "\nfield coverage  " + structuralController.lastRun.displacement_rows + " nodal rows • " + structuralController.lastRun.stress_rows + " integration-point rows\n" : "") +
                              structuralController.lastRun.evaluated_obligations + " / " + structuralController.lastRun.declared_obligations + " obligations evaluated" +
                              (structuralController.lastRun.detail ? "\n" + structuralController.lastRun.detail : "") +
                              (structuralController.lastRun.archive_error ? "\narchive unavailable: " + structuralController.lastRun.archive_error : "")
                        color: root.workflowStatusColor
                        wrapMode: Text.WordWrap
                    }
                    ListView {
                        id: blockerList
                        Layout.fillWidth: true
                        Layout.fillHeight: structuralController.blockers.length > 0
                        Layout.preferredHeight: structuralController.blockers.length > 0 ? -1 : 0
                        visible: structuralController.blockers.length > 0
                        clip: true
                        spacing: 4
                        model: structuralController.blockers
                        delegate: Rectangle {
                            required property var modelData
                            width: blockerList.width
                            height: blockerText.implicitHeight + 18
                            color: "#2a211b"
                            border.color: "#8f6933"
                            Label {
                                id: blockerText
                                anchors.fill: parent
                                anchors.margins: 8
                                text: modelData.code + "\n" + modelData.message
                                color: "#e0b861"
                                wrapMode: Text.WordWrap
                                font.pixelSize: 10
                            }
                        }
                    }
                    ListView {
                        id: findingList
                        Layout.fillWidth: true
                        Layout.fillHeight: structuralController.findings.length > 0
                        Layout.preferredHeight: structuralController.findings.length > 0 ? -1 : 0
                        visible: structuralController.findings.length > 0
                        clip: true
                        spacing: 4
                        model: structuralController.findings
                        delegate: Rectangle {
                            required property var modelData
                            width: findingList.width
                            height: findingText.implicitHeight + 20
                            color: modelData.disposition === "violated" ? "#301d1d" :
                                   modelData.disposition === "cannot_answer" ? "#2a2620" : "#1b2a22"
                            border.color: modelData.disposition === "violated" ? "#b85450" :
                                          modelData.disposition === "cannot_answer" ? "#8f6933" : "#365f4b"
                            Label {
                                id: findingText
                                anchors.fill: parent
                                anchors.margins: 8
                                text: modelData.disposition + "\n" + modelData.obligation +
                                      (modelData.disposition === "cannot_answer" ?
                                          "\nno measured value  •  limit " + Number(modelData.limit).toExponential(5) + " " + modelData.unit :
                                          "\nmeasured " + Number(modelData.measured).toExponential(5) + " " + modelData.unit +
                                          "  •  limit " + Number(modelData.limit).toExponential(5) + " " + modelData.unit)
                                color: modelData.disposition === "violated" ? "#e87972" :
                                       modelData.disposition === "cannot_answer" ? "#e0b861" : "#70c99a"
                                wrapMode: Text.WordWrap
                                font.pixelSize: 10
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: structuralController.lastRun.assessment !== undefined
                        text: visible ?
                            "ASSESSMENT  " + structuralController.lastRun.assessment.verdict +
                            "  •  coverage " + structuralController.lastRun.assessment.coverage +
                            "  •  " + structuralController.lastRun.assessment.execution_state : ""
                        color: mutedColor
                        wrapMode: Text.WordWrap
                        font.pixelSize: 10
                    }
                    Label {
                        visible: structuralController.uncoveredRequirements.length > 0
                        text: "UNCOVERED REQUIREMENTS"
                        color: mutedColor
                        font.bold: true
                        font.pixelSize: 10
                    }
                    ListView {
                        id: uncoveredList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 90)
                        visible: structuralController.uncoveredRequirements.length > 0
                        clip: true
                        spacing: 4
                        model: structuralController.uncoveredRequirements
                        delegate: Rectangle {
                            required property var modelData
                            width: uncoveredList.width
                            height: uncoveredText.implicitHeight + 18
                            color: "#241f2a"
                            border.color: "#5a4c78"
                            Label {
                                id: uncoveredText
                                anchors.fill: parent
                                anchors.margins: 8
                                text: modelData.description + " (" + modelData.criticality + ")" +
                                      "\nlimit " + modelData.limit_value + " " + modelData.unit +
                                      "  •  " + modelData.rationale
                                color: "#c3b3e6"
                                wrapMode: Text.WordWrap
                                font.pixelSize: 10
                            }
                        }
                    }
                    Label {
                        visible: structuralController.reviewedInputHistory.length > 0
                        text: "REVIEWED INPUT HISTORY (THIS GEOMETRY)"
                        color: mutedColor
                        font.bold: true
                        font.pixelSize: 10
                    }
                    ListView {
                        id: reviewedInputHistoryList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 140)
                        visible: structuralController.reviewedInputHistory.length > 0
                        clip: true
                        spacing: 3
                        model: structuralController.reviewedInputHistory
                        delegate: Rectangle {
                            required property var modelData
                            width: reviewedInputHistoryList.width
                            height: historyText.implicitHeight + 14
                            color: modelData.active ? "#182420" : "#181b1e"
                            border.color: modelData.active ? "#365f4b" : "#2c3238"
                            opacity: modelData.active ? 1.0 : 0.65
                            Label {
                                id: historyText
                                anchors.fill: parent
                                anchors.margins: 6
                                text: "[" + modelData.kind + "] rev " + modelData.revision +
                                      (modelData.active ? "" : " (superseded)") +
                                      "\n" + modelData.summary
                                color: modelData.active ? "#9fd6bd" : mutedColor
                                wrapMode: Text.WordWrap
                                font.pixelSize: 9
                            }
                        }
                    }
                    Label {
                        objectName: "compiledEvidenceSummary"
                        Layout.fillWidth: true
                        visible: structuralController.canRun
                        text: "Compiled request\n" +
                              structuralController.requestPreview.nodes + " nodes  •  " + structuralController.requestPreview.elements + " elements\n" +
                              structuralController.requestPreview.fixed_nodes + " fixed nodes  •  " + structuralController.requestPreview.loaded_nodes + " loaded nodes\n" +
                              "selected load area  " + Number(structuralController.requestPreview.selected_load_area_m2).toExponential(4) + " m²\n" +
                              "resultant [" + Number(structuralController.requestPreview.resultant_force_x_n).toExponential(3) + ", " +
                              Number(structuralController.requestPreview.resultant_force_y_n).toExponential(3) + ", " +
                              Number(structuralController.requestPreview.resultant_force_z_n).toExponential(3) + "] N\n" +
                              "mesh quality  " + Number(structuralController.requestPreview.minimum_mean_ratio).toFixed(4) +
                              " ≥ " + Number(structuralController.requestPreview.minimum_mean_ratio_threshold).toFixed(4) + "\n" +
                              "mesh  " + structuralController.requestPreview.mesh_sha256 + "\n" +
                              "geometry  " + structuralController.requestPreview.geometry_sha256
                        color: textColor
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: structuralController.lastRun.project_anchored ?
                              "The immutable manifest, reviewed setup, exact deck, raw solver outputs, and captured streams are embedded in project history. This is not a safety or validation claim." :
                              "Local run artifacts are retained in the selected output folder but are not yet committed to the Prometheus project. Readiness or scoped non-violation does not mean the component is safe or validated for this real scenario."
                        color: mutedColor
                        wrapMode: Text.WordWrap
                        font.pixelSize: 10
                    }
                }
            }
        }
    }
}
