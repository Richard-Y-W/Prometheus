import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

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

    function submitReview() {
        structuralController.reviewSetup({
            analysis_id: analysisId.text,
            component_name: componentName.text,
            geometry_sha256: geometryHash.text,
            material_designation: materialName.text,
            material_source_sha256: materialHash.text,
            material_applicability: materialApplicability.text,
            youngs_modulus_pa: Number(youngsModulus.text),
            poisson_ratio: Number(poissonRatio.text),
            material_reviewed: materialReviewed.checked,
            force_x_n: Number(forceX.text), force_y_n: Number(forceY.text), force_z_n: Number(forceZ.text),
            load_reviewed: loadReviewed.checked,
            restraint_reviewed: restraintReviewed.checked,
            displacement_limit_m: Number(displacementLimit.text),
            von_mises_limit_pa: Number(stressLimit.text),
            requirement_rationale: requirementRationale.text,
            requirement_reviewed: requirementReviewed.checked,
            mesh_minimum_size_m: Number(meshMinimum.text),
            mesh_maximum_size_m: Number(meshMaximum.text),
            mesher_identity: mesherIdentity.text,
            mesh_controls_reviewed: meshReviewed.checked,
            scenario_description: scenarioDescription.text,
            scenario_confirmed: scenarioConfirmed.checked
        });
    }

    FileDialog {
        id: meshDialog
        title: "Open Gmsh Abaqus tetrahedral mesh"
        nameFilters: ["Abaqus input mesh (*.inp)", "All files (*)"]
        onAccepted: structuralController.loadMesh(selectedFile, Number(coordinateScale.text), Number(patchAngle.text))
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
                Label { text: "BOUNDED LINEAR-STATIC WORKFLOW"; color: mutedColor; font.bold: true; font.pixelSize: 11 }
                Label { text: "Structural setup review"; color: textColor; font.pixelSize: 23 }
                Label { text: "Status: " + structuralController.status; color: structuralController.canRun ? "#70c99a" : "#e0ac62" }
            }
            Item { Layout.fillWidth: true }
            Button { text: "×"; flat: true; onClicked: root.closeRequested() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: lineColor }
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
                        TextField { id: coordinateScale; text: "0.001"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0 } }
                        Label { text: "Angle°"; color: mutedColor }
                        TextField { id: patchAngle; text: "15"; Layout.preferredWidth: 55; validator: DoubleValidator { bottom: 0; top: 180 } }
                    }
                    Button { text: "Load generated tetra mesh…"; Layout.fillWidth: true; onClicked: meshDialog.open() }
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
                            color: "#151b20"
                            border.color: lineColor
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
                                        onToggled: structuralController.setPatchSelected(modelData.id, "load", checked)
                                    }
                                    CheckBox {
                                        text: "Fully fixed"
                                        palette.text: textColor
                                        palette.windowText: textColor
                                        checked: structuralController.selectedRestraintPatchIds.indexOf(modelData.id) >= 0
                                        onToggled: structuralController.setPatchSelected(modelData.id, "restraint", checked)
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
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 5
                            Label { text: "Analysis ID"; color: mutedColor }
                            TextField { id: analysisId; Layout.fillWidth: true; placeholderText: "stable analysis identity" }
                            Label { text: "Component"; color: mutedColor }
                            TextField { id: componentName; Layout.fillWidth: true; placeholderText: "selected component" }
                            Label { text: "Geometry SHA-256"; color: mutedColor }
                            TextField { id: geometryHash; Layout.fillWidth: true; placeholderText: "sha256:…" }
                            Label { text: "Material designation"; color: mutedColor }
                            TextField { id: materialName; Layout.fillWidth: true; placeholderText: "exact alloy and temper" }
                            Label { text: "Material source SHA-256"; color: mutedColor }
                            TextField { id: materialHash; Layout.fillWidth: true; placeholderText: "sha256:…" }
                            Label { text: "Applicability"; color: mutedColor }
                            TextField { id: materialApplicability; Layout.fillWidth: true; placeholderText: "condition/temper applicability" }
                            Label { text: "Young's modulus (Pa)"; color: mutedColor }
                            TextField { id: youngsModulus; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0 } }
                            Label { text: "Poisson ratio"; color: mutedColor }
                            TextField { id: poissonRatio; Layout.fillWidth: true; validator: DoubleValidator { bottom: -0.999; top: 0.499 } }
                        }
                        CheckBox { id: materialReviewed; text: "I reviewed material identity, source, applicability, and elastic properties"; palette.text: textColor; palette.windowText: textColor }
                        Rectangle { Layout.fillWidth: true; height: 1; color: lineColor }
                        Label { text: "Total surface force (N)"; color: textColor; font.bold: true }
                        RowLayout {
                            Label { text: "X"; color: mutedColor }
                            TextField { id: forceX; text: "0"; Layout.fillWidth: true; validator: DoubleValidator {} }
                            Label { text: "Y"; color: mutedColor }
                            TextField { id: forceY; text: "0"; Layout.fillWidth: true; validator: DoubleValidator {} }
                            Label { text: "Z"; color: mutedColor }
                            TextField { id: forceZ; text: "0"; Layout.fillWidth: true; validator: DoubleValidator {} }
                        }
                        RowLayout {
                            CheckBox { id: loadReviewed; text: "Load selection and vector reviewed"; palette.text: textColor; palette.windowText: textColor }
                            CheckBox { id: restraintReviewed; text: "Fixed surface reviewed"; palette.text: textColor; palette.windowText: textColor }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: lineColor }
                        GridLayout {
                            Layout.fillWidth: true; columns: 2
                            Label { text: "Displacement limit (m)"; color: mutedColor }
                            TextField { id: displacementLimit; Layout.fillWidth: true; text: "0"; validator: DoubleValidator { bottom: 0 } }
                            Label { text: "Von Mises limit (Pa)"; color: mutedColor }
                            TextField { id: stressLimit; Layout.fillWidth: true; text: "0"; validator: DoubleValidator { bottom: 0 } }
                            Label { text: "Source or exploratory rationale"; color: mutedColor }
                            TextField { id: requirementRationale; Layout.fillWidth: true }
                        }
                        CheckBox { id: requirementReviewed; text: "Requirement limits and rationale reviewed"; palette.text: textColor; palette.windowText: textColor }
                        Rectangle { Layout.fillWidth: true; height: 1; color: lineColor }
                        GridLayout {
                            Layout.fillWidth: true; columns: 2
                            Label { text: "Minimum mesh size (m)"; color: mutedColor }
                            TextField { id: meshMinimum; Layout.fillWidth: true; text: "0.001"; validator: DoubleValidator { bottom: 0 } }
                            Label { text: "Maximum mesh size (m)"; color: mutedColor }
                            TextField { id: meshMaximum; Layout.fillWidth: true; text: "0.003"; validator: DoubleValidator { bottom: 0 } }
                            Label { text: "Mesher identity"; color: mutedColor }
                            TextField { id: mesherIdentity; Layout.fillWidth: true; text: "Gmsh 4.15.2" }
                        }
                        CheckBox { id: meshReviewed; text: "Mesh controls and mesher reviewed"; palette.text: textColor; palette.windowText: textColor }
                        Label { text: "Scenario description"; color: mutedColor }
                        TextArea { id: scenarioDescription; Layout.fillWidth: true; Layout.preferredHeight: 62; wrapMode: TextEdit.Wrap; placeholderText: "What is loaded, fixed, assumed, and intentionally excluded?" }
                        CheckBox { id: scenarioConfirmed; text: "I confirm this complete bounded scenario"; palette.text: textColor; palette.windowText: textColor }
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
                        Layout.fillWidth: true
                        text: structuralController.canRun ? "READY FOR ISOLATED EXECUTION" : structuralController.blockers.length + " BLOCKER(S)"
                        color: structuralController.canRun ? "#70c99a" : "#e0ac62"
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Button { text: root.calculixExecutable.toString() === "" ? "Select ccx…" : "ccx selected ✓"; onClicked: calculixDialog.open() }
                        Button { text: root.outputRoot.toString() === "" ? "Output folder…" : "Output selected ✓"; onClicked: outputDialog.open() }
                    }
                    Button {
                        Layout.fillWidth: true
                        text: structuralController.busy ? "Running isolated solver…" : "Run reviewed analysis"
                        highlighted: true
                        enabled: structuralController.canRun && !structuralController.busy && root.calculixExecutable.toString() !== "" && root.outputRoot.toString() !== ""
                        onClicked: structuralController.runAnalysis(root.calculixExecutable, root.outputRoot)
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: structuralController.lastRun.status !== undefined
                        text: "Last local run: " + structuralController.lastRun.status +
                              "\n" + (structuralController.lastRun.maximum_displacement_m !== undefined ?
                              "max displacement  " + Number(structuralController.lastRun.maximum_displacement_m).toExponential(5) + " m at node " + structuralController.lastRun.maximum_displacement_node_id +
                              "\n  vector [" + Number(structuralController.lastRun.maximum_displacement_x_m).toExponential(3) + ", " + Number(structuralController.lastRun.maximum_displacement_y_m).toExponential(3) + ", " + Number(structuralController.lastRun.maximum_displacement_z_m).toExponential(3) + "] m" +
                              "\nmax von Mises  " + Number(structuralController.lastRun.maximum_von_mises_pa).toExponential(5) + " Pa at element " + structuralController.lastRun.maximum_stress_element_id + ", integration point " + structuralController.lastRun.maximum_stress_integration_point +
                              "\nfield coverage  " + structuralController.lastRun.displacement_rows + " nodal rows • " + structuralController.lastRun.stress_rows + " integration-point rows\n" : "") +
                              structuralController.lastRun.evaluated_obligations + " / " + structuralController.lastRun.declared_obligations + " obligations evaluated"
                        color: structuralController.lastRun.status === "completed" ? "#70c99a" : "#e87972"
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
                            color: modelData.disposition === "violated" ? "#301d1d" : "#1b2a22"
                            border.color: modelData.disposition === "violated" ? "#b85450" : "#365f4b"
                            Label {
                                id: findingText
                                anchors.fill: parent
                                anchors.margins: 8
                                text: modelData.disposition + "\n" + modelData.obligation +
                                      "\nmeasured " + Number(modelData.measured).toExponential(5) + " " + modelData.unit +
                                      "  •  limit " + Number(modelData.limit).toExponential(5) + " " + modelData.unit
                                color: modelData.disposition === "violated" ? "#e87972" : "#70c99a"
                                wrapMode: Text.WordWrap
                                font.pixelSize: 10
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: structuralController.canRun
                        text: "Compiled request\n" + structuralController.requestPreview.nodes + " nodes  •  " + structuralController.requestPreview.elements + " elements\n" + structuralController.requestPreview.fixed_nodes + " fixed nodes  •  " + structuralController.requestPreview.loaded_nodes + " loaded nodes"
                        color: textColor
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Local run artifacts are retained in the selected output folder but are not yet committed to the Prometheus project. Readiness or scoped non-violation does not mean the component is safe or validated for this real scenario."
                        color: mutedColor
                        wrapMode: Text.WordWrap
                        font.pixelSize: 10
                    }
                }
            }
        }
    }
}
